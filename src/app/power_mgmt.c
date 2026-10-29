/*
 * app/power_mgmt.c — Application layer: ignition sense, display backlight,
 *                    sleep/wake.
 *
 * Implements app/power_mgmt.h on top of Arduino GPIO
 * (pinMode / digitalRead / millis) and ui/display.h backlight control.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "power_mgmt.h"

#include <Arduino.h>

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
power_mgmt_status_t power_mgmt_init(void)
{
    pinMode(IGNITION_SENSE_GPIO, INPUT);
    return POWER_MGMT_OK;
}

bool power_mgmt_ignition_on(void)
{
    return digitalRead(IGNITION_SENSE_GPIO) == HIGH;
}

power_mgmt_status_t power_mgmt_sleep(display_t *disp)
{
    if (disp == NULL) {
        return POWER_MGMT_ERR_INVALID_ARG;
    }
    return (display_backlight(disp, false) == DISPLAY_OK)
               ? POWER_MGMT_OK
               : POWER_MGMT_ERR_INVALID_ARG;
}

power_mgmt_status_t power_mgmt_wake(display_t *disp)
{
    if (disp == NULL) {
        return POWER_MGMT_ERR_INVALID_ARG;
    }
    return (display_backlight(disp, true) == DISPLAY_OK)
               ? POWER_MGMT_OK
               : POWER_MGMT_ERR_INVALID_ARG;
}

power_mgmt_status_t power_mgmt_wait_ignition(uint32_t timeout_ms)
{
    uint32_t start_ms = millis();

    while (!power_mgmt_ignition_on()) {
        if ((uint32_t)(millis() - start_ms) >= timeout_ms) {
            return POWER_MGMT_ERR_TIMEOUT;
        }
    }

    return POWER_MGMT_OK;
}
