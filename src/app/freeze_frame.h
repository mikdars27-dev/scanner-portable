/*
 * app/freeze_frame.h — Freeze Frame data (Feature 2: Membekukan Data).
 *
 * Reads OBD2 freeze frame data via J1979 Mode $02. Mode $02 is PID-based like
 * Mode $01: PID $02 returns the DTC that triggered the freeze frame, other
 * PIDs return the frozen parameter values at the moment the DTC first set.
 *
 * Layering: APP -> freeze_frame_* -> protocol_* -> j1979_* -> iso15765_2_* -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_FREEZE_FRAME_H
#define APP_FREEZE_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/protocol.h"
#include "protocol/dtc.h"   /* dtc_entry_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    FREEZE_FRAME_OK                    = 0,
    FREEZE_FRAME_ERR_INVALID_ARG       = -1,
    FREEZE_FRAME_ERR_NOT_INITIALIZED   = -2,
    FREEZE_FRAME_ERR_PROTOCOL          = -3,
    FREEZE_FRAME_ERR_BUFFER_TOO_SMALL  = -4,
    FREEZE_FRAME_ERR_NOT_SUPPORTED     = -5,   /* not CAN/J1979 transport */
    FREEZE_FRAME_ERR_TIMEOUT           = -6,   /* underlying transport timeout */
} freeze_frame_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef struct {
    protocol_t *proto;   /* unified protocol handle (CAN/J1979 required) */
} freeze_frame_config_t;

#define FREEZE_FRAME_CONFIG_DEFAULT(p)  { .proto = (p) }

typedef struct freeze_frame freeze_frame_t;   /* opaque handle */

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

freeze_frame_status_t freeze_frame_init(const freeze_frame_config_t *cfg,
                                        freeze_frame_t **out);

freeze_frame_status_t freeze_frame_deinit(freeze_frame_t *ff);

bool freeze_frame_is_initialized(const freeze_frame_t *ff);

/* =========================================================================
 * Freeze frame read
 * ========================================================================= */

/* Read the DTC that triggered the freeze frame (Mode $02 PID $02).
 * Result is written as a 5-character DTC string (e.g. "P0300") into buf. */
freeze_frame_status_t freeze_frame_read_trigger_dtc(freeze_frame_t *ff,
                                                     char *buf, size_t len,
                                                     uint32_t timeout_ms);

/* Read a freeze frame parameter PID into a formatted string.
 * Common PIDs: 0x0C RPM, 0x0D speed, 0x05 coolant temp, 0x0F intake temp,
 *              0x04 engine load, 0x10 MAF, 0x11 throttle position.
 * Output format mirrors live_data_format_pid() (e.g. "RPM: 1200 rpm"). */
freeze_frame_status_t freeze_frame_read_pid(freeze_frame_t *ff, uint8_t pid,
                                             char *buf, size_t len,
                                             uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_FREEZE_FRAME_H */
