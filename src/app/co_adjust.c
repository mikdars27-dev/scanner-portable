/*
 * app/co_adjust.c — CO / idle mixture adjustment implementation.
 *
 * Adjusts the fuel mixture (rich/lean) at idle for early fuel-injected
 * motorcycles. Uses UDS $2F IOControl on K-Line or J1979 Mode $08 on CAN.
 *
 * Safety: WARN_CUSTOM warning screen before any mixture change; SELECT
 * required.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "co_adjust.h"

#include <Arduino.h>
#include <string.h>

struct co_adjust {
    co_adjust_config_t cfg;
    bool                initialized;
};

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

#define CO_ADJUST_CMD_TIMEOUT_MS 2000UL   /* per-command protocol timeout */

/* UDS $2F InputOutputControl operation: short term adjustment. */
#define CO_ADJUST_UDS_OP_SHORT_TERM_ADJUST 0x02U

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static co_adjust_status_t co_adjust_from_kline(kline_proto_status_t st)
{
    switch (st) {
        case KLINE_PROTO_OK:                    return CO_ADJUST_OK;
        case KLINE_PROTO_ERR_INVALID_ARG:       return CO_ADJUST_ERR_INVALID_ARG;
        case KLINE_PROTO_ERR_NOT_INITIALIZED:   return CO_ADJUST_ERR_NOT_INITIALIZED;
        case KLINE_PROTO_ERR_TIMEOUT:           return CO_ADJUST_ERR_TIMEOUT;
        case KLINE_PROTO_ERR_BUFFER_TOO_SMALL:  return CO_ADJUST_ERR_PROTOCOL;
        case KLINE_PROTO_ERR_NEGATIVE_RESPONSE: return CO_ADJUST_ERR_PROTOCOL;
        default:                                return CO_ADJUST_ERR_PROTOCOL;
    }
}

static co_adjust_status_t co_adjust_from_j1979(j1979_status_t st)
{
    switch (st) {
        case J1979_OK:                    return CO_ADJUST_OK;
        case J1979_ERR_INVALID_ARG:       return CO_ADJUST_ERR_INVALID_ARG;
        case J1979_ERR_NOT_INITIALIZED:   return CO_ADJUST_ERR_NOT_INITIALIZED;
        case J1979_ERR_TIMEOUT:           return CO_ADJUST_ERR_TIMEOUT;
        case J1979_ERR_BUFFER_TOO_SMALL:  return CO_ADJUST_ERR_PROTOCOL;
        case J1979_ERR_NEGATIVE_RESPONSE: return CO_ADJUST_ERR_PROTOCOL;
        default:                          return CO_ADJUST_ERR_PROTOCOL;
    }
}

/* Show the CO adjust warning screen. Returns true if the user selected
 * (proceed), false if the user aborted. */
static bool co_adjust_show_warning(co_adjust_t *ca)
{
    return warning_show(ca->cfg.disp, WARN_CUSTOM,
                        "Penyesuaian CO idle - risiko mesin!",
                        ACTUATOR_TEST_MAX_MS);
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

co_adjust_status_t co_adjust_init(const co_adjust_config_t *cfg,
                                  co_adjust_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL || cfg->disp == NULL) {
        return CO_ADJUST_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return CO_ADJUST_ERR_INVALID_ARG;
    }

    proto_transport_t tr = proto_get_transport(cfg->proto);
    if (tr != PROTO_TRANSPORT_CAN && tr != PROTO_TRANSPORT_KLINE) {
        return CO_ADJUST_ERR_NOT_SUPPORTED;
    }

    co_adjust_t *ca = (co_adjust_t *)calloc(1, sizeof(*ca));
    if (ca == NULL) {
        return CO_ADJUST_ERR_INVALID_ARG;
    }

    ca->cfg         = *cfg;
    ca->initialized = true;

    *out = ca;
    return CO_ADJUST_OK;
}

co_adjust_status_t co_adjust_deinit(co_adjust_t *ca)
{
    if (ca == NULL) {
        return CO_ADJUST_ERR_INVALID_ARG;
    }
    ca->initialized = false;
    free(ca);
    return CO_ADJUST_OK;
}

bool co_adjust_is_initialized(const co_adjust_t *ca)
{
    return ca != NULL && ca->initialized;
}

/* ------------------------------------------------------------------------
 * Mixture adjustment
 * ------------------------------------------------------------------------ */

co_adjust_status_t co_adjust_set_mixture(co_adjust_t *ca, int8_t adjustment,
                                          uint32_t timeout_ms)
{
    (void)timeout_ms; /* per-command timeout is fixed internally */

    if (ca == NULL) return CO_ADJUST_ERR_INVALID_ARG;
    if (!ca->initialized) return CO_ADJUST_ERR_NOT_INITIALIZED;
    if (adjustment < CO_ADJUST_MIXTURE_MIN || adjustment > CO_ADJUST_MIXTURE_MAX) {
        return CO_ADJUST_ERR_INVALID_ARG;
    }

    /* Safety: warning screen before changing idle mixture. */
    if (!co_adjust_show_warning(ca)) {
        return CO_ADJUST_ERR_USER_ABORT;
    }

    /* Convert int8_t (-128..+127) to uint8_t (two's complement, same bit
     * pattern). */
    uint8_t adj_byte = (uint8_t)adjustment;

    proto_transport_t tr = proto_get_transport(ca->cfg.proto);
    if (tr == PROTO_TRANSPORT_CAN) {
        j1979_t *jh = proto_j1979_handle(ca->cfg.proto);
        if (jh == NULL) {
            return CO_ADJUST_ERR_PROTOCOL;
        }

        uint8_t resp[8];
        size_t  resp_len = 0;

        j1979_status_t st = j1979_send(jh, J1979_MODE_CONTROL,
                                       ca->cfg.j1979_pid,
                                       &adj_byte, 1,
                                       resp, sizeof(resp), &resp_len,
                                       CO_ADJUST_CMD_TIMEOUT_MS);
        return co_adjust_from_j1979(st);
    }
    if (tr == PROTO_TRANSPORT_KLINE) {
        kline_proto_t *kh = proto_kline_handle(ca->cfg.proto);
        if (kh == NULL) {
            return CO_ADJUST_ERR_PROTOCOL;
        }

        uint8_t resp[8];
        size_t  resp_len = 0;

        kline_proto_status_t pst = kline_proto_uds_io_control(
                kh, ca->cfg.uds_did, CO_ADJUST_UDS_OP_SHORT_TERM_ADJUST,
                &adj_byte, 1, resp, sizeof(resp), &resp_len,
                CO_ADJUST_CMD_TIMEOUT_MS);
        return co_adjust_from_kline(pst);
    }
    return CO_ADJUST_ERR_NOT_SUPPORTED;
}

co_adjust_status_t co_adjust_read_mixture(co_adjust_t *ca, int8_t *current,
                                           uint32_t timeout_ms)
{
    (void)timeout_ms; /* per-command timeout is fixed internally */

    if (ca == NULL) return CO_ADJUST_ERR_INVALID_ARG;
    if (!ca->initialized) return CO_ADJUST_ERR_NOT_INITIALIZED;
    if (current == NULL) return CO_ADJUST_ERR_INVALID_ARG;

    proto_transport_t tr = proto_get_transport(ca->cfg.proto);
    if (tr == PROTO_TRANSPORT_CAN) {
        j1979_t *jh = proto_j1979_handle(ca->cfg.proto);
        if (jh == NULL) {
            return CO_ADJUST_ERR_PROTOCOL;
        }

        uint8_t resp[8];
        size_t  resp_len = 0;

        /* Mode $08 with no data may not be supported by all ECUs; treat as
         * best-effort read of current control state. */
        j1979_status_t st = j1979_send(jh, J1979_MODE_CONTROL,
                                       ca->cfg.j1979_pid,
                                       NULL, 0,
                                       resp, sizeof(resp), &resp_len,
                                       CO_ADJUST_CMD_TIMEOUT_MS);
        if (st != J1979_OK) {
            return co_adjust_from_j1979(st);
        }
        if (resp_len < 1) {
            return CO_ADJUST_ERR_PROTOCOL;
        }
        *current = (int8_t)resp[0];
        return CO_ADJUST_OK;
    }
    if (tr == PROTO_TRANSPORT_KLINE) {
        kline_proto_t *kh = proto_kline_handle(ca->cfg.proto);
        if (kh == NULL) {
            return CO_ADJUST_ERR_PROTOCOL;
        }

        uint8_t resp[8];
        size_t  resp_len = 0;

        kline_proto_status_t pst = kline_proto_uds_read_did(
                kh, ca->cfg.uds_did, resp, sizeof(resp), &resp_len,
                CO_ADJUST_CMD_TIMEOUT_MS);
        if (pst != KLINE_PROTO_OK) {
            return co_adjust_from_kline(pst);
        }
        if (resp_len < 1) {
            return CO_ADJUST_ERR_PROTOCOL;
        }
        *current = (int8_t)resp[0];
        return CO_ADJUST_OK;
    }
    return CO_ADJUST_ERR_NOT_SUPPORTED;
}
