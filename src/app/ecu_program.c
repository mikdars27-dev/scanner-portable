/*
 * app/ecu_program.c — ECU program / reset implementation (Feature 4: Program ECU).
 *
 * UDS $11 ECUReset, $10 DiagnosticSessionControl (programming session),
 * $27 SecurityAccess (seed/key), and $36 TransferData over K-Line/UDS via
 * kline_proto_*.
 *
 * Safety: warning_show(WARN_ECU_FLASH) before any operation that writes to the
 * ECU (reset, enter programming, download); SELECT is required.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "ecu_program.h"

#include <Arduino.h>
#include <string.h>

struct ecu_program {
    ecu_program_config_t cfg;
    bool                  initialized;
};

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

/* UDS service IDs used directly via kline_proto_send (no dedicated helper). */
#define ECU_PROGRAM_SID_ECU_RESET             0x11U
#define ECU_PROGRAM_SID_REQUEST_DOWNLOAD      0x34U
#define ECU_PROGRAM_SID_TRANSFER_DATA         0x36U
#define ECU_PROGRAM_SID_REQUEST_TRANSFER_EXIT 0x37U

/* UDS ECUReset subfunction types. */
#define ECU_PROGRAM_RESET_HARD     0x01U   /* hard reset */
#define ECU_PROGRAM_RESET_KEY_OFF  0x02U   /* key off / on */
#define ECU_PROGRAM_RESET_SOFT     0x03U   /* soft reset */

/* Max transfer data per kline_proto_send call. UDS $36 TransferData payload
 * is [block_sequence_counter][data...], so reserve 1 byte for the counter. */
#define ECU_PROGRAM_DOWNLOAD_CHUNK_MAX 240U

/* Warning screen timeout (same 30s cap as actuator tests). */
#define ECU_PROGRAM_WARN_TIMEOUT_MS ACTUATOR_TEST_MAX_MS

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static ecu_program_status_t ecu_program_from_kline(kline_proto_status_t st)
{
    switch (st) {
        case KLINE_PROTO_OK:                    return ECU_PROGRAM_OK;
        case KLINE_PROTO_ERR_INVALID_ARG:       return ECU_PROGRAM_ERR_INVALID_ARG;
        case KLINE_PROTO_ERR_NOT_INITIALIZED:   return ECU_PROGRAM_ERR_NOT_INITIALIZED;
        case KLINE_PROTO_ERR_TIMEOUT:           return ECU_PROGRAM_ERR_TIMEOUT;
        case KLINE_PROTO_ERR_BUFFER_TOO_SMALL:  return ECU_PROGRAM_ERR_PROTOCOL;
        case KLINE_PROTO_ERR_NEGATIVE_RESPONSE: return ECU_PROGRAM_ERR_PROTOCOL;
        default:                                return ECU_PROGRAM_ERR_PROTOCOL;
    }
}

/* Show the ECU flash warning screen. Returns true if the user selected
 * (proceed), false if the user aborted. */
static bool ecu_program_show_warning(ecu_program_t *ep)
{
    return warning_show(ep->cfg.disp, WARN_ECU_FLASH, NULL,
                        ECU_PROGRAM_WARN_TIMEOUT_MS);
}

/* Send a raw UDS request over K-Line via kline_proto_send. */
static ecu_program_status_t ecu_program_send_raw(ecu_program_t *ep,
        uint8_t sid, const uint8_t *data, size_t data_len, uint32_t timeout_ms)
{
    kline_proto_t *kline = proto_kline_handle(ep->cfg.proto);
    if (kline == NULL) {
        return ECU_PROGRAM_ERR_NOT_SUPPORTED;
    }

    uint8_t resp[16];
    size_t  resp_len = 0;
    kline_proto_status_t pst = kline_proto_send(kline, sid, data, data_len,
                                                resp, sizeof(resp), &resp_len,
                                                timeout_ms);
    return ecu_program_from_kline(pst);
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

ecu_program_status_t ecu_program_init(const ecu_program_config_t *cfg,
                                      ecu_program_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL || cfg->disp == NULL) {
        return ECU_PROGRAM_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return ECU_PROGRAM_ERR_INVALID_ARG;
    }
    if (proto_get_transport(cfg->proto) != PROTO_TRANSPORT_KLINE) {
        /* ECU program (UDS over K-Line) requires K-Line transport. */
        return ECU_PROGRAM_ERR_NOT_SUPPORTED;
    }

    ecu_program_t *ep = (ecu_program_t *)calloc(1, sizeof(*ep));
    if (ep == NULL) {
        return ECU_PROGRAM_ERR_INVALID_ARG;
    }

    ep->cfg         = *cfg;
    ep->initialized = true;

    *out = ep;
    return ECU_PROGRAM_OK;
}

ecu_program_status_t ecu_program_deinit(ecu_program_t *ep)
{
    if (ep == NULL) {
        return ECU_PROGRAM_ERR_INVALID_ARG;
    }
    ep->initialized = false;
    free(ep);
    return ECU_PROGRAM_OK;
}

bool ecu_program_is_initialized(const ecu_program_t *ep)
{
    return ep != NULL && ep->initialized;
}

/* ------------------------------------------------------------------------
 * ECU programming operations
 * ------------------------------------------------------------------------ */

ecu_program_status_t ecu_program_reset(ecu_program_t *ep, uint32_t timeout_ms)
{
    if (ep == NULL) return ECU_PROGRAM_ERR_INVALID_ARG;
    if (!ep->initialized) return ECU_PROGRAM_ERR_NOT_INITIALIZED;
    if (timeout_ms == 0) return ECU_PROGRAM_ERR_INVALID_ARG;

    /* Safety: warning screen before resetting the ECU. */
    if (!ecu_program_show_warning(ep)) {
        return ECU_PROGRAM_ERR_USER_ABORT;
    }

    /* UDS $11 ECUReset, subfunction 0x01 (hard reset). */
    uint8_t data[1] = { ECU_PROGRAM_RESET_HARD };
    return ecu_program_send_raw(ep, ECU_PROGRAM_SID_ECU_RESET,
                                data, sizeof(data), timeout_ms);
}

ecu_program_status_t ecu_program_enter_programming(ecu_program_t *ep,
                                                    uint32_t timeout_ms)
{
    if (ep == NULL) return ECU_PROGRAM_ERR_INVALID_ARG;
    if (!ep->initialized) return ECU_PROGRAM_ERR_NOT_INITIALIZED;
    if (timeout_ms == 0) return ECU_PROGRAM_ERR_INVALID_ARG;

    /* Safety: warning screen before entering programming mode. */
    if (!ecu_program_show_warning(ep)) {
        return ECU_PROGRAM_ERR_USER_ABORT;
    }

    kline_proto_t *kline = proto_kline_handle(ep->cfg.proto);
    if (kline == NULL) {
        return ECU_PROGRAM_ERR_NOT_SUPPORTED;
    }

    kline_proto_status_t pst = kline_proto_uds_session(kline,
            KLINE_PROTO_SESSION_PROGRAMMING, timeout_ms);
    return ecu_program_from_kline(pst);
}

ecu_program_status_t ecu_program_request_seed(ecu_program_t *ep, uint8_t level,
                                              uint8_t *seed, size_t *out_len,
                                              uint32_t timeout_ms)
{
    if (ep == NULL) return ECU_PROGRAM_ERR_INVALID_ARG;
    if (!ep->initialized) return ECU_PROGRAM_ERR_NOT_INITIALIZED;
    if (seed == NULL || out_len == NULL || timeout_ms == 0) {
        return ECU_PROGRAM_ERR_INVALID_ARG;
    }

    kline_proto_t *kline = proto_kline_handle(ep->cfg.proto);
    if (kline == NULL) {
        return ECU_PROGRAM_ERR_NOT_SUPPORTED;
    }

    kline_proto_status_t pst = kline_proto_uds_seed(kline, level,
            seed, 16 /* seed_buf_size */, out_len, timeout_ms);
    return ecu_program_from_kline(pst);
}

ecu_program_status_t ecu_program_send_key(ecu_program_t *ep, uint8_t level,
                                          const uint8_t *key, size_t key_len,
                                          uint32_t timeout_ms)
{
    if (ep == NULL) return ECU_PROGRAM_ERR_INVALID_ARG;
    if (!ep->initialized) return ECU_PROGRAM_ERR_NOT_INITIALIZED;
    if (key == NULL || key_len == 0 || timeout_ms == 0) {
        return ECU_PROGRAM_ERR_INVALID_ARG;
    }

    kline_proto_t *kline = proto_kline_handle(ep->cfg.proto);
    if (kline == NULL) {
        return ECU_PROGRAM_ERR_NOT_SUPPORTED;
    }

    kline_proto_status_t pst = kline_proto_uds_key(kline, level,
            key, key_len, timeout_ms);
    return ecu_program_from_kline(pst);
}

ecu_program_status_t ecu_program_download(ecu_program_t *ep, const uint8_t *data,
                                          size_t len, uint32_t timeout_ms)
{
    if (ep == NULL) return ECU_PROGRAM_ERR_INVALID_ARG;
    if (!ep->initialized) return ECU_PROGRAM_ERR_NOT_INITIALIZED;
    if (data == NULL || len == 0 || timeout_ms == 0) {
        return ECU_PROGRAM_ERR_INVALID_ARG;
    }

    /* Safety: warning screen before downloading to ECU memory. */
    if (!ecu_program_show_warning(ep)) {
        return ECU_PROGRAM_ERR_USER_ABORT;
    }

    kline_proto_t *kline = proto_kline_handle(ep->cfg.proto);
    if (kline == NULL) {
        return ECU_PROGRAM_ERR_NOT_SUPPORTED;
    }

    /* Send data in ECU_PROGRAM_DOWNLOAD_CHUNK_MAX-sized chunks via
     * UDS $36 TransferData: [block_sequence_counter][data chunk].
     * Sequence counter starts at 1 and wraps at 255. */
    uint8_t  seq = 1U;
    size_t   offset = 0;
    uint8_t  resp[16];
    size_t   resp_len = 0;

    while (offset < len) {
        size_t chunk_len = len - offset;
        if (chunk_len > ECU_PROGRAM_DOWNLOAD_CHUNK_MAX - 1U) {
            chunk_len = ECU_PROGRAM_DOWNLOAD_CHUNK_MAX - 1U;
        }

        uint8_t chunk[1 + ECU_PROGRAM_DOWNLOAD_CHUNK_MAX - 1U];
        chunk[0] = seq;
        memcpy(chunk + 1, data + offset, chunk_len);

        kline_proto_status_t pst = kline_proto_send(kline,
                ECU_PROGRAM_SID_TRANSFER_DATA, chunk, chunk_len + 1U,
                resp, sizeof(resp), &resp_len, timeout_ms);
        if (pst != KLINE_PROTO_OK) {
            return ecu_program_from_kline(pst);
        }

        offset += chunk_len;
        seq = (uint8_t)((seq % 255U) + 1U);
    }

    return ECU_PROGRAM_OK;
}
