/*
 * app/abs_module.c — ABS module diagnostics implementation.
 *
 * Reads and clears ABS trouble codes and tests ABS hydraulic/electronic brake
 * components via proto_send() with manufacturer-specific ABS service IDs.
 *
 * Safety: WARN_CUSTOM warning screen before component testing (brake safety).
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "abs_module.h"

#include <Arduino.h>
#include <string.h>

struct abs {
    abs_config_t cfg;
    bool         initialized;
};

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

#define ABS_CMD_TIMEOUT_MS    2000UL   /* per-command protocol timeout */
#define ABS_TEST_POLL_MS      100UL    /* STOP button poll interval */
#define ABS_TEST_TICK_MS       20UL    /* main loop tick */

/* UDS ClearDiagnosticInformation DTC mask: all DTCs. */
#define ABS_DTC_MASK_ALL 0xFFFFFFUL

/* ABS component test control operations. */
#define ABS_COMP_OP_START 0x01U
#define ABS_COMP_OP_STOP  0x00U

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static abs_status_t abs_from_proto(proto_status_t st)
{
    switch (st) {
        case PROTO_OK:                    return ABS_OK;
        case PROTO_ERR_INVALID_ARG:       return ABS_ERR_INVALID_ARG;
        case PROTO_ERR_NOT_INITIALIZED:   return ABS_ERR_NOT_INITIALIZED;
        case PROTO_ERR_TIMEOUT:           return ABS_ERR_TIMEOUT;
        case PROTO_ERR_BUFFER_TOO_SMALL:  return ABS_ERR_PROTOCOL;
        case PROTO_ERR_NEGATIVE_RESPONSE: return ABS_ERR_PROTOCOL;
        default:                          return ABS_ERR_PROTOCOL;
    }
}

/* Show the ABS component test warning screen. Returns true if the user
 * selected (proceed), false if the user aborted. */
static bool abs_show_warning(abs_t *abs)
{
    return warning_show(abs->cfg.disp, WARN_CUSTOM,
                        "Uji komponen ABS - risiko rem!",
                        ACTUATOR_TEST_MAX_MS);
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

abs_status_t abs_init(const abs_config_t *cfg, abs_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL || cfg->disp == NULL) {
        return ABS_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return ABS_ERR_INVALID_ARG;
    }

    abs_t *ab = (abs_t *)calloc(1, sizeof(*ab));
    if (ab == NULL) {
        return ABS_ERR_INVALID_ARG;
    }

    ab->cfg         = *cfg;
    ab->initialized = true;

    *out = ab;
    return ABS_OK;
}

abs_status_t abs_deinit(abs_t *ab)
{
    if (ab == NULL) {
        return ABS_ERR_INVALID_ARG;
    }
    ab->initialized = false;
    free(ab);
    return ABS_OK;
}

bool abs_is_initialized(const abs_t *ab)
{
    return ab != NULL && ab->initialized;
}

/* ------------------------------------------------------------------------
 * ABS diagnostics
 * ------------------------------------------------------------------------ */

abs_status_t abs_read_dtcs(abs_t *ab, dtc_list_t *list, uint32_t timeout_ms)
{
    if (ab == NULL) return ABS_ERR_INVALID_ARG;
    if (!ab->initialized) return ABS_ERR_NOT_INITIALIZED;
    if (list == NULL || list->entries == NULL || list->capacity == 0 ||
        timeout_ms == 0)
    {
        return ABS_ERR_INVALID_ARG;
    }

    /* UDS $19 ReadDTCInformation, subfunction 0x01 = reportDTCsByStatusMask.
     * Raw DTC bytes are 2 bytes per DTC. */
    uint8_t req[1] = { 0x01U };
    uint8_t resp[256];
    size_t  resp_len = 0;

    proto_status_t pst = proto_send(ab->cfg.proto, ab->cfg.abs_sid_read_dtcs,
                                    req, sizeof(req),
                                    resp, sizeof(resp), &resp_len, timeout_ms);
    if (pst != PROTO_OK) {
        return abs_from_proto(pst);
    }

    /* Decode raw 2-byte DTCs into the caller-provided dtc_list_t. */
    dtc_status_t dst = dtc_decode_list(resp, resp_len, list);
    if (dst != DTC_OK) {
        return ABS_ERR_PROTOCOL;
    }

    return ABS_OK;
}

abs_status_t abs_clear_dtcs(abs_t *ab, uint32_t timeout_ms)
{
    if (ab == NULL) return ABS_ERR_INVALID_ARG;
    if (!ab->initialized) return ABS_ERR_NOT_INITIALIZED;
    if (timeout_ms == 0) return ABS_ERR_INVALID_ARG;

    /* UDS $14 ClearDiagnosticInformation, DTC mask = all (0xFFFFFF), 3 bytes
     * little-endian. */
    uint8_t req[3];
    req[0] = (uint8_t)(ABS_DTC_MASK_ALL & 0xFFUL);
    req[1] = (uint8_t)((ABS_DTC_MASK_ALL >> 8) & 0xFFUL);
    req[2] = (uint8_t)((ABS_DTC_MASK_ALL >> 16) & 0xFFUL);

    uint8_t resp[8];
    size_t  resp_len = 0;

    proto_status_t pst = proto_send(ab->cfg.proto, ab->cfg.abs_sid_clear_dtcs,
                                    req, sizeof(req),
                                    resp, sizeof(resp), &resp_len, timeout_ms);
    return abs_from_proto(pst);
}

abs_status_t abs_test_component(abs_t *ab, uint8_t component_id,
                                 uint32_t timeout_ms)
{
    if (ab == NULL) return ABS_ERR_INVALID_ARG;
    if (!ab->initialized) return ABS_ERR_NOT_INITIALIZED;
    if (component_id == 0x00U || timeout_ms == 0) {
        return ABS_ERR_INVALID_ARG;
    }

    /* Safety: warning screen before testing brake components. */
    if (!abs_show_warning(ab)) {
        return ABS_ERR_USER_ABORT;
    }

    /* Manufacturer-specific component test: [component_id, 0x01=start]. */
    uint8_t req_start[2] = { component_id, ABS_COMP_OP_START };
    uint8_t req_stop[2]  = { component_id, ABS_COMP_OP_STOP };
    uint8_t resp[8];
    size_t  resp_len = 0;
    proto_status_t pst;

    pst = proto_send(ab->cfg.proto, ab->cfg.abs_sid_test_component,
                     req_start, sizeof(req_start),
                     resp, sizeof(resp), &resp_len, timeout_ms);
    if (pst != PROTO_OK) {
        return abs_from_proto(pst);
    }

    /* Run for timeout_ms (capped at ACTUATOR_TEST_MAX_MS), polling the STOP
     * button (BACK) every 100 ms. millis() wraparound is safe with unsigned
     * subtraction as long as intervals stay below ~49 days. */
    uint32_t limit = timeout_ms < ACTUATOR_TEST_MAX_MS
                     ? timeout_ms : ACTUATOR_TEST_MAX_MS;
    uint32_t start_ms = millis();
    uint32_t next_poll = start_ms;
    bool     stopped = false;

    for (;;) {
        uint32_t now = millis();
        if ((uint32_t)(now - start_ms) >= limit) {
            break;
        }

        if ((int32_t)(now - next_poll) >= 0) {
            next_poll = now + ABS_TEST_POLL_MS;
            if ((buttons_read() & BTN_BACK) != 0) {
                stopped = true;
                break;
            }
        }

        delay(ABS_TEST_TICK_MS);
        yield();
    }

    /* Command the ECU to return control of the component (best effort). */
    (void)proto_send(ab->cfg.proto, ab->cfg.abs_sid_test_component,
                     req_stop, sizeof(req_stop),
                     resp, sizeof(resp), &resp_len, timeout_ms);
    buttons_wait_release();

    if (stopped) {
        return ABS_OK;
    }

    /* Forced cap reached before the requested duration. */
    if (timeout_ms > ACTUATOR_TEST_MAX_MS &&
        (uint32_t)(millis() - start_ms) >= ACTUATOR_TEST_MAX_MS)
    {
        return ABS_ERR_TIMEOUT;
    }

    return ABS_OK;
}
