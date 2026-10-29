/*
 * app/sensor_test.h — Application-layer ECU sensor/monitor tests.
 *
 * Runs onboard ECU sensor tests over the unified protocol: O2 bank 1 sensor
 * 1/2 (mode $05), catalyst and EVAP monitors (mode $06), and a generic DID
 * read via UDS $22 (K-Line). Results are formatted into human-readable
 * strings for the UI layer.
 *
 * Layering: APP -> sensor_test_* -> proto_* -> {j1979_*, kline_proto_*} -> drivers
 */

#ifndef APP_SENSOR_TEST_H
#define APP_SENSOR_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"   /* protocol_t, proto_get_transport, proto_is_initialized */
#include "protocol/j1979.h"      /* j1979_t, j1979_send, J1979_MODE_O2_SENSOR, J1979_MODE_MONITOR_NONCONT */
#include "protocol/kline_proto.h" /* kline_proto_t, kline_proto_uds_read_did */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    SENSOR_TEST_OK                    = 0,
    SENSOR_TEST_ERR_INVALID_ARG       = -1,
    SENSOR_TEST_ERR_NOT_INITIALIZED   = -2,
    SENSOR_TEST_ERR_PROTOCOL          = -3,   /* underlying protocol failure
                                                 * or test not supported on
                                                 * the active transport */
    SENSOR_TEST_ERR_BUFFER_TOO_SMALL  = -4,   /* out_buf cannot hold the result */
} sensor_test_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
/* The real struct is defined only in app/sensor_test.c. */
typedef struct sensor_test sensor_test_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* initialized protocol handle (CAN/J1979 or K-Line/UDS) */
} sensor_test_config_t;

/* Default config (designated initializers). */
#define SENSOR_TEST_CONFIG_DEFAULT(p) { .proto = (p) }

/* =========================================================================
 * Test IDs
 * ========================================================================= */

#define SENSOR_TEST_O2_BANK1_S1       1   /* O2 sensor bank 1 sensor 1 (mode $05) */
#define SENSOR_TEST_O2_BANK1_S2       2   /* O2 sensor bank 1 sensor 2 (mode $05) */
#define SENSOR_TEST_CATALYST_MONITOR  3   /* catalyst monitor (mode $06) */
#define SENSOR_TEST_EVAP_MONITOR      4   /* EVAP monitor (mode $06) */
#define SENSOR_TEST_DID_UDS_22        5   /* generic DID read via UDS $22 */

/* Mode $06 monitor IDs (sent as the mode $06 PID byte). Example defaults;
 * adjust per ECU/manufacturer. */
#define SENSOR_TEST_MID_CATALYST      0x01
#define SENSOR_TEST_MID_EVAP          0x02

/* Default DID for SENSOR_TEST_DID_UDS_22 (0xF190 = VIN on many ECUs). */
#define SENSOR_TEST_DID_DEFAULT       0xF190

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the sensor test layer over an initialized protocol handle. */
sensor_test_status_t sensor_test_init(const sensor_test_config_t *cfg,
                                      sensor_test_t **out);

/* Release the sensor test handle. */
sensor_test_status_t sensor_test_deinit(sensor_test_t *st);

/* True if the handle was successfully initialized. */
bool sensor_test_is_initialized(const sensor_test_t *st);

/* =========================================================================
 * Sensor tests
 * ========================================================================= */

/*
 * Run sensor test `test_id` and write a human-readable result into out_buf.
 * out_buf is always NUL-terminated on success. Returns
 * SENSOR_TEST_ERR_BUFFER_TOO_SMALL when the result (including the NUL
 * terminator) does not fit.
 *
 * Tests 1-4 require the CAN/J1979 transport; test 5 requires the K-Line
 * (UDS) transport. A transport mismatch yields SENSOR_TEST_ERR_PROTOCOL.
 */
sensor_test_status_t sensor_test_run(sensor_test_t *st, uint8_t test_id,
                                     char *out_buf, size_t out_buf_len,
                                     uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_SENSOR_TEST_H */
