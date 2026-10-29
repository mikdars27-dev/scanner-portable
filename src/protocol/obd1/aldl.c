/*
 * protocol/obd1/aldl.c — GM ALDL OBD1 implementation.
 *
 * Receives ALDL stream frames at 8192 baud:
 *   [0xFF sync][address][length][data...][checksum]
 * Checksum = sum of address + length + data bytes (mod 256), complemented.
 *
 * Also supports request/response mode: scan tool sends [address][data...],
 * ECU replies with [address][length][data...][checksum].
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "aldl.h"

#include <Arduino.h>
#include <string.h>

/* ALDL sync byte that starts every frame. */
#define ALDL_SYNC_BYTE 0xFFU

/* Max data bytes per ALDL frame. */
#define ALDL_MAX_DATA 16U

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct aldl {
    aldl_config_t cfg;
    bool          initialized;
    uart_t       *uart;

    /* Receive state machine. */
    typedef enum {
        RX_WAIT_SYNC,   /* waiting for 0xFF */
        RX_ADDR,        /* reading address byte */
        RX_LEN,         /* reading length byte */
        RX_DATA,        /* reading data bytes */
        RX_CHECKSUM,    /* reading checksum byte */
    } rx_state_t;

    rx_state_t  rx_state;
    uint8_t     rx_buf[ALDL_MAX_DATA + 3]; /* addr + len + data + checksum */
    size_t      rx_idx;
    uint8_t     rx_expected_len;
    uint8_t     rx_checksum;
    uint32_t    rx_start_ms;
};

/* ------------------------------------------------------------------------
 * Checksum
 * ------------------------------------------------------------------------ */

/* ALDL checksum: two's complement of (address + length + data sum). */
static uint8_t aldl_checksum(const uint8_t *bytes, size_t n)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += bytes[i];
    }
    return (uint8_t)((~sum + 1U) & 0xFFU);
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

aldl_status_t aldl_init(const aldl_config_t *cfg, aldl_t **out_aldl)
{
    if (cfg == NULL || out_aldl == NULL) {
        return ALDL_ERR_INVALID_ARG;
    }
    if (cfg->uart_port == UART_PORT_0) {
        return ALDL_ERR_INVALID_ARG;
    }
    if (cfg->baud != ALDL_BAUD_8192 && cfg->baud != ALDL_BAUD_160) {
        return ALDL_ERR_INVALID_ARG;
    }
    if (cfg->timeout_ms == 0) {
        return ALDL_ERR_INVALID_ARG;
    }

    uart_config_t ucfg = {};
    ucfg.port          = cfg->uart_port;
    ucfg.tx_gpio       = cfg->tx_gpio;
    ucfg.rx_gpio       = cfg->rx_gpio;
    ucfg.baud          = cfg->baud;
    ucfg.invert_tx     = cfg->invert;
    ucfg.invert_rx     = cfg->invert;
    ucfg.rx_timeout_ms = cfg->timeout_ms;

    aldl_t *aldl = (aldl_t *)calloc(1, sizeof(*aldl));
    if (aldl == NULL) {
        return ALDL_ERR_INVALID_ARG;
    }

    uart_status_t st = uart_init(&ucfg, &aldl->uart);
    if (st != UART_OK) {
        free(aldl);
        return ALDL_ERR_INVALID_ARG;
    }

    aldl->cfg         = *cfg;
    aldl->initialized = true;
    aldl->rx_state    = RX_WAIT_SYNC;
    aldl->rx_idx      = 0;
    aldl->rx_start_ms = 0;

    *out_aldl = aldl;
    return ALDL_OK;
}

aldl_status_t aldl_deinit(aldl_t *aldl)
{
    if (aldl == NULL) {
        return ALDL_OK;
    }
    if (!aldl->initialized) {
        return ALDL_ERR_NOT_INITIALIZED;
    }

    if (aldl->uart != NULL) {
        (void)uart_deinit(aldl->uart);
        aldl->uart = NULL;
    }

    free(aldl);
    return ALDL_OK;
}

bool aldl_is_initialized(const aldl_t *aldl)
{
    return aldl != NULL && aldl->initialized;
}

aldl_status_t aldl_receive_frame(aldl_t *aldl, aldl_frame_t *out_frame,
                                 uint32_t timeout_ms)
{
    if (aldl == NULL || !aldl->initialized || out_frame == NULL) {
        return ALDL_ERR_INVALID_ARG;
    }

    uint32_t start = millis();
    uint32_t remaining = timeout_ms;

    while ((uint32_t)(millis() - start) < timeout_ms) {
        remaining = timeout_ms - (uint32_t)(millis() - start);

        uint8_t  byte = 0;
        size_t   len  = 0;
        uart_status_t st = uart_read(aldl->uart, &byte, 1, &len, remaining);
        if (st == UART_ERR_TIMEOUT) {
            continue; /* no byte yet, keep trying */
        }
        if (st != UART_OK || len == 0) {
            continue;
        }

        switch (aldl->rx_state) {
        case RX_WAIT_SYNC:
            if (byte == ALDL_SYNC_BYTE) {
                aldl->rx_state  = RX_ADDR;
                aldl->rx_idx    = 0;
                aldl->rx_checksum = 0;
                aldl->rx_start_ms = millis();
            }
            break;

        case RX_ADDR:
            aldl->rx_buf[aldl->rx_idx++] = byte;
            aldl->rx_checksum += byte;
            aldl->rx_state = RX_LEN;
            break;

        case RX_LEN:
            aldl->rx_buf[aldl->rx_idx++] = byte;
            aldl->rx_checksum += byte;
            aldl->rx_expected_len = byte;
            if (byte == 0) {
                /* Frame with no data: go straight to checksum. */
                aldl->rx_state = RX_CHECKSUM;
            } else if ((size_t)byte > ALDL_MAX_DATA) {
                /* Malformed length: resync. */
                aldl->rx_state = RX_WAIT_SYNC;
            } else {
                aldl->rx_state = RX_DATA;
            }
            break;

        case RX_DATA:
            aldl->rx_buf[aldl->rx_idx++] = byte;
            aldl->rx_checksum += byte;
            if (aldl->rx_idx >= aldl->rx_expected_len + 2) {
                /* addr + len + all data read; next is checksum. */
                aldl->rx_state = RX_CHECKSUM;
            }
            break;

        case RX_CHECKSUM: {
            uint8_t rx_cs = byte;
            uint8_t calc_cs = aldl_checksum(aldl->rx_buf, aldl->rx_idx);

            out_frame->address = aldl->rx_buf[0];
            out_frame->length  = aldl->rx_buf[1];
            memcpy(out_frame->data, aldl->rx_buf + 2,
                   out_frame->length > ALDL_MAX_DATA
                       ? ALDL_MAX_DATA : out_frame->length);
            out_frame->checksum = rx_cs;
            out_frame->valid    = (calc_cs == rx_cs);

            aldl->rx_state = RX_WAIT_SYNC;
            return out_frame->valid ? ALDL_OK : ALDL_ERR_PROTOCOL;
        }
        }
    }

    return ALDL_ERR_TIMEOUT;
}

aldl_status_t aldl_send_request(aldl_t *aldl, uint8_t address,
                                const uint8_t *data, size_t data_len,
                                uint32_t timeout_ms)
{
    if (aldl == NULL || !aldl->initialized) {
        return ALDL_ERR_NOT_INITIALIZED;
    }
    if (data_len > 0 && data == NULL) {
        return ALDL_ERR_INVALID_ARG;
    }
    if (data_len > ALDL_MAX_DATA) {
        return ALDL_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t tx_buf[ALDL_MAX_DATA + 2];
    tx_buf[0] = address;
    tx_buf[1] = (uint8_t)data_len;
    if (data_len > 0) {
        memcpy(tx_buf + 2, data, data_len);
    }
    uint8_t cs = aldl_checksum(tx_buf, data_len + 2);

    uint8_t frame[ALDL_MAX_DATA + 4];
    frame[0] = ALDL_SYNC_BYTE;
    memcpy(frame + 1, tx_buf, data_len + 2);
    frame[data_len + 3] = cs;

    uart_status_t st = uart_write(aldl->uart, frame, data_len + 4, timeout_ms);
    return (st == UART_OK) ? ALDL_OK : ALDL_ERR_TIMEOUT;
}

aldl_status_t aldl_extract_dtcs(const aldl_frame_t *frame,
                                dtc_list_t *list)
{
    if (frame == NULL || list == NULL || !frame->valid) {
        return ALDL_ERR_INVALID_ARG;
    }
    if (list->entries == NULL || list->capacity == 0) {
        return ALDL_ERR_INVALID_ARG;
    }

    /* ALDL DTCs are stored as 2-byte pairs in the frame data. The high bit
     * of the first byte indicates a valid code. Decode each pair via the
     * shared dtc_decode_list() and append to the caller's dtc_list_t. */
    size_t pair_count = frame->length / 2;
    if (pair_count == 0) {
        return ALDL_OK; /* no DTC bytes in frame */
    }

    /* dtc_decode_list decodes contiguous raw 2-byte DTCs from a buffer.
     * ALDL frames may have non-DTC data mixed in, so callers should verify
     * the frame address block contains DTC data before calling this. */
    dtc_status_t st = dtc_decode_list(frame->data,
                                      pair_count * 2U, list);
    return (st == DTC_OK) ? ALDL_OK : ALDL_ERR_PROTOCOL;
}
