/*
 * app/co_adjust.h — CO / idle mixture adjustment (Feature 11: Penyesuaian CO Idle).
 *
 * Adjusts the fuel mixture (rich/lean) at idle for early fuel-injected
 * motorcycles (e.g. Yamaha). Uses UDS $2F InputOutputControl on K-Line or
 * J1979 Mode $08 on CAN through the unified protocol (protocol_*).
 *
 * Safety: warning_show(WARN_CUSTOM) before any mixture change — incorrect
 * idle mixture can damage the engine. SELECT is required.
 *
 * Layering: APP -> co_adjust_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_CO_ADJUST_H
#define APP_CO_ADJUST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"     /* protocol_t, proto_get_transport, proto_is_initialized */
#include "protocol/kline_proto.h"  /* kline_proto_uds_io_control */
#include "protocol/j1979.h"        /* J1979_MODE_CONTROL, j1979_send */
#include "ui/warning_screen.h"     /* warning_show, WARN_CUSTOM, display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    CO_ADJUST_OK                    = 0,
    CO_ADJUST_ERR_INVALID_ARG       = -1,
    CO_ADJUST_ERR_NOT_INITIALIZED   = -2,
    CO_ADJUST_ERR_PROTOCOL          = -3,   /* underlying protocol failure */
    CO_ADJUST_ERR_TIMEOUT           = -4,   /* command timeout */
    CO_ADJUST_ERR_USER_ABORT        = -5,   /* user declined via warning screen */
    CO_ADJUST_ERR_SAFETY            = -6,   /* safety condition not met */
    CO_ADJUST_ERR_NOT_SUPPORTED     = -7,   /* transport not CAN or K-Line */
} co_adjust_status_t;

/* =========================================================================
 * Mixture adjustment range
 * ========================================================================= */

/* Adjustment value: -128 = maximum lean, 0 = stock, +127 = maximum rich. */
#define CO_ADJUST_MIXTURE_MIN -128
#define CO_ADJUST_MIXTURE_MAX 127

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct co_adjust co_adjust_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* already-initialized unified protocol handle */
    display_t  *disp;    /* display for the pre-adjustment warning screen */

    /* UDS DID / J1979 PID used for idle mixture control. Manufacturer-specific;
     * defaults cover common early injection ECUs. */
    uint16_t uds_did;    /* default: 0x0110 */
    uint8_t  j1979_pid;  /* default: 0x10 */
} co_adjust_config_t;

/* Default config (designated initializers). */
#define CO_ADJUST_CONFIG_DEFAULT(p, d)                                        \
    {                                                                         \
        .proto = (p), .disp = (d),                                            \
        .uds_did = 0x0110U, .j1979_pid = 0x10U,                             \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the CO adjust service over an already-initialized protocol
 * handle. cfg->proto and cfg->disp must be non-NULL. */
co_adjust_status_t co_adjust_init(const co_adjust_config_t *cfg,
                                  co_adjust_t **out);

/* Release the co_adjust handle. */
co_adjust_status_t co_adjust_deinit(co_adjust_t *ca);

/* True if the handle was successfully initialized. */
bool co_adjust_is_initialized(const co_adjust_t *ca);

/* =========================================================================
 * Mixture adjustment
 * ========================================================================= */

/* Set the idle fuel mixture adjustment.
 *
 * adjustment: -128 (max lean) .. 0 (stock) .. +127 (max rich).
 *
 * On K-Line/UDS $2F: sends kline_proto_uds_io_control() with uds_did,
 * control_op 0x02 (shortTermAdjustment), and params = [adjustment byte].
 * On CAN/J1979 Mode $08: sends j1979_send() with J1979_MODE_CONTROL,
 * pid = j1979_pid, and data = [adjustment byte as uint8_t].
 *
 * warning_show(WARN_CUSTOM) is shown and SELECT is required before changing
 * the mixture; otherwise CO_ADJUST_ERR_USER_ABORT is returned.
 */
co_adjust_status_t co_adjust_set_mixture(co_adjust_t *ca, int8_t adjustment,
                                          uint32_t timeout_ms);

/* Read the current idle mixture adjustment from the ECU.
 *
 * current: receives the current adjustment (-128 .. +127).
 *
 * On K-Line/UDS $22 ReadDID: reads uds_did and parses the first data byte.
 * On CAN/J1979 Mode $08: reads j1979_pid and parses the first data byte.
 *
 * Returns CO_ADJUST_OK with current filled, or an error status. */
co_adjust_status_t co_adjust_read_mixture(co_adjust_t *ca, int8_t *current,
                                           uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_CO_ADJUST_H */
