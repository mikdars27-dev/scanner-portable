/*
 * app/signal_output.c — Signal output / sensor simulation implementation.
 *
 * Simulates sensor signals to the ECU for testing. Uses UDS $2F
 * InputOutputControlByIdentifier on K-Line (KWP2000/UDS) or J1979 Mode $08
 * on CAN (ISO 15765-2).
 *
 * Safety: WARN_ACTUATOR_TEST warning screen before any signal is commanded;
 * SELECT required; forced 30 s timeout.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "signal_output.h"

#include <Arduino.h>
#include <string.h>

struct signal_output {
    signal_output_config_t cfg;
    bool                    initialized;
};

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

#define SIGNAL_OUTPUT_CMD_TIMEOUT_MS 2000UL  /* per-command protocol timeout */

/* J1979 Mode $08 control state (first data byte). */
#define SIGNAL_OUTPUT_J1979_CTRL_START 0x01U
#define SIGNAL_OUTPUT_J1979_CTRL_STOP  0x00U

/* UDS $2F InputOutputControl operations. */
#define SIGNAL_OUTPUT_UDS_OP_RETURN_TO_ECU 0x01U

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static signal_output_status_t signal_output_from_kline(kline_proto_status_t st)
{
    switch (st) {
        case KLINE_PROTO_OK:                    return SIGNAL_OUTPUT_OK;
        case KLINE_PROTO_ERR_INVALID_ARG:       return SIGNAL_OUTPUT_ERR_INVALID_ARG;
        case KLINE_PROTO_ERR_NOT_INITIALIZED:   return SIGNAL_OUTPUT_ERR_NOT_INITIALIZED;
        case KLINE_PROTO_ERR_TIMEOUT:           return SIGNAL_OUTPUT_ERR_TIMEOUT;
        case KLINE_PROTO_ERR_BUFFER_TOO_SMALL:  return SIGNAL_OUTPUT_ERR_PROTOCOL;
        case KLINE_PROTO_ERR_NEGATIVE_RESPONSE: return SIGNAL_OUTPUT_ERR_PROTOCOL;
        default:                                return SIGNAL_OUTPUT_ERR_PROTOCOL;
    }
}

static signal_output_status_t signal_output_from_j1979(j1979_status_t st)
{
    switch (st) {
        case J1979_OK:                    return SIGNAL_OUTPUT_OK;
        case J1979_ERR_INVALID_ARG:       return SIGNAL_OUTPUT_ERR_INVALID_ARG;
        case J1979_ERR_NOT_INITIALIZED:   return SIGNAL_OUTPUT_ERR_NOT_INITIALIZED;
        case J1979_ERR_TIMEOUT:           return SIGNAL_OUTPUT_ERR_TIMEOUT;
        case J1979_ERR_BUFFER_TOO_SMALL:  return SIGNAL_OUTPUT_ERR_PROTOCOL;
        case J1979_ERR_NEGATIVE_RESPONSE: return SIGNAL_OUTPUT_ERR_PROTOCOL;
        default:                          return SIGNAL_OUTPUT_ERR_PROTOCOL;
    }
}

/* Show the actuator/signal warning screen. Returns true if the user selected
 * (proceed), false if the user aborted. */
static bool signal_output_show_warning(signal_output_t *so)
{
    return warning_show(so->cfg.disp, WARN_ACTUATOR_TEST, NULL,
                        ACTUATOR_TEST_MAX_MS);
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

signal_output_status_t signal_output_init(const signal_output_config_t *cfg,
                                          signal_output_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL || cfg->disp == NULL) {
        return SIGNAL_OUTPUT_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return SIGNAL_OUTPUT_ERR_INVALID_ARG;
    }

    proto_transport_t tr = proto_get_transport(cfg->proto);
    if (tr != PROTO_TRANSPORT_CAN && tr != PROTO_TRANSPORT_KLINE) {
        return SIGNAL_OUTPUT_ERR_NOT_SUPPORTED;
    }

    signal_output_t *so = (signal_output_t *)calloc(1, sizeof(*so));
    if (so == NULL) {
        return SIGNAL_OUTPUT_ERR_INVALID_ARG;
    }

    so->cfg         = *cfg;
    so->initialized = true;

    *out = so;
    return SIGNAL_OUTPUT_OK;
}

signal_output_status_t signal_output_deinit(signal_output_t *so)
{
    if (so == NULL) {
        return SIGNAL_OUTPUT_ERR_INVALID_ARG;
    }
    so->initialized = false;
    free(so);
    return SIGNAL_OUTPUT_OK;
}

bool signal_output_is_initialized(const signal_output_t *so)
{
    return so != NULL && so->initialized;
}

/* ------------------------------------------------------------------------
 * Command helpers
 * ------------------------------------------------------------------------ */

/* Send start/stop control command over CAN via J1979 Mode $08. */
static signal_output_status_t signal_output_cmd_can(signal_output_t *so,
                                                    uint8_t pid,
                                                    bool start)
{
    j1979_t *jh = proto_j1979_handle(so->cfg.proto);
    if (jh == NULL) {
        return SIGNAL_OUTPUT_ERR_PROTOCOL;
    }

    uint8_t ctrl = start ? SIGNAL_OUTPUT_J1979_CTRL_START
                         : SIGNAL_OUTPUT_J1979_CTRL_STOP;
    uint8_t resp[8];
    size_t  resp_len = 0;

    j1979_status_t st = j1979_send(jh, J1979_MODE_CONTROL, pid,
                                   &ctrl, 1, resp, sizeof(resp),
                                   &resp_len, SIGNAL_OUTPUT_CMD_TIMEOUT_MS);
    return signal_output_from_j1979(st);
}

/* Send IOControl command over K-Line via UDS $2F. */
static signal_output_status_t signal_output_cmd_kline(signal_output_t *so,
                                                      uint16_t did,
                                                      uint8_t control_op,
                                                      const uint8_t *params,
                                                      size_t params_len)
{
    kline_proto_t *kh = proto_kline_handle(so->cfg.proto);
    if (kh == NULL) {
        return SIGNAL_OUTPUT_ERR_PROTOCOL;
    }

    uint8_t resp[8];
    size_t  resp_len = 0;

    kline_proto_status_t pst = kline_proto_uds_io_control(kh, did, control_op,
                                                          params, params_len,
                                                          resp, sizeof(resp),
                                                          &resp_len,
                                                          SIGNAL_OUTPUT_CMD_TIMEOUT_MS);
    return signal_output_from_kline(pst);
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

signal_output_status_t signal_output_set(signal_output_t *so, uint16_t did,
                                         uint8_t control_op,
                                         const uint8_t *params, size_t params_len,
                                         uint32_t timeout_ms)
{
    (void)timeout_ms; /* per-command timeout is fixed internally */

    if (so == NULL) return SIGNAL_OUTPUT_ERR_INVALID_ARG;
    if (!so->initialized) return SIGNAL_OUTPUT_ERR_NOT_INITIALIZED;
    if (params_len > 0 && params == NULL) return SIGNAL_OUTPUT_ERR_INVALID_ARG;

    /* Safety: show warning screen and require SELECT before commanding
     * any signal output. */
    if (!signal_output_show_warning(so)) {
        return SIGNAL_OUTPUT_ERR_USER_ABORT;
    }

    proto_transport_t tr = proto_get_transport(so->cfg.proto);
    if (tr == PROTO_TRANSPORT_CAN) {
        /* J1979 Mode $08: did is the PID, params[0] is the control state. */
        uint8_t ctrl = (params_len > 0) ? params[0]
                                        : SIGNAL_OUTPUT_J1979_CTRL_START;
        return signal_output_cmd_can(so, (uint8_t)did, ctrl != 0x00U);
    }
    if (tr == PROTO_TRANSPORT_KLINE) {
        /* UDS $2F InputOutputControlByIdentifier. */
        uint8_t op = control_op;
        if (op == 0x00U) {
            op = SIGNAL_OUTPUT_UDS_OP_START; /* default to start */
        }
        return signal_output_cmd_kline(so, did, op, params, params_len);
    }
    return SIGNAL_OUTPUT_ERR_NOT_SUPPORTED;
}

signal_output_status_t signal_output_release(signal_output_t *so, uint16_t did,
                                              uint32_t timeout_ms)
{
    (void)timeout_ms; /* per-command timeout is fixed internally */

    if (so == NULL) return SIGNAL_OUTPUT_ERR_INVALID_ARG;
    if (!so->initialized) return SIGNAL_OUTPUT_ERR_NOT_INITIALIZED;

    proto_transport_t tr = proto_get_transport(so->cfg.proto);
    if (tr == PROTO_TRANSPORT_CAN) {
        /* J1979 Mode $08: stop control (return to ECU). */
        return signal_output_cmd_can(so, (uint8_t)did, false);
    }
    if (tr == PROTO_TRANSPORT_KLINE) {
        /* UDS $2F: return control to ECU. */
        return signal_output_cmd_kline(so, did,
                                       SIGNAL_OUTPUT_UDS_OP_RETURN_TO_ECU,
                                       NULL, 0);
    }
    return SIGNAL_OUTPUT_ERR_NOT_SUPPORTED;
}
