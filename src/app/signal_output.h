/*
 * app/signal_output.h — Signal output / sensor simulation (Feature 5: Keluaran Sinyal).
 *
 * Simulates sensor signals to the ECU for testing. Uses UDS $2F
 * InputOutputControlByIdentifier on K-Line (KWP2000/UDS) or J1979 Mode $08
 * (Control of on-board system/test/component) on CAN, both through the unified
 * protocol (protocol_*).
 *
 * Safety: the WARN_ACTUATOR_TEST warning screen is shown before any signal is
 * commanded and SELECT is required. A forced 30-second timeout applies.
 *
 * Layering: APP -> signal_output_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_SIGNAL_OUTPUT_H
#define APP_SIGNAL_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"     /* protocol_t, proto_get_transport, proto_is_initialized */
#include "protocol/kline_proto.h"  /* kline_proto_uds_io_control */
#include "protocol/j1979.h"        /* J1979_MODE_CONTROL, j1979_send */
#include "ui/warning_screen.h"     /* warning_show, WARN_ACTUATOR_TEST, display_t */
#include "ui/buttons.h"            /* BTN_BACK (unused here, kept for symmetry) */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    SIGNAL_OUTPUT_OK                    = 0,
    SIGNAL_OUTPUT_ERR_INVALID_ARG       = -1,
    SIGNAL_OUTPUT_ERR_NOT_INITIALIZED   = -2,
    SIGNAL_OUTPUT_ERR_PROTOCOL          = -3,   /* underlying protocol failure */
    SIGNAL_OUTPUT_ERR_TIMEOUT           = -4,   /* command timeout */
    SIGNAL_OUTPUT_ERR_USER_ABORT        = -5,   /* user declined via warning screen */
    SIGNAL_OUTPUT_ERR_SAFETY            = -6,   /* safety condition not met */
    SIGNAL_OUTPUT_ERR_NOT_SUPPORTED     = -7,   /* transport not CAN or K-Line */
} signal_output_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct signal_output signal_output_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* already-initialized unified protocol handle */
    display_t  *disp;    /* display for the pre-test warning screen */
} signal_output_config_t;

/* Default config (designated initializers). */
#define SIGNAL_OUTPUT_CONFIG_DEFAULT(p, d) { .proto = (p), .disp = (d) }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the signal output service over an already-initialized protocol
 * handle. cfg->proto and cfg->disp must be non-NULL. */
signal_output_status_t signal_output_init(const signal_output_config_t *cfg,
                                          signal_output_t **out);

/* Release the signal output handle. */
signal_output_status_t signal_output_deinit(signal_output_t *so);

/* True if the handle was successfully initialized. */
bool signal_output_is_initialized(const signal_output_t *so);

/* =========================================================================
 * Signal output
 * ========================================================================= */

/*
 * Command a signal output / sensor simulation to the ECU.
 *
 * did         : UDS data identifier (K-Line) or J1979 Mode $08 PID (CAN).
 * control_op  : UDS control operation (0x01=returnToEcu, 0x02=startAdjustment,
 *               0x03=startTest, etc.) — used on K-Line only; ignored on CAN.
 * params      : control parameter bytes (may be NULL if params_len==0).
 * params_len  : length of params.
 *
 * On CAN/J1979 Mode $08: sends [control_byte] where control_byte = params[0]
 * (or 0x01 if params is NULL), identifying the test/component via did (PID).
 * On K-Line/UDS $2F: calls kline_proto_uds_io_control() with did, control_op,
 * and params.
 *
 * warning_show(WARN_ACTUATOR_TEST) is shown and SELECT is required before any
 * signal is commanded; otherwise SIGNAL_OUTPUT_ERR_USER_ABORT is returned.
 */
signal_output_status_t signal_output_set(signal_output_t *so, uint16_t did,
                                         uint8_t control_op,
                                         const uint8_t *params, size_t params_len,
                                         uint32_t timeout_ms);

/*
 * Release control of a previously commanded signal, returning the component
 * to ECU control.
 *
 * did : UDS DID (K-Line) or J1979 PID (CAN) of the component to release.
 *
 * On K-Line: sends UDS $2F with control_op 0x01 (returnToEcu).
 * On CAN: sends J1979 Mode $08 with control byte 0x00 (stop).
 */
signal_output_status_t signal_output_release(signal_output_t *so, uint16_t did,
                                              uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_OUTPUT_H */
