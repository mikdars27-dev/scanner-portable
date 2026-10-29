/*
 * app/abs_module.h — ABS module diagnostics (Feature 7: Modul ABS).
 *
 * Reads and clears ABS trouble codes and tests ABS hydraulic/electronic brake
 * components. ABS service IDs are manufacturer-specific, so the SIDs used for
 * read/clear DTC and component test are configurable via abs_config_t with
 * sensible defaults (UDS $19 ReadDTCInformation, $14 ClearDiagnosticInformation,
 * $2F InputOutputControl).
 *
 * Safety: warning_show(WARN_CUSTOM) before component testing (brake system
 * safety). Reading/clearing DTCs are read-only-ish and do not require the
 * warning screen, but clearing DTCs is destructive (cannot be undone).
 *
 * Layering: APP -> abs_module_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_ABS_MODULE_H
#define APP_ABS_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"     /* protocol_t, proto_send, proto_status_t */
#include "protocol/dtc.h"          /* dtc_list_t */
#include "ui/warning_screen.h"     /* warning_show, WARN_CUSTOM, display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    ABS_OK                    = 0,
    ABS_ERR_INVALID_ARG       = -1,
    ABS_ERR_NOT_INITIALIZED   = -2,
    ABS_ERR_PROTOCOL          = -3,   /* underlying protocol failure */
    ABS_ERR_TIMEOUT           = -4,   /* command timeout */
    ABS_ERR_USER_ABORT        = -5,   /* user declined via warning screen */
    ABS_ERR_SAFETY            = -6,   /* safety condition not met */
    ABS_ERR_NOT_SUPPORTED     = -7,   /* transport not supported */
} abs_status_t;

/* =========================================================================
 * ABS component IDs (for abs_test_component)
 * ========================================================================= */

#define ABS_COMP_HYDRAULIC_UNIT      0x01U  /* hydraulic modulator unit */
#define ABS_COMP_FRONT_LEFT_SOLENOID 0x02U  /* front left inlet/outlet solenoid */
#define ABS_COMP_FRONT_RIGHT_SOLENOID 0x03U /* front right inlet/outlet solenoid */
#define ABS_COMP_REAR_LEFT_SOLENOID  0x04U  /* rear left inlet/outlet solenoid */
#define ABS_COMP_REAR_RIGHT_SOLENOID 0x05U  /* rear right inlet/outlet solenoid */
#define ABS_COMP_PUMP_MOTOR          0x06U  /* ABS pump motor */
#define ABS_COMP_RELAY               0x07U  /* ABS relay */

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct abs abs_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* already-initialized unified protocol handle */
    display_t  *disp;    /* display for the pre-test warning screen */

    /* Manufacturer-specific ABS service IDs. Defaults cover common UDS/KWP2000
     * ABS implementations; adjust per vehicle make/model. */
    uint8_t abs_sid_read_dtcs;        /* default: 0x19 UDS ReadDTCInformation */
    uint8_t abs_sid_clear_dtcs;       /* default: 0x14 UDS ClearDiagnosticInfo */
    uint8_t abs_sid_test_component;   /* default: 0x2F UDS InputOutputControl */
} abs_config_t;

/* Default config (designated initializers). */
#define ABS_CONFIG_DEFAULT(p, d)                                              \
    {                                                                         \
        .proto = (p), .disp = (d),                                            \
        .abs_sid_read_dtcs = 0x19U,                                           \
        .abs_sid_clear_dtcs = 0x14U,                                          \
        .abs_sid_test_component = 0x2FU,                                      \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the ABS module service over an already-initialized protocol
 * handle. cfg->proto and cfg->disp must be non-NULL. */
abs_status_t abs_init(const abs_config_t *cfg, abs_t **out);

/* Release the ABS module handle. */
abs_status_t abs_deinit(abs_t *abs);

/* True if the handle was successfully initialized. */
bool abs_is_initialized(const abs_t *abs);

/* =========================================================================
 * ABS diagnostics
 * ========================================================================= */

/* Read ABS trouble codes.
 *
 * Sends the ABS read-DTCs request (abs_sid_read_dtcs) and decodes the raw
 * 2-byte DTC response entries into list->entries (caller-allocated, capacity
 * must be set by the caller). list->count is set to the number decoded.
 *
 * Returns ABS_OK with list filled, or an error status on failure. */
abs_status_t abs_read_dtcs(abs_t *abs, dtc_list_t *list, uint32_t timeout_ms);

/* Clear ABS trouble codes.
 *
 * Sends the ABS clear-DTCs request (abs_sid_clear_dtcs) with mask 0xFFFFFF
 * (all DTCs). This is destructive and cannot be undone.
 *
 * Returns ABS_OK on success. */
abs_status_t abs_clear_dtcs(abs_t *abs, uint32_t timeout_ms);

/* Test an ABS component (hydraulic/electronic brake component).
 *
 * component_id: one of ABS_COMP_* above.
 * Sends the ABS component test request (abs_sid_test_component) with
 * data [component_id, 0x01=start].
 *
 * warning_show(WARN_CUSTOM) is shown and SELECT is required before testing;
 * otherwise ABS_ERR_USER_ABORT is returned and nothing is commanded.
 */
abs_status_t abs_test_component(abs_t *abs, uint8_t component_id,
                                 uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_ABS_MODULE_H */
