/*
 * app/actuator_test.h — Application-layer actuator test (output / bi-directional control).
 *
 * Commands an ECU actuator (fuel pump, idle valve, EGR, injector cycle, relay)
 * for a bounded time. J1979 mode $08 is used on CAN (ISO 15765-2) and UDS
 * $2F InputOutputControlByIdentifier on K-Line (KWP2000/UDS), both through the
 * unified protocol (protocol_*).
 *
 * Safety: before any actuator operation the WARN_ACTUATOR_TEST warning screen
 * is shown and SELECT is required. A forced 30-second timeout applies
 * (ACTUATOR_TEST_MAX_MS) and the STOP button (BACK) is polled every 100 ms
 * while the test runs.
 *
 * Layering: APP -> actuator_test_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 */

#ifndef APP_ACTUATOR_TEST_H
#define APP_ACTUATOR_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"     /* protocol_t, proto_get_transport */
#include "protocol/kline_proto.h"  /* kline_proto_uds_io_control */
#include "protocol/j1979.h"        /* J1979_MODE_CONTROL */
#include "ui/warning_screen.h"     /* warning_show, WARN_ACTUATOR_TEST, display_t */
#include "ui/buttons.h"            /* buttons_read, buttons_wait_release, BTN_BACK */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Test IDs
 * ========================================================================= */

#define ACTUATOR_TEST_ID_FUEL_PUMP   1U   /* fuel pump */
#define ACTUATOR_TEST_ID_IDLE_VALVE  2U   /* idle valve / IAC */
#define ACTUATOR_TEST_ID_EGR         3U   /* EGR valve */
#define ACTUATOR_TEST_ID_INJECTOR    4U   /* injector cycle */
#define ACTUATOR_TEST_ID_RELAY       5U   /* main relay / fuel relay */
#define ACTUATOR_TEST_ID_IGNITION_COIL 6U /* ignition coil */
#define ACTUATOR_TEST_ID_RADIATOR_FAN  7U /* radiator fan */
#define ACTUATOR_TEST_ID_FUEL_PUMP_2   8U /* secondary fuel pump */

#define ACTUATOR_TEST_ID_MIN 1U
#define ACTUATOR_TEST_ID_MAX 8U

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    ACTUATOR_TEST_OK                  = 0,
    ACTUATOR_TEST_ERR_INVALID_ARG     = -1,
    ACTUATOR_TEST_ERR_NOT_INITIALIZED = -2,
    ACTUATOR_TEST_ERR_PROTOCOL        = -3,   /* underlying protocol failure */
    ACTUATOR_TEST_ERR_TIMEOUT         = -4,   /* forced max / duration reached */
    ACTUATOR_TEST_ERR_USER_ABORT      = -5,   /* user declined or stopped */
    ACTUATOR_TEST_ERR_SAFETY          = -6,   /* safety condition not met */
} actuator_test_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
typedef struct actuator_test actuator_test_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    protocol_t *proto;   /* already-initialized unified protocol handle */
    display_t  *disp;    /* display for the pre-test warning screen */
} actuator_test_config_t;

/* Default config (designated initializers). */
#define ACTUATOR_TEST_CONFIG_DEFAULT(p, d) { .proto = (p), .disp = (d) }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the actuator test service over an already-initialized protocol
 * handle. cfg->proto and cfg->disp must be non-NULL. */
actuator_test_status_t actuator_test_init(const actuator_test_config_t *cfg,
                                          actuator_test_t **out);

/* Release the actuator test handle. */
actuator_test_status_t actuator_test_deinit(actuator_test_t *at);

/* True if the handle was successfully initialized. */
bool actuator_test_is_initialized(const actuator_test_t *at);

/* =========================================================================
 * Actuator test
 * ========================================================================= */

/*
 * Run an actuator test for duration_ms.
 *
 * test_id: ACTUATOR_TEST_ID_* (1=fuel pump, 2=idle valve, 3=EGR,
 *          4=injector cycle, 5=relay, 6=ignition coil, 7=radiator fan,
 *          8=secondary fuel pump).
 *
 * The ECU is commanded to start controlling the actuator, the test runs for
 * min(duration_ms, ACTUATOR_TEST_MAX_MS), and the ECU is commanded to return
 * control when the test ends.
 *
 * Before the test, warning_show(WARN_ACTUATOR_TEST) is shown and SELECT is
 * required; otherwise ACTUATOR_TEST_ERR_USER_ABORT is returned and nothing is
 * commanded. During the test the BACK button (STOP) is polled every 100 ms;
 * when pressed the ECU is commanded to return control and *stopped_early is
 * set to true (return value remains ACTUATOR_TEST_OK).
 *
 * Returns ACTUATOR_TEST_ERR_TIMEOUT when the requested duration exceeds
 * ACTUATOR_TEST_MAX_MS and the forced cap is reached.
 */
actuator_test_status_t actuator_test_run(actuator_test_t *at,
                                         uint8_t test_id,
                                         uint32_t duration_ms,
                                         bool *stopped_early);

/*
 * Request an immediate stop of a running test: run() will exit on its next
 * poll and command the ECU to return control. Safe to call from another
 * context (ISR or another task).
 */
actuator_test_status_t actuator_test_stop(actuator_test_t *at);

#ifdef __cplusplus
}
#endif

#endif /* APP_ACTUATOR_TEST_H */
