/*
 * app/live_data.c — OBD2 live-data (mode $01 PID) application layer.
 *
 * Reads live-data PIDs over the unified protocol (CAN/J1979) and formats
 * raw response bytes into human-readable strings for the UI layer:
 * engine RPM, vehicle speed, coolant/intake air temperature, calculated
 * engine load, MAF airflow, and throttle position.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "live_data.h"

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */

struct live_data {
    protocol_t *proto;
    bool        initialized;
};

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

/* Map a proto_status_t into a live_data_status_t. */
static live_data_status_t live_data_from_proto(proto_status_t st)
{
    switch (st) {
        case PROTO_OK:                    return LIVE_DATA_OK;
        case PROTO_ERR_INVALID_ARG:       return LIVE_DATA_ERR_INVALID_ARG;
        case PROTO_ERR_NOT_INITIALIZED:   return LIVE_DATA_ERR_NOT_INITIALIZED;
        case PROTO_ERR_BUFFER_TOO_SMALL:  return LIVE_DATA_ERR_BUFFER_TOO_SMALL;
        case PROTO_ERR_TIMEOUT:
        case PROTO_ERR_PROTOCOL:
        case PROTO_ERR_NEGATIVE_RESPONSE:
        default:                          return LIVE_DATA_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * PID formatting
 * ------------------------------------------------------------------------ */

/*
 * Format raw PID response bytes into a human-readable string.
 *
 * Supported PIDs (mode $01):
 *   0x04 engine load (A * 100 / 255 %)
 *   0x05 coolant temp (A - 40 C)
 *   0x0C engine RPM ((A*256 + B) / 4 rpm)
 *   0x0D vehicle speed (A km/h)
 *   0x0F intake air temp (A - 40 C)
 *   0x10 MAF airflow ((A*256 + B) / 100 g/s)
 *   0x11 throttle position (A * 100 / 255 %)
 *
 * Unknown PID -> "PID 0x?? not supported".
 * out_buf is always NUL-terminated on success.
 */
live_data_status_t live_data_format_pid(uint8_t pid, const uint8_t *data,
                                        size_t len, char *out_buf,
                                        size_t out_buf_len)
{
    int n;

    if (out_buf == NULL || out_buf_len == 0) {
        return LIVE_DATA_ERR_INVALID_ARG;
    }
    if (len > 0 && data == NULL) {
        return LIVE_DATA_ERR_INVALID_ARG;
    }

    switch (pid) {
        case J1979_PID_ENGINE_RPM: {    /* 0x0C: ((A*256)+B) / 4 */
            if (len < 2) return LIVE_DATA_ERR_PROTOCOL;
            float rpm = j1979_pid_rpm(data, len);
            n = snprintf(out_buf, out_buf_len, "RPM: %.0f rpm", rpm);
            break;
        }
        case J1979_PID_VEHICLE_SPEED: { /* 0x0D: A */
            if (len < 1) return LIVE_DATA_ERR_PROTOCOL;
            float speed = j1979_pid_speed(data, len);
            n = snprintf(out_buf, out_buf_len, "Speed: %.0f km/h", speed);
            break;
        }
        case J1979_PID_COOLANT_TEMP: {  /* 0x05: A - 40 */
            if (len < 1) return LIVE_DATA_ERR_PROTOCOL;
            float temp = j1979_pid_coolant_temp(data, len);
            n = snprintf(out_buf, out_buf_len, "Coolant: %.1f C", temp);
            break;
        }
        case J1979_PID_INTAKE_TEMP: {   /* 0x0F: A - 40 */
            if (len < 1) return LIVE_DATA_ERR_PROTOCOL;
            float temp = j1979_pid_intake_temp(data, len);
            n = snprintf(out_buf, out_buf_len, "Intake: %.1f C", temp);
            break;
        }
        case J1979_PID_ENGINE_LOAD: {   /* 0x04: A * 100 / 255 */
            if (len < 1) return LIVE_DATA_ERR_PROTOCOL;
            float load = j1979_pid_engine_load(data, len);
            n = snprintf(out_buf, out_buf_len, "Load: %.0f%%", load);
            break;
        }
        case J1979_PID_MAF: {           /* 0x10: ((A*256)+B) / 100 */
            if (len < 2) return LIVE_DATA_ERR_PROTOCOL;
            float maf = j1979_pid_maf(data, len);
            n = snprintf(out_buf, out_buf_len, "MAF: %.1f g/s", maf);
            break;
        }
        case J1979_PID_THROTTLE_POS: {  /* 0x11: A * 100 / 255 */
            if (len < 1) return LIVE_DATA_ERR_PROTOCOL;
            float throttle = j1979_pid_throttle(data, len);
            n = snprintf(out_buf, out_buf_len, "Throttle: %.0f%%", throttle);
            break;
        }
        default:
            n = snprintf(out_buf, out_buf_len,
                         "PID 0x%02X not supported", pid);
            break;
    }

    if (n < 0 || (size_t)n >= out_buf_len) {
        return LIVE_DATA_ERR_BUFFER_TOO_SMALL;
    }
    return LIVE_DATA_OK;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

live_data_status_t live_data_init(const live_data_config_t *cfg,
                                  live_data_t **out)
{
    if (cfg == NULL || out == NULL) {
        return LIVE_DATA_ERR_INVALID_ARG;
    }
    if (cfg->proto == NULL) {
        return LIVE_DATA_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return LIVE_DATA_ERR_NOT_INITIALIZED;
    }

    live_data_t *ld = (live_data_t *)calloc(1, sizeof(*ld));
    if (ld == NULL) {
        return LIVE_DATA_ERR_INVALID_ARG;
    }

    ld->proto = cfg->proto;
    ld->initialized = true;
    *out = ld;
    return LIVE_DATA_OK;
}

live_data_status_t live_data_deinit(live_data_t *ld)
{
    if (ld == NULL) {
        return LIVE_DATA_ERR_INVALID_ARG;
    }
    free(ld);
    return LIVE_DATA_OK;
}

bool live_data_is_initialized(const live_data_t *ld)
{
    return ld != NULL && ld->initialized;
}

live_data_status_t live_data_read_pid(live_data_t *ld, uint8_t pid,
                                      char *out_buf, size_t out_buf_len,
                                      uint32_t timeout_ms)
{
    if (ld == NULL || out_buf == NULL || out_buf_len == 0) {
        return LIVE_DATA_ERR_INVALID_ARG;
    }
    if (!ld->initialized) {
        return LIVE_DATA_ERR_NOT_INITIALIZED;
    }
    if (ld->proto == NULL || !proto_is_initialized(ld->proto)) {
        return LIVE_DATA_ERR_NOT_INITIALIZED;
    }

    uint8_t data[64];
    size_t  data_len = 0;

    proto_status_t st = proto_read_pid(ld->proto, pid, data, sizeof(data),
                                       &data_len, timeout_ms);
    if (st != PROTO_OK) {
        return live_data_from_proto(st);
    }

    return live_data_format_pid(pid, data, data_len, out_buf, out_buf_len);
}
