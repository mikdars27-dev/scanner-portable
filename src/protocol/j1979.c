/*
 * protocol/j1979.c — SAE J1979 OBD2 diagnostic modes $01–$0A implementation.
 *
 * Built on top of ISO 15765-2 CAN transport.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "protocol/j1979.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */
typedef struct {
    iso15765_t *transport;
    bool        initialized;
} j1979_t;

/* ------------------------------------------------------------------------
 * Request / response helpers
 * ------------------------------------------------------------------------ */

/* Build a J1979 request payload: [mode][pid][args...] */
static size_t build_request(uint8_t mode, uint8_t pid,
                            const uint8_t *args, size_t args_len,
                            uint8_t *out, size_t out_size)
{
    size_t needed = 2 + args_len;
    if (needed > out_size) return 0;
    out[0] = mode;
    out[1] = pid;
    if (args_len > 0 && args != NULL) {
        memcpy(&out[2], args, args_len);
    }
    return needed;
}

/* Parse a J1979 response. Returns 0 on success, -1 on error/NRC.
 * On success: resp points to payload data after SID+PID echo, *resp_len set.
 * On NRC: resp[0] = NRC code, *resp_len = 1. */
static int parse_response(const uint8_t *raw, size_t raw_len,
                          uint8_t expected_mode, uint8_t expected_pid,
                          const uint8_t **resp, size_t *resp_len)
{
    if (raw_len < 2) return -1;

    uint8_t resp_sid = raw[0];
    uint8_t resp_pid = raw[1];

    /* Negative response: 0x7F [mode] [NRC] */
    if (resp_sid == 0x7F) {
        if (raw_len < 3) return -1;
        /* caller handles NRC via resp[0] = NRC */
        *resp = &raw[2];
        *resp_len = 1;
        return -2;   /* special: negative response */
    }

    uint8_t expected_sid = (uint8_t)(expected_mode + 0x40);
    if (resp_sid != expected_sid) return -1;
    if (resp_pid != expected_pid) return -1;

    if (raw_len < 3) return -1;
    *resp = &raw[2];
    *resp_len = raw_len - 2;
    return 0;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

j1979_status_t j1979_init(const j1979_config_t *cfg, j1979_t **out_j1979)
{
    if (cfg == NULL || out_j1979 == NULL || cfg->transport == NULL) {
        return J1979_ERR_INVALID_ARG;
    }
    if (!iso15765_is_initialized(cfg->transport)) {
        return J1979_ERR_NOT_INITIALIZED;
    }

    j1979_t *j = (j1979_t *)calloc(1, sizeof(*j));
    if (j == NULL) {
        return J1979_ERR_INVALID_ARG;
    }

    j->transport = cfg->transport;
    j->initialized = true;

    *out_j1979 = j;
    return J1979_OK;
}

j1979_status_t j1979_deinit(j1979_t *j1979)
{
    if (j1979 == NULL) return J1979_ERR_INVALID_ARG;
    free(j1979);
    return J1979_OK;
}

bool j1979_is_initialized(const j1979_t *j1979)
{
    return j1979 != NULL && j1979->initialized;
}

j1979_status_t j1979_send(j1979_t *j1979, uint8_t mode, uint8_t pid,
                          const uint8_t *args, size_t args_len,
                          uint8_t *resp, size_t resp_buf_size,
                          size_t *out_len, uint32_t timeout_ms)
{
    if (j1979 == NULL || resp == NULL || out_len == NULL) {
        return J1979_ERR_INVALID_ARG;
    }
    if (!j1979->initialized) {
        return J1979_ERR_NOT_INITIALIZED;
    }

    /* Build request payload. */
    uint8_t req[8];
    size_t req_len = build_request(mode, pid, args, args_len, req, sizeof(req));
    if (req_len == 0) {
        return J1979_ERR_BUFFER_TOO_SMALL;
    }

    /* Send via ISO 15765-2 transport. */
    uint8_t raw[4096];
    size_t raw_len = 0;
    iso15765_status_t st = iso15765_send_request(j1979->transport,
                                                 req, req_len,
                                                 raw, sizeof(raw), &raw_len,
                                                 timeout_ms);
    if (st != ISO15765_OK) {
        return J1979_ERR_TIMEOUT;
    }

    /* Parse response. */
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    int parsed = parse_response(raw, raw_len, mode, pid, &payload, &payload_len);

    if (parsed == -2) {
        /* Negative response: 0x7F [mode] [NRC] */
        if (payload_len >= 1) {
            resp[0] = payload[0];   /* NRC code */
            *out_len = 1;
        } else {
            resp[0] = J1979_NRC_GENERAL_REJECT;
            *out_len = 1;
        }
        return J1979_ERR_NEGATIVE_RESPONSE;
    }
    if (parsed != 0) {
        return J1979_ERR_PROTOCOL;
    }

    /* Copy payload to caller buffer. */
    if (payload_len > resp_buf_size) {
        return J1979_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(resp, payload, payload_len);
    *out_len = payload_len;
    return J1979_OK;
}

/* ------------------------------------------------------------------------
 * Mode-specific helpers
 * ------------------------------------------------------------------------ */

j1979_status_t j1979_read_pid(j1979_t *j1979, uint8_t pid,
                              uint8_t *data, size_t data_buf_size,
                              size_t *out_len, uint32_t timeout_ms)
{
    return j1979_send(j1979, J1979_MODE_LIVE_DATA, pid,
                      NULL, 0, data, data_buf_size, out_len, timeout_ms);
}

j1979_status_t j1979_read_dtcs_confirmed(j1979_t *j1979,
                                         dtc_list_t *list,
                                         uint32_t timeout_ms)
{
    if (j1979 == NULL || list == NULL) {
        return J1979_ERR_INVALID_ARG;
    }
    if (!j1979->initialized) {
        return J1979_ERR_NOT_INITIALIZED;
    }

    uint8_t resp[4096];
    size_t resp_len = 0;
    j1979_status_t st = j1979_send(j1979, J1979_MODE_DTC_CONFIRMED, 0x00,
                                   NULL, 0, resp, sizeof(resp), &resp_len,
                                   timeout_ms);
    if (st != J1979_OK) {
        return st;
    }

    /* Response data: DTCs, 2 bytes each. */
    return (j1979_status_t)dtc_decode_list(resp, resp_len, list);
}

j1979_status_t j1979_read_dtcs_pending(j1979_t *j1979,
                                       dtc_list_t *list,
                                       uint32_t timeout_ms)
{
    if (j1979 == NULL || list == NULL) {
        return J1979_ERR_INVALID_ARG;
    }
    if (!j1979->initialized) {
        return J1979_ERR_NOT_INITIALIZED;
    }

    uint8_t resp[4096];
    size_t resp_len = 0;
    j1979_status_t st = j1979_send(j1979, J1979_MODE_DTC_PENDING, 0x00,
                                   NULL, 0, resp, sizeof(resp), &resp_len,
                                   timeout_ms);
    if (st != J1979_OK) {
        return st;
    }

    return (j1979_status_t)dtc_decode_list(resp, resp_len, list);
}

j1979_status_t j1979_read_dtcs_permanent(j1979_t *j1979,
                                         dtc_list_t *list,
                                         uint32_t timeout_ms)
{
    if (j1979 == NULL || list == NULL) {
        return J1979_ERR_INVALID_ARG;
    }
    if (!j1979->initialized) {
        return J1979_ERR_NOT_INITIALIZED;
    }

    uint8_t resp[4096];
    size_t resp_len = 0;
    j1979_status_t st = j1979_send(j1979, J1979_MODE_DTC_PERMANENT, 0x00,
                                   NULL, 0, resp, sizeof(resp), &resp_len,
                                   timeout_ms);
    if (st != J1979_OK) {
        return st;
    }

    return (j1979_status_t)dtc_decode_list(resp, resp_len, list);
}

j1979_status_t j1979_clear_dtcs(j1979_t *j1979, uint32_t timeout_ms)
{
    if (j1979 == NULL) return J1979_ERR_INVALID_ARG;
    if (!j1979->initialized) return J1979_ERR_NOT_INITIALIZED;

    uint8_t resp[8];
    size_t resp_len = 0;
    return j1979_send(j1979, J1979_MODE_CLEAR_DTC, 0x00,
                      NULL, 0, resp, sizeof(resp), &resp_len, timeout_ms);
}

/* ------------------------------------------------------------------------
 * PID value conversion helpers
 * ------------------------------------------------------------------------ */

float j1979_pid_rpm(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 2) return -1.0f;
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    return (float)raw / 4.0f;
}

float j1979_pid_speed(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 1) return -1.0f;
    return (float)data[0];
}

float j1979_pid_coolant_temp(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 1) return -40.0f;
    return (float)data[0] - 40.0f;
}

float j1979_pid_intake_temp(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 1) return -40.0f;
    return (float)data[0] - 40.0f;
}

float j1979_pid_engine_load(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 1) return -1.0f;
    return (float)data[0] * 100.0f / 255.0f;
}

float j1979_pid_maf(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 2) return -1.0f;
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    return (float)raw / 100.0f;
}

float j1979_pid_throttle(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 1) return -1.0f;
    return (float)data[0] * 100.0f / 255.0f;
}