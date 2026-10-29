/*
 * app/power_mgmt.h — Application layer: ignition sense, display backlight,
 *                    sleep/wake.
 *
 * Reads the ignition-sense input (PC817 optocoupler on IGNITION_SENSE_GPIO),
 * switches the TFT backlight through display_backlight(), and blocks until
 * the ignition is on with an optional timeout.
 *
 * Layering: APP -> power_mgmt_* -> ui/display_* + Arduino GPIO
 *           (pinMode / digitalRead / millis)
 */

#ifndef APP_POWER_MGMT_H
#define APP_POWER_MGMT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "ui/display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    POWER_MGMT_OK               = 0,
    POWER_MGMT_ERR_INVALID_ARG  = -1,
    POWER_MGMT_ERR_TIMEOUT      = -2,
} power_mgmt_status_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Configure IGNITION_SENSE_GPIO as an input. Always returns POWER_MGMT_OK. */
power_mgmt_status_t power_mgmt_init(void);

/* True when the ignition sense input reads active (ignition on). */
bool power_mgmt_ignition_on(void);

/* Suspend the display: turn the backlight off. disp must not be NULL. */
power_mgmt_status_t power_mgmt_sleep(display_t *disp);

/* Wake the display: turn the backlight on. disp must not be NULL. */
power_mgmt_status_t power_mgmt_wake(display_t *disp);

/* Block until the ignition is on, or until timeout_ms elapses.
 * Returns POWER_MGMT_OK when the ignition is on,
 * POWER_MGMT_ERR_TIMEOUT when the deadline passes first.
 * timeout_ms = 0 returns immediately (timeout unless ignition is already on). */
power_mgmt_status_t power_mgmt_wait_ignition(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_POWER_MGMT_H */
