/*
 * app/sensor_test.c — Application-layer ECU sensor/monitor tests.
 *
 * Runs O2 bank 1 sensor 1/2 (mode $05), catalyst and EVAP monitors
 * (mode $06), and a generic DID read via UDS $22 over the unified protocol,
 * formatting results into human-readable strings for the UI layer.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "sensor_test.h"

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */

struct sensor_test {
    protocol_t *proto;
    bool        initialized;
};

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

/* Map a j1979_status_t into a sensor_test_status_t. */
static sensor_test_status_t sensor_test_from_j1979(j1979_status_t st)
{
    switch (st) {
        case J1979_OK:                    return SENSOR_TEST_OK;
        case J1979_ERR_INVALID_ARG:       return SENSOR_TEST_ERR_INVALID_ARG;
        case J1979_ERR_NOT_INITIALIZED:   return SENSOR_TEST_ERR_NOT_INITIALIZED;
        case J1979_ERR_BUFFER_TOO_SMALL:  return SENSOR_TEST_ERR_BUFFER_TOO_SMALL;
        case J1979_ERR_TIMEOUT:
        case J1979_ERR_PROTOCOL:
        case J1979_ERR_NEGATIVE_RESPONSE:
        default:                          return SENSOR_TEST_ERR_PROTOCOL;
    }
}

/* Map a kline_proto_status_t into a sensor_test_status_t. */
static sensor_test_status_t sensor_test_from_kline(kline_proto_status_t st)
{
    switch (st) {
        case KLINE_PROTO_OK:                    return SENSOR_TEST_OK;
        case KLINE_PROTO_ERR_INVALID_ARG:       return SENSOR_TEST_ERR_INVALID_ARG;
        case KLINE_PROTO_ERR_NOT_INITIALIZED:   return SENSOR_TEST_ERR_NOT_INITIALIZED;
        case KLINE_PROTO_ERR_BUFFER_TOO_SMALL:  return SENSOR_TEST_ERR_BUFFER_TOO_SMALL;
        case KLINE_PROTO_ERR_TIMEOUT:
        case KLINE_PROTO_ERR_PROTOCOL:
        case KLINE_PROTO_ERR_NEGATIVE_RESPONSE:
        default:                                return SENSOR_TEST_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Response formatting helpers
 * ------------------------------------------------------------------------ */

/*
 * Format `len` bytes from `data` as space-separated hex into out_buf.
 * Returns the number of characters written (excluding NUL), or -1 when
 * the output does not fit (including the NUL terminator).
 */
static int sensor_test_format_hex(char *out_buf, size_t out_buf_len,
                                  const uint8_t *data, size_t len)
{
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        int n = snprintf(out_buf + pos, out_buf_len - pos,
                         i == 0 ? "%02X" : " %02X", data[i]);
        if (n < 0 || (size_t)n >= out_buf_len - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    return (int)pos;
}

/* ------------------------------------------------------------------------
 * Test 1/2: O2 sensor monitor results (mode $05)
 * ------------------------------------------------------------------------ */

/*
 * Mode $05 response data is a sequence of 4-byte groups, one per O2 sensor
 * monitor: group 0 = bank 1 sensor 1, group 1 = bank 1 sensor 2, ...
 * (ECU-dependent; adjust the group size/offset if the target ECU differs.)
 */
static sensor_test_status_t sensor_test_o2(sensor_test_t *st, uint8_t group,
                                           char *out_buf, size_t out_buf_len,
                                           uint32_t timeout_ms)
{
    j1979_t *j1979 = proto_j1979_handle(st->proto);
    if (j1979 == NULL) {
        return SENSOR_TEST_ERR_PROTOCOL;
    }

    uint8_t resp[256];
    size_t  resp_len = 0;
    j1979_status_t jst = j1979_send(j1979, J1979_MODE_O2_SENSOR, 0x00,
                                    NULL, 0, resp, sizeof(resp), &resp_len,
                                    timeout_ms);
    if (jst != J1979_OK) {
        return sensor_test_from_j1979(jst);
    }

    if (resp_len < (group + 1) * 4) {
        return SENSOR_TEST_ERR_PROTOCOL;
    }

    const uint8_t *g = &resp[group * 4];
    int n = snprintf(out_buf, out_buf_len,
                     "O2 B1S%d: %02X %02X %02X %02X",
                     (int)group + 1, g[0], g[1], g[2], g[3]);
    if (n < 0 || (size_t)n >= out_buf_len) {
        return SENSOR_TEST_ERR_BUFFER_TOO_SMALL;
    }
    return SENSOR_TEST_OK;
}

/* ------------------------------------------------------------------------
 * Test 3/4: Mode $06 non-continuous monitor tests
 * ------------------------------------------------------------------------ */

/*
 * Mode $06 response data after the MID echo:
 * [TestValueHi][TestValueLo][MinHi][MinLo][MaxHi][MaxLo]
 * Values are raw 16-bit integers (mode $06 defines no scaling factor).
 */
static sensor_test_status_t sensor_test_monitor(sensor_test_t *st, uint8_t mid,
                                                const char *name,
                                                char *out_buf, size_t out_buf_len,
                                                uint32_t timeout_ms)
{
    j1979_t *j1979 = proto_j1979_handle(st->proto);
    if (j1979 == NULL) {
        return SENSOR_TEST_ERR_PROTOCOL;
    }

    uint8_t resp[256];
    size_t  resp_len = 0;
    j1979_status_t jst = j1979_send(j1979, J1979_MODE_MONITOR_NONCONT, mid,
                                    NULL, 0, resp, sizeof(resp), &resp_len,
                                    timeout_ms);
    if (jst != J1979_OK) {
        return sensor_test_from_j1979(jst);
    }

    if (resp_len < 6) {
        return SENSOR_TEST_ERR_PROTOCOL;
    }

    uint16_t value = ((uint16_t)resp[0] << 8) | resp[1];
    uint16_t mn    = ((uint16_t)resp[2] << 8) | resp[3];
    uint16_t mx    = ((uint16_t)resp[4] << 8) | resp[5];

    int n = snprintf(out_buf, out_buf_len,
                     "%s: value=%u min=%u max=%u",
                     name, (unsigned int)value, (unsigned int)mn,
                     (unsigned int)mx);
    if (n < 0 || (size_t)n >= out_buf_len) {
        return SENSOR_TEST_ERR_BUFFER_TOO_SMALL;
    }
    return SENSOR_TEST_OK;
}

/* ------------------------------------------------------------------------
 * Test 5: Generic DID read via UDS $22 (K-Line)
 * ------------------------------------------------------------------------ */

static sensor_test_status_t sensor_test_did_uds(sensor_test_t *st,
                                                char *out_buf, size_t out_buf_len,
                                                uint32_t timeout_ms)
{
    kline_proto_t *kline = proto_kline_handle(st->proto);
    if (kline == NULL) {
        return SENSOR_TEST_ERR_PROTOCOL;
    }

    uint8_t resp[256];
    size_t  resp_len = 0;
    kline_proto_status_t kst = kline_proto_uds_read_did(
        kline, SENSOR_TEST_DID_DEFAULT, resp, sizeof(resp), &resp_len,
        timeout_ms);
    if (kst != KLINE_PROTO_OK) {
        return sensor_test_from_kline(kst);
    }

    int n = snprintf(out_buf, out_buf_len,
                     "DID 0x%04X (%u bytes): ",
                     SENSOR_TEST_DID_DEFAULT, (unsigned int)resp_len);
    if (n < 0 || (size_t)n >= out_buf_len) {
        return SENSOR_TEST_ERR_BUFFER_TOO_SMALL;
    }

    int r = sensor_test_format_hex(out_buf + (size_t)n,
                                   out_buf_len - (size_t)n,
                                   resp, resp_len);
    if (r < 0) {
        return SENSOR_TEST_ERR_BUFFER_TOO_SMALL;
    }
    return SENSOR_TEST_OK;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

sensor_test_status_t sensor_test_init(const sensor_test_config_t *cfg,
                                      sensor_test_t **out)
{
    if (cfg == NULL || out == NULL) {
        return SENSOR_TEST_ERR_INVALID_ARG;
    }
    if (cfg->proto == NULL) {
        return SENSOR_TEST_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return SENSOR_TEST_ERR_NOT_INITIALIZED;
    }

    sensor_test_t *st = (sensor_test_t *)calloc(1, sizeof(*st));
    if (st == NULL) {
        return SENSOR_TEST_ERR_INVALID_ARG;
    }

    st->proto       = cfg->proto;
    st->initialized = true;
    *out = st;
    return SENSOR_TEST_OK;
}

sensor_test_status_t sensor_test_deinit(sensor_test_t *st)
{
    if (st == NULL) {
        return SENSOR_TEST_ERR_INVALID_ARG;
    }
    free(st);
    return SENSOR_TEST_OK;
}

bool sensor_test_is_initialized(const sensor_test_t *st)
{
    return st != NULL && st->initialized;
}

sensor_test_status_t sensor_test_run(sensor_test_t *st, uint8_t test_id,
                                     char *out_buf, size_t out_buf_len,
                                     uint32_t timeout_ms)
{
    if (st == NULL || out_buf == NULL || out_buf_len == 0) {
        return SENSOR_TEST_ERR_INVALID_ARG;
    }
    if (!st->initialized) {
        return SENSOR_TEST_ERR_NOT_INITIALIZED;
    }
    if (st->proto == NULL || !proto_is_initialized(st->proto)) {
        return SENSOR_TEST_ERR_NOT_INITIALIZED;
    }

    switch (test_id) {
        case SENSOR_TEST_O2_BANK1_S1:
            if (proto_get_transport(st->proto) != PROTO_TRANSPORT_CAN) {
                return SENSOR_TEST_ERR_PROTOCOL;
            }
            return sensor_test_o2(st, 0, out_buf, out_buf_len, timeout_ms);
        case SENSOR_TEST_O2_BANK1_S2:
            if (proto_get_transport(st->proto) != PROTO_TRANSPORT_CAN) {
                return SENSOR_TEST_ERR_PROTOCOL;
            }
            return sensor_test_o2(st, 1, out_buf, out_buf_len, timeout_ms);
        case SENSOR_TEST_CATALYST_MONITOR:
            if (proto_get_transport(st->proto) != PROTO_TRANSPORT_CAN) {
                return SENSOR_TEST_ERR_PROTOCOL;
            }
            return sensor_test_monitor(st, SENSOR_TEST_MID_CATALYST,
                                       "Catalyst", out_buf, out_buf_len,
                                       timeout_ms);
        case SENSOR_TEST_EVAP_MONITOR:
            if (proto_get_transport(st->proto) != PROTO_TRANSPORT_CAN) {
                return SENSOR_TEST_ERR_PROTOCOL;
            }
            return sensor_test_monitor(st, SENSOR_TEST_MID_EVAP,
                                       "EVAP", out_buf, out_buf_len,
                                       timeout_ms);
        case SENSOR_TEST_DID_UDS_22:
            if (proto_get_transport(st->proto) != PROTO_TRANSPORT_KLINE) {
                return SENSOR_TEST_ERR_PROTOCOL;
            }
            return sensor_test_did_uds(st, out_buf, out_buf_len, timeout_ms);
        default:
            return SENSOR_TEST_ERR_INVALID_ARG;
    }
}
