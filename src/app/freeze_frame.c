/*
 * app/freeze_frame.c — Freeze Frame data implementation.
 *
 * Reads OBD2 freeze frame data via J1979 Mode $02.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "freeze_frame.h"

#include <Arduino.h>
#include <string.h>

#include "protocol/j1979.h"

/* J1979 Mode $02 PID: DTC that caused the freeze frame */
#define FREEZE_FRAME_PID_TRIGGER_DTC 0x02U

struct freeze_frame {
    protocol_t *proto;
    bool        initialized;
};

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */
static freeze_frame_status_t freeze_frame_from_j1979(j1979_status_t st)
{
    switch (st) {
        case J1979_OK:                    return FREEZE_FRAME_OK;
        case J1979_ERR_INVALID_ARG:       return FREEZE_FRAME_ERR_INVALID_ARG;
        case J1979_ERR_NOT_INITIALIZED:   return FREEZE_FRAME_ERR_NOT_INITIALIZED;
        case J1979_ERR_TIMEOUT:           return FREEZE_FRAME_ERR_TIMEOUT;
        case J1979_ERR_BUFFER_TOO_SMALL:  return FREEZE_FRAME_ERR_BUFFER_TOO_SMALL;
        case J1979_ERR_NEGATIVE_RESPONSE: return FREEZE_FRAME_ERR_PROTOCOL;
        default:                          return FREEZE_FRAME_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
freeze_frame_status_t freeze_frame_init(const freeze_frame_config_t *cfg,
                                        freeze_frame_t **out)
{
    if (cfg == NULL || out == NULL) {
        return FREEZE_FRAME_ERR_INVALID_ARG;
    }
    if (cfg->proto == NULL || !proto_is_initialized(cfg->proto)) {
        return FREEZE_FRAME_ERR_INVALID_ARG;
    }
    if (proto_get_transport(cfg->proto) != PROTO_TRANSPORT_CAN) {
        /* Freeze frame (Mode $02) is only available over CAN/J1979. */
        return FREEZE_FRAME_ERR_INVALID_ARG;
    }

    freeze_frame_t *ff = (freeze_frame_t *)calloc(1, sizeof(*ff));
    if (ff == NULL) {
        return FREEZE_FRAME_ERR_INVALID_ARG;
    }

    ff->proto       = cfg->proto;
    ff->initialized = true;

    *out = ff;
    return FREEZE_FRAME_OK;
}

freeze_frame_status_t freeze_frame_deinit(freeze_frame_t *ff)
{
    if (ff == NULL) {
        return FREEZE_FRAME_ERR_INVALID_ARG;
    }
    ff->initialized = false;
    free(ff);
    return FREEZE_FRAME_OK;
}

bool freeze_frame_is_initialized(const freeze_frame_t *ff)
{
    return ff != NULL && ff->initialized;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */
freeze_frame_status_t freeze_frame_read_trigger_dtc(freeze_frame_t *ff,
                                                     char *buf, size_t len,
                                                     uint32_t timeout_ms)
{
    if (ff == NULL || !ff->initialized) {
        return FREEZE_FRAME_ERR_NOT_INITIALIZED;
    }
    if (buf == NULL || len == 0) {
        return FREEZE_FRAME_ERR_INVALID_ARG;
    }

    j1979_t *j1979 = proto_j1979_handle(ff->proto);
    if (j1979 == NULL) {
        return FREEZE_FRAME_ERR_NOT_SUPPORTED;
    }

    uint8_t  data[16];
    size_t   data_len = 0;
    j1979_status_t st = j1979_read_pid(j1979, FREEZE_FRAME_PID_TRIGGER_DTC,
                                       data, sizeof(data), &data_len, timeout_ms);
    if (st != J1979_OK) {
        return freeze_frame_from_j1979(st);
    }

    /* Mode $02 PID $02 response: 2 raw DTC bytes */
    if (data_len < 2) {
        return FREEZE_FRAME_ERR_PROTOCOL;
    }

    char code[6];
    dtc_status_t dst = dtc_decode(data, code, sizeof(code), NULL, 0);
    if (dst != DTC_OK) {
        return FREEZE_FRAME_ERR_PROTOCOL;
    }

    size_t code_len = strlen(code);
    if (code_len >= len) {
        code_len = len - 1;
    }
    memcpy(buf, code, code_len);
    buf[code_len] = '\0';
    return FREEZE_FRAME_OK;
}

freeze_frame_status_t freeze_frame_read_pid(freeze_frame_t *ff, uint8_t pid,
                                             char *buf, size_t len,
                                             uint32_t timeout_ms)
{
    if (ff == NULL || !ff->initialized) {
        return FREEZE_FRAME_ERR_NOT_INITIALIZED;
    }
    if (buf == NULL || len == 0) {
        return FREEZE_FRAME_ERR_INVALID_ARG;
    }

    j1979_t *j1979 = proto_j1979_handle(ff->proto);
    if (j1979 == NULL) {
        return FREEZE_FRAME_ERR_NOT_SUPPORTED;
    }

    uint8_t  data[16];
    size_t   data_len = 0;
    j1979_status_t st = j1979_read_pid(j1979, pid,
                                       data, sizeof(data), &data_len, timeout_ms);
    if (st != J1979_OK) {
        return freeze_frame_from_j1979(st);
    }

    /* Format using the same conversion helpers as live_data */
    float value = 0.0f;
    const char *unit = "";
    const char *name = "PID";

    switch (pid) {
        case J1979_PID_ENGINE_RPM:
            value = j1979_pid_rpm(data, data_len);
            unit  = "rpm";
            name  = "RPM";
            break;
        case J1979_PID_VEHICLE_SPEED:
            value = j1979_pid_speed(data, data_len);
            unit  = "km/h";
            name  = "Speed";
            break;
        case J1979_PID_COOLANT_TEMP:
            value = j1979_pid_coolant_temp(data, data_len);
            unit  = "C";
            name  = "Coolant";
            break;
        case J1979_PID_INTAKE_TEMP:
            value = j1979_pid_intake_temp(data, data_len);
            unit  = "C";
            name  = "Intake";
            break;
        case J1979_PID_ENGINE_LOAD:
            value = j1979_pid_engine_load(data, data_len);
            unit  = "%";
            name  = "Load";
            break;
        case J1979_PID_MAF:
            value = j1979_pid_maf(data, data_len);
            unit  = "g/s";
            name  = "MAF";
            break;
        case J1979_PID_THROTTLE_POS:
            value = j1979_pid_throttle(data, data_len);
            unit  = "%";
            name  = "Throttle";
            break;
        default:
            /* Unknown PID: show raw hex */
            snprintf(buf, len, "PID 0x%02X raw", pid);
            return FREEZE_FRAME_OK;
    }

    if (pid == J1979_PID_ENGINE_RPM || pid == J1979_PID_VEHICLE_SPEED ||
        pid == J1979_PID_COOLANT_TEMP || pid == J1979_PID_INTAKE_TEMP ||
        pid == J1979_PID_MAF)
    {
        snprintf(buf, len, "%s: %.1f %s", name, value, unit);
    } else {
        snprintf(buf, len, "%s: %.0f %s", name, value, unit);
    }
    return FREEZE_FRAME_OK;
}
