/*
 * protocol/kline_proto.c — KWP2000 / UDS diagnostic services on K-Line.
 *
 * Built on top of drivers/kline.c.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "protocol/kline_proto.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */
typedef struct {
    kline_t *kline;
    bool     initialized;
} kline_proto_t;

/* ------------------------------------------------------------------------
 * Request / response helpers
 * ------------------------------------------------------------------------ */

/* Build a KWP2000/UDS request: [sid][data...] */
static size_t build_request(uint8_t sid, const uint8_t *data, size_t data_len,
                            uint8_t *out, size_t out_size)
{
    size_t needed = 1 + data_len;
    if (needed > out_size) return 0;
    out[0] = sid;
    if (data_len > 0 && data != NULL) {
        memcpy(&out[1], data, data_len);
    }
    return needed;
}

/* Parse a KWP2000/UDS response.
 * Returns 0 on success, -1 on protocol error, -2 on negative response.
 * On success: resp points to payload after SID+0x40 echo, *resp_len set.
 * On NRC: resp[0] = NRC code, *resp_len = 1. */
static int parse_response(const uint8_t *raw, size_t raw_len,
                          uint8_t expected_sid,
                          const uint8_t **resp, size_t *resp_len)
{
    if (raw_len < 1) return -1;

    uint8_t resp_sid = raw[0];

    /* Negative response: 0x7F [sid] [NRC] */
    if (resp_sid == 0x7F) {
        if (raw_len < 3) return -1;
        *resp = &raw[2];
        *resp_len = 1;
        return -2;
    }

    uint8_t expected_resp_sid = (uint8_t)(expected_sid + 0x40);
    if (resp_sid != expected_resp_sid) return -1;

    if (raw_len < 1) return -1;
    *resp = &raw[1];
    *resp_len = raw_len - 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

kline_proto_status_t kline_proto_init(const kline_proto_config_t *cfg,
                                      kline_proto_t **out_proto)
{
    if (cfg == NULL || out_proto == NULL || cfg->kline == NULL) {
        return KLINE_PROTO_ERR_INVALID_ARG;
    }
    if (!kline_is_initialized(cfg->kline)) {
        return KLINE_PROTO_ERR_NOT_INITIALIZED;
    }

    kline_proto_t *p = (kline_proto_t *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return KLINE_PROTO_ERR_INVALID_ARG;
    }

    p->kline = cfg->kline;
    p->initialized = true;

    *out_proto = p;
    return KLINE_PROTO_OK;
}

kline_proto_status_t kline_proto_deinit(kline_proto_t *proto)
{
    if (proto == NULL) return KLINE_PROTO_ERR_INVALID_ARG;
    free(proto);
    return KLINE_PROTO_OK;
}

bool kline_proto_is_initialized(const kline_proto_t *proto)
{
    return proto != NULL && proto->initialized;
}

kline_proto_status_t kline_proto_send(kline_proto_t *proto,
                                      uint8_t sid, const uint8_t *data,
                                      size_t data_len,
                                      uint8_t *resp, size_t resp_buf_size,
                                      size_t *out_len, uint32_t timeout_ms)
{
    if (proto == NULL || resp == NULL || out_len == NULL) {
        return KLINE_PROTO_ERR_INVALID_ARG;
    }
    if (!proto->initialized) {
        return KLINE_PROTO_ERR_NOT_INITIALIZED;
    }

    /* Build request: [sid][data...] */
    uint8_t req[256];
    size_t req_len = build_request(sid, data, data_len, req, sizeof(req));
    if (req_len == 0) {
        return KLINE_PROTO_ERR_BUFFER_TOO_SMALL;
    }

    /* Send request and receive response via kline_transfer. */
    uint8_t raw[256];
    size_t raw_len = 0;
    kline_status_t st = kline_transfer(proto->kline,
                                       req, req_len,
                                       raw, sizeof(raw), &raw_len,
                                       timeout_ms);
    if (st != KLINE_OK) {
        return KLINE_PROTO_ERR_TIMEOUT;
    }

    /* Parse response. */
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    int parsed = parse_response(raw, raw_len, sid, &payload, &payload_len);

    if (parsed == -2) {
        /* Negative response: 0x7F [sid] [NRC] */
        if (payload_len >= 1) {
            resp[0] = payload[0];   /* NRC code */
            *out_len = 1;
        } else {
            resp[0] = KLINE_PROTO_NRC_GENERAL_REJECT;
            *out_len = 1;
        }
        return KLINE_PROTO_ERR_NEGATIVE_RESPONSE;
    }
    if (parsed != 0) {
        return KLINE_PROTO_ERR_PROTOCOL;
    }

    /* Copy payload to caller buffer. */
    if (payload_len > resp_buf_size) {
        return KLINE_PROTO_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(resp, payload, payload_len);
    *out_len = payload_len;
    return KLINE_PROTO_OK;
}

/* ------------------------------------------------------------------------
 * UDS mode-specific helpers
 * ------------------------------------------------------------------------ */

kline_proto_status_t kline_proto_uds_session(kline_proto_t *proto,
                                             uint8_t session_type,
                                             uint32_t timeout_ms)
{
    if (proto == NULL) return KLINE_PROTO_ERR_INVALID_ARG;
    uint8_t args[1] = { session_type };
    uint8_t resp[8];
    size_t resp_len = 0;
    return kline_proto_send(proto, KLINE_PROTO_SID_DIAG_SESSION_CONTROL,
                            args, 1, resp, sizeof(resp), &resp_len, timeout_ms);
}

kline_proto_status_t kline_proto_uds_seed(kline_proto_t *proto,
                                          uint8_t level,
                                          uint8_t *seed, size_t seed_buf_size,
                                          size_t *out_len,
                                          uint32_t timeout_ms)
{
    if (proto == NULL || seed == NULL || out_len == NULL) {
        return KLINE_PROTO_ERR_INVALID_ARG;
    }
    uint8_t args[1] = { level };
    uint8_t resp[32];
    size_t resp_len = 0;
    kline_proto_status_t st = kline_proto_send(proto, KLINE_PROTO_SID_SECURITY_ACCESS,
                                               args, 1, resp, sizeof(resp),
                                               &resp_len, timeout_ms);
    if (st != KLINE_PROTO_OK) {
        return st;
    }
    /* Response: [level_echo][seed_bytes...] */
    if (resp_len < 1) return KLINE_PROTO_ERR_PROTOCOL;
    size_t seed_bytes = resp_len - 1;
    if (seed_bytes > seed_buf_size) seed_bytes = seed_buf_size;
    memcpy(seed, &resp[1], seed_bytes);
    *out_len = seed_bytes;
    return KLINE_PROTO_OK;
}

kline_proto_status_t kline_proto_uds_key(kline_proto_t *proto,
                                         uint8_t level,
                                         const uint8_t *key, size_t key_len,
                                         uint32_t timeout_ms)
{
    if (proto == NULL || key == NULL) return KLINE_PROTO_ERR_INVALID_ARG;
    uint8_t args[1 + 255];
    args[0] = level;
    if (key_len > 255) return KLINE_PROTO_ERR_INVALID_ARG;
    memcpy(&args[1], key, key_len);
    uint8_t resp[8];
    size_t resp_len = 0;
    return kline_proto_send(proto, KLINE_PROTO_SID_SECURITY_ACCESS,
                            args, 1 + key_len, resp, sizeof(resp),
                            &resp_len, timeout_ms);
}

kline_proto_status_t kline_proto_uds_read_dtc(kline_proto_t *proto,
                                              uint8_t subfunc,
                                              const uint8_t *args, size_t args_len,
                                              uint8_t *resp, size_t resp_buf_size,
                                              size_t *out_len, uint32_t timeout_ms)
{
    if (proto == NULL || resp == NULL || out_len == NULL) {
        return KLINE_PROTO_ERR_INVALID_ARG;
    }
    if (1 + args_len > 255) return KLINE_PROTO_ERR_INVALID_ARG;
    uint8_t all_args[256];
    all_args[0] = subfunc;
    if (args_len > 0 && args != NULL) {
        memcpy(&all_args[1], args, args_len);
    }
    return kline_proto_send(proto, KLINE_PROTO_SID_READ_DTC_INFO,
                            all_args, 1 + args_len, resp, resp_buf_size,
                            out_len, timeout_ms);
}

kline_proto_status_t kline_proto_uds_clear_dtc(kline_proto_t *proto,
                                               uint32_t dtc_mask,
                                               uint32_t timeout_ms)
{
    if (proto == NULL) return KLINE_PROTO_ERR_INVALID_ARG;
    uint8_t args[4];
    args[0] = (uint8_t)((dtc_mask >> 16) & 0xFF);
    args[1] = (uint8_t)((dtc_mask >> 8)  & 0xFF);
    args[2] = (uint8_t)(dtc_mask & 0xFF);
    args[3] = 0x00;   /* reserved */
    uint8_t resp[8];
    size_t resp_len = 0;
    return kline_proto_send(proto, KLINE_PROTO_SID_CLEAR_DIAG_INFO,
                            args, 4, resp, sizeof(resp), &resp_len, timeout_ms);
}

kline_proto_status_t kline_proto_uds_read_did(kline_proto_t *proto,
                                              uint16_t did,
                                              uint8_t *resp, size_t resp_buf_size,
                                              size_t *out_len, uint32_t timeout_ms)
{
    if (proto == NULL || resp == NULL || out_len == NULL) {
        return KLINE_PROTO_ERR_INVALID_ARG;
    }
    uint8_t args[2];
    args[0] = (uint8_t)((did >> 8) & 0xFF);
    args[1] = (uint8_t)(did & 0xFF);
    return kline_proto_send(proto, KLINE_PROTO_SID_READ_DATA_BY_ID,
                            args, 2, resp, resp_buf_size, out_len, timeout_ms);
}

kline_proto_status_t kline_proto_uds_io_control(kline_proto_t *proto,
                                                uint16_t did, uint8_t control_op,
                                                const uint8_t *params, size_t params_len,
                                                uint8_t *resp, size_t resp_buf_size,
                                                size_t *out_len, uint32_t timeout_ms)
{
    if (proto == NULL || resp == NULL || out_len == NULL) {
        return KLINE_PROTO_ERR_INVALID_ARG;
    }
    if (3 + params_len > 255) return KLINE_PROTO_ERR_INVALID_ARG;
    uint8_t all_args[256];
    all_args[0] = (uint8_t)((did >> 8) & 0xFF);
    all_args[1] = (uint8_t)(did & 0xFF);
    all_args[2] = control_op;
    if (params_len > 0 && params != NULL) {
        memcpy(&all_args[3], params, params_len);
    }
    return kline_proto_send(proto, KLINE_PROTO_SID_INPUT_OUTPUT_CONTROL,
                            all_args, 3 + params_len, resp, resp_buf_size,
                            out_len, timeout_ms);
}

/* UDS $3E TesterPresent */
kline_proto_status_t kline_proto_uds_tester_present(kline_proto_t *proto,
                                                     uint32_t timeout_ms)
{
    if (proto == NULL) return KLINE_PROTO_ERR_INVALID_ARG;
    uint8_t resp[8];
    size_t resp_len = 0;
    return kline_proto_send(proto, KLINE_PROTO_SID_TESTER_PRESENT,
                            NULL, 0, resp, sizeof(resp), &resp_len, timeout_ms);
}