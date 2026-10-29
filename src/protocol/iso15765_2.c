/*
 * protocol/iso15765_2.c — ISO 15765-2 CAN transport layer implementation.
 *
 * Single Frame (SF), First Frame (FF), Consecutive Frame (CF), Flow Control
 * (FC) per ISO 15765-2:2016. Built on top of drivers/can_mcp2515.c.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "protocol/iso15765_2.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal constants
 * ------------------------------------------------------------------------ */
#define ISO15765_SF_MAX_DATA   7   /* max data bytes in a Single Frame */
#define ISO15765_FF_MAX_DATA   6   /* max data bytes in a First Frame  */
#define ISO15765_CF_MAX_DATA   7   /* max data bytes in a Consecutive Frame */
#define ISO15765_MAX_DL        4095 /* max 12-bit data length */

#define ISO15765_DEFAULT_N_BS  1000UL   /* N_Bs block timeout (ms) */
#define ISO15765_DEFAULT_N_CS  0UL      /* N_Cs separation time (ms) */

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */
typedef struct {
    can_t      *can;
    uint32_t    tx_addr;
    uint32_t    rx_addr;
    uint32_t    timeout_ms;   /* N_As / N_Ar */
    uint8_t     bs;           /* advertised block size (0 = continuous) */
    uint8_t     stmin;        /* advertised STmin (ms) */
    bool        initialized;
} iso15765_t;

/* ------------------------------------------------------------------------
 * CAN frame helpers
 * ------------------------------------------------------------------------ */

/* Build a CAN frame with standard 11-bit ID, data frame (not RTR). */
static void make_frame(can_frame_t *f, uint32_t id,
                       const uint8_t *data, uint8_t dlc)
{
    f->id      = id;
    f->extended = false;
    f->rtr     = false;
    f->dlc     = dlc & 0x0F;
    if (f->dlc > 8) f->dlc = 8;
    memcpy(f->data, data, f->dlc);
}

/* ------------------------------------------------------------------------
 * PCI encode / decode
 * ------------------------------------------------------------------------ */

/* Encode SF PCI: [type=0 | dl(4 bits)] */
static uint8_t pci_sf(uint8_t dl)
{
    return (uint8_t)((ISO15765_PCI_SF << 4) | (dl & 0x0F));
}

/* Encode FF PCI (2 bytes): [type=1 | dl_high(4 bits)][dl_low(8 bits)] */
static void pci_ff(uint16_t dl, uint8_t *pci)
{
    pci[0] = (uint8_t)((ISO15765_PCI_FF << 4) | ((dl >> 8) & 0x0F));
    pci[1] = (uint8_t)(dl & 0xFF);
}

/* Encode CF PCI: [type=2 | sn(4 bits)] */
static uint8_t pci_cf(uint8_t sn)
{
    return (uint8_t)((ISO15765_PCI_CF << 4) | (sn & 0x0F));
}

/* Encode FC PCI (3 bytes): [type=3 | fs(4 bits)][bs][stmin] */
static void pci_fc(uint8_t fs, uint8_t bs, uint8_t stmin, uint8_t *pci)
{
    pci[0] = (uint8_t)((ISO15765_PCI_FC << 4) | (fs & 0x0F));
    pci[1] = bs;
    pci[2] = stmin;
}

/* Decode first PCI byte: returns type and low nibble. */
static void pci_decode_first(uint8_t pci0, uint8_t *type, uint8_t *low)
{
    *type = (uint8_t)(pci0 >> 4);
    *low  = (uint8_t)(pci0 & 0x0F);
}

/* ------------------------------------------------------------------------
 * Send helpers
 * ------------------------------------------------------------------------ */

/* Send one CAN frame. Returns ISO15765_OK or error. */
static int send_frame(iso15765_t *iso, uint32_t id,
                      const uint8_t *data, uint8_t dlc,
                      uint32_t timeout_ms)
{
    can_frame_t f;
    make_frame(&f, id, data, dlc);
    can_status_t st = can_write(iso->can, &f, timeout_ms);
    return (st == CAN_OK) ? 0 : -1;
}

/*
 * Send a Single Frame. tx_len must be 1..7.
 */
static int send_sf(iso15765_t *iso, const uint8_t *tx, size_t tx_len,
                   uint32_t timeout_ms)
{
    if (tx_len == 0 || tx_len > ISO15765_SF_MAX_DATA) return -1;
    uint8_t buf[1 + ISO15765_SF_MAX_DATA];
    buf[0] = pci_sf((uint8_t)tx_len);
    memcpy(&buf[1], tx, tx_len);
    return send_frame(iso, iso->tx_addr, buf, (uint8_t)(1 + tx_len), timeout_ms);
}

/*
 * Send a First Frame. tx_len must be > 7 and <= ISO15765_MAX_DL.
 * Returns the total data length (for caller to track).
 */
static int send_ff(iso15765_t *iso, const uint8_t *tx, size_t tx_len,
                   uint32_t timeout_ms)
{
    if (tx_len <= ISO15765_SF_MAX_DATA || tx_len > ISO15765_MAX_DL) return -1;
    uint8_t buf[2 + ISO15765_FF_MAX_DATA];
    pci_ff((uint16_t)tx_len, buf);
    size_t ff_data = (tx_len < ISO15765_FF_MAX_DATA) ? tx_len : ISO15765_FF_MAX_DATA;
    memcpy(&buf[2], tx, ff_data);
    return send_frame(iso, iso->tx_addr, buf, (uint8_t)(2 + ff_data), timeout_ms);
}

/*
 * Send a Consecutive Frame. seq is 1..15 (wraps).
 */
static int send_cf(iso15765_t *iso, const uint8_t *data, uint8_t data_len,
                   uint8_t seq, uint32_t timeout_ms)
{
    if (data_len == 0 || data_len > ISO15765_CF_MAX_DATA) return -1;
    uint8_t buf[1 + ISO15765_CF_MAX_DATA];
    buf[0] = pci_cf(seq);
    memcpy(&buf[1], data, data_len);
    return send_frame(iso, iso->tx_addr, buf, (uint8_t)(1 + data_len), timeout_ms);
}

/*
 * Send a Flow Control frame.
 */
static int send_fc(iso15765_t *iso, uint8_t fs, uint8_t bs, uint8_t stmin,
                   uint32_t timeout_ms)
{
    uint8_t buf[3];
    pci_fc(fs, bs, stmin, buf);
    return send_frame(iso, iso->tx_addr, buf, 3, timeout_ms);
}

/* ------------------------------------------------------------------------
 * Receive helpers (blocking, reads from CAN)
 * ------------------------------------------------------------------------ */

/*
 * Wait for a CAN frame from rx_addr. Fills frame. Returns 0 on success,
 * -1 on timeout or error.
 */
static int recv_frame(iso15765_t *iso, can_frame_t *frame, uint32_t timeout_ms)
{
    uint32_t start = (uint32_t)millis();
    for (;;) {
        bool avail = false;
        if (can_available(iso->can, &avail) != CAN_OK || !avail) {
            uint32_t now = (uint32_t)millis();
            if ((int32_t)(now - start) >= (int32_t)timeout_ms) {
                return -1;
            }
            /* Small yield delay; MCP2515 INT polling is fast enough. */
            delay(1);
            continue;
        }
        can_status_t st = can_read(iso->can, frame, 100);
        if (st != CAN_OK) return -1;
        if (frame->id == iso->rx_addr && !frame->extended && !frame->rtr) {
            return 0;
        }
        /* Frame from a different address — ignore and keep waiting. */
    }
}

/* ------------------------------------------------------------------------
 * Transmission: request (SF or FF+CF with FC handling)
 * ------------------------------------------------------------------------ */

/*
 * Transmit a request payload. Handles SF or FF+CF. Reads FC frames from the
 * ECU when sending multi-frame.
 */
static int transmit_request(iso15765_t *iso, const uint8_t *tx, size_t tx_len,
                            uint32_t timeout_ms)
{
    uint32_t deadline = (uint32_t)millis() + timeout_ms;

    if (tx_len <= ISO15765_SF_MAX_DATA) {
        return send_sf(iso, tx, tx_len, timeout_ms);
    }

    /* Multi-frame: send First Frame. */
    if (send_ff(iso, tx, tx_len, timeout_ms) < 0) {
        return -1;
    }

    /* Wait for Flow Control from ECU. */
    can_frame_t cf;
    for (;;) {
        uint32_t now = (uint32_t)millis();
        int32_t left = (int32_t)(deadline - now);
        if (left <= 0) return -1;

        if (recv_frame(iso, &cf, (uint32_t)left) < 0) {
            return -1;
        }
        uint8_t pci_type, pci_low;
        pci_decode_first(cf.data[0], &pci_type, &pci_low);

        if (pci_type == ISO15765_PCI_FC) {
            uint8_t fs = pci_low;
            if (fs == ISO15765_FS_OVFS) {
                return -1;   /* ECU overflow — abort */
            }
            if (fs == ISO15765_FS_WT) {
                /* ECU wants us to wait — poll again for another FC. */
                continue;
            }
            /* fs == CTS: proceed. */
            break;
        }
        return -1;   /* unexpected frame — protocol error */
    }

    /* Send Consecutive Frames in blocks. */
    size_t offset = ISO15765_FF_MAX_DATA;   /* bytes already sent in FF */
    uint8_t seq = 1;
    uint8_t block_count = 0;

    while (offset < tx_len) {
        uint8_t chunk[ISO15765_CF_MAX_DATA];
        size_t chunk_len = tx_len - offset;
        if (chunk_len > ISO15765_CF_MAX_DATA) {
            chunk_len = ISO15765_CF_MAX_DATA;
        }
        memcpy(chunk, &tx[offset], chunk_len);

        if (send_cf(iso, chunk, (uint8_t)chunk_len, seq, timeout_ms) < 0) {
            return -1;
        }

        offset += chunk_len;
        seq = (uint8_t)((seq % 15) + 1);   /* 1..15, wrap */

        /* Block size handling: after `bs` CFs, wait for next FC. */
        if (iso->bs > 0) {
            block_count++;
            if (block_count >= iso->bs && offset < tx_len) {
                block_count = 0;
                /* Wait for next Flow Control. */
                for (;;) {
                    uint32_t now = (uint32_t)millis();
                    int32_t left = (int32_t)(deadline - now);
                    if (left <= 0) return -1;

                    if (recv_frame(iso, &cf, (uint32_t)left) < 0) {
                        return -1;
                    }
                    uint8_t pt, pl;
                    pci_decode_first(cf.data[0], &pt, &pl);
                    if (pt == ISO15765_PCI_FC) {
                        if (pl == ISO15765_FS_CTS) break;
                        if (pl == ISO15765_FS_WT) continue;
                        return -1;
                    }
                    return -1;
                }
            }
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------
 * Reception: response (SF or FF+CF with FC handling)
 * ------------------------------------------------------------------------ */

/*
 * Receive a response payload. Handles SF or FF+CF. Sends FC frames to the
 * ECU when receiving multi-frame. Stores result in rx buffer.
 */
static int receive_response(iso15765_t *iso, uint8_t *rx, size_t rx_buf_size,
                            size_t *out_len, uint32_t timeout_ms)
{
    uint32_t deadline = (uint32_t)millis() + timeout_ms;

    can_frame_t cf;
    if (recv_frame(iso, &cf, timeout_ms) < 0) {
        return -1;
    }

    uint8_t pci_type, pci_low;
    pci_decode_first(cf.data[0], &pci_type, &pci_low);

    if (pci_type == ISO15765_PCI_SF) {
        uint8_t dl = pci_low;
        if (dl == 0 || dl > ISO15765_SF_MAX_DATA) {
            return -1;
        }
        if ((size_t)(1 + dl) > cf.dlc) return -1;
        if (dl > rx_buf_size) return -1;
        memcpy(rx, &cf.data[1], dl);
        *out_len = (size_t)dl;
        return 0;
    }

    if (pci_type == ISO15765_PCI_FF) {
        uint16_t total_dl = (uint16_t)(((uint16_t)pci_low & 0x0F) << 8) | cf.data[1];
        if (total_dl == 0 || total_dl > ISO15765_MAX_DL) {
            return -1;
        }
        if ((size_t)total_dl > rx_buf_size) {
            return -1;   /* buffer too small */
        }

        /* Copy FF data bytes (bytes 2..dlc-1). */
        size_t ff_data_len = cf.dlc - 2;
        if (ff_data_len > total_dl) ff_data_len = total_dl;
        memcpy(rx, &cf.data[2], ff_data_len);
        size_t offset = ff_data_len;

        /* Send Flow Control: CTS with our bs and stmin. */
        if (send_fc(iso, ISO15765_FS_CTS, iso->bs, iso->stmin,
                    timeout_ms) < 0) {
            return -1;
        }

        /* Receive Consecutive Frames. */
        uint8_t seq = 1;
        while (offset < total_dl) {
            uint32_t now = (uint32_t)millis();
            int32_t left = (int32_t)(deadline - now);
            if (left <= 0) return -1;

            if (recv_frame(iso, &cf, (uint32_t)left) < 0) {
                return -1;
            }
            pci_decode_first(cf.data[0], &pci_type, &pci_low);
            if (pci_type != ISO15765_PCI_CF) {
                return -1;
            }
            if (pci_low != seq) {
                return -1;   /* sequence mismatch */
            }

            size_t cf_data_len = cf.dlc - 1;
            if (cf_data_len > ISO15765_CF_MAX_DATA) cf_data_len = ISO15765_CF_MAX_DATA;
            if (offset + cf_data_len > total_dl) {
                cf_data_len = total_dl - offset;
            }
            memcpy(&rx[offset], &cf.data[1], cf_data_len);
            offset += cf_data_len;
            seq = (uint8_t)((seq % 15) + 1);

            /* Block size: after `bs` CFs received, send next FC. */
            if (iso->bs > 0) {
                static uint8_t rx_block_count = 0;
                rx_block_count++;
                if (rx_block_count >= iso->bs && offset < total_dl) {
                    rx_block_count = 0;
                    if (send_fc(iso, ISO15765_FS_CTS, iso->bs, iso->stmin,
                                timeout_ms) < 0) {
                        return -1;
                    }
                }
            }
        }

        *out_len = total_dl;
        return 0;
    }

    return -1;   /* unexpected PCI type */
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

iso15765_status_t iso15765_init(const iso15765_config_t *cfg,
                                iso15765_t **out_iso)
{
    if (cfg == NULL || out_iso == NULL || cfg->can == NULL) {
        return ISO15765_ERR_INVALID_ARG;
    }

    iso15765_t *iso = (iso15765_t *)calloc(1, sizeof(*iso));
    if (iso == NULL) {
        return ISO15765_ERR_INVALID_ARG;
    }

    iso->can        = cfg->can;
    iso->tx_addr    = cfg->tx_addr;
    iso->rx_addr    = cfg->rx_addr;
    iso->timeout_ms = cfg->timeout_ms ? cfg->timeout_ms : 1000UL;
    iso->bs         = cfg->bs;
    iso->stmin      = cfg->stmin;
    iso->initialized = true;

    *out_iso = iso;
    return ISO15765_OK;
}

iso15765_status_t iso15765_deinit(iso15765_t *iso)
{
    if (iso == NULL) return ISO15765_ERR_INVALID_ARG;
    free(iso);
    return ISO15765_OK;
}

bool iso15765_is_initialized(const iso15765_t *iso)
{
    return iso != NULL && iso->initialized;
}

iso15765_status_t iso15765_set_flow_control(iso15765_t *iso,
                                            uint8_t bs, uint8_t stmin)
{
    if (iso == NULL) return ISO15765_ERR_INVALID_ARG;
    iso->bs   = bs;
    iso->stmin = stmin;
    return ISO15765_OK;
}

iso15765_status_t iso15765_send_request(iso15765_t *iso,
                                        const uint8_t *tx, size_t tx_len,
                                        uint8_t *rx, size_t rx_buf_size,
                                        size_t *out_len,
                                        uint32_t timeout_ms)
{
    if (iso == NULL || tx == NULL || rx == NULL || out_len == NULL) {
        return ISO15765_ERR_INVALID_ARG;
    }
    if (!iso->initialized) {
        return ISO15765_ERR_NOT_INITIALIZED;
    }
    if (tx_len == 0 || tx_len > ISO15765_MAX_DL) {
        return ISO15765_ERR_INVALID_ARG;
    }

    /* Transmit request. */
    if (transmit_request(iso, tx, tx_len, timeout_ms) < 0) {
        return ISO15765_ERR_TIMEOUT;
    }

    /* Receive response. */
    if (receive_response(iso, rx, rx_buf_size, out_len, timeout_ms) < 0) {
        return ISO15765_ERR_NO_RESPONSE;
    }

    return ISO15765_OK;
}

/* ------------------------------------------------------------------------
 * Non-blocking poll (stub — frame assembly handled by send_request above)
 * ------------------------------------------------------------------------ */

iso15765_status_t iso15765_poll(iso15765_t *iso, uint32_t timeout_ms)
{
    (void)iso;
    (void)timeout_ms;
    /* Blocking send_request covers the common case. Poll-based reception
     * can be added later for event-driven architectures. */
    return ISO15765_OK;
}

iso15765_status_t iso15765_rx_get(iso15765_t *iso,
                                  uint8_t *rx, size_t rx_buf_size,
                                  size_t *out_len, uint32_t timeout_ms)
{
    (void)iso;
    (void)rx;
    (void)rx_buf_size;
    (void)out_len;
    (void)timeout_ms;
    return ISO15765_OK;
}