/*
 * app/battery_test.h — Battery testing (Feature 8: Deteksi Baterai).
 *
 * Measures battery voltage via the ADC driver (GPIO 35-39, ADC1 input-only,
 * external voltage divider), tests internal resistance (open circuit vs
 * loaded voltage), and estimates Cold Cranking Amps (CCA).
 *
 * Layering: APP -> battery_test_* -> adc_* -> analogRead()
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_BATTERY_TEST_H
#define APP_BATTERY_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "drivers/adc.h"   /* adc_config_t, adc_* */
#include "ui/display.h"    /* display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    BATTERY_OK                    = 0,
    BATTERY_ERR_INVALID_ARG       = -1,
    BATTERY_ERR_NOT_INITIALIZED   = -2,
    BATTERY_ERR_PROTOCOL          = -3,   /* underlying ADC/driver failure */
    BATTERY_ERR_TIMEOUT           = -4,   /* user timeout during test */
    BATTERY_ERR_USER_ABORT        = -5,   /* user aborted via button */
    BATTERY_ERR_SAFETY            = -6,   /* safety condition not met */
    BATTERY_ERR_NOT_SUPPORTED     = -7,
} battery_status_t;

/* =========================================================================
 * Battery health verdicts
 * ========================================================================= */

typedef enum {
    BATTERY_HEALTH_UNKNOWN   = 0,
    BATTERY_HEALTH_GOOD,    /* voltage and resistance within spec */
    BATTERY_HEALTH_MARGINAL,/* borderline, recommend replacement soon */
    BATTERY_HEALTH_POOR,    /* weak battery, replace recommended */
    BATTERY_HEALTH_BAD,     /* failed, replace immediately */
} battery_health_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct battery battery_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    display_t    *disp;          /* display for user prompts during tests */
    adc_config_t  adc;           /* ADC driver config (GPIO, divider, etc.) */
    float         nominal_voltage_v; /* nominal battery voltage (12.0V car) */
    float         cca_rating_a;  /* CCA rating for reference (0 = unknown) */
    float         load_current_a;/* known load current for resistance test */
} battery_config_t;

/* Default config (designated initializers). */
#define BATTERY_CONFIG_DEFAULT                                               \
    {                                                                        \
        .disp             = NULL,                                            \
        .adc              = ADC_CONFIG_DEFAULT,                              \
        .nominal_voltage_v = 12.0f,                                          \
        .cca_rating_a     = 0.0f,                                            \
        .load_current_a   = 0.0f,                                            \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the battery test service. Initializes the ADC driver.
 * cfg->adc.gpio_pin must be a valid ADC1 pin (35-39). */
battery_status_t battery_init(const battery_config_t *cfg, battery_t **out);

/* Release the battery test handle and deinitialize the ADC driver. */
battery_status_t battery_deinit(battery_t *bt);

/* True if battery_init() was called successfully. */
bool battery_is_initialized(const battery_t *bt);

/* =========================================================================
 * Battery measurements
 * ========================================================================= */

/* Read battery voltage in volts.
 * Uses the ADC driver with the configured voltage divider ratio. */
battery_status_t battery_read_voltage(battery_t *bt, float *voltage_v,
                                       uint32_t timeout_ms);

/* Test internal resistance.
 *
 * Measures open-circuit voltage, then prompts the user (via display) to
 * apply a known load and press SELECT. Measures loaded voltage and computes
 * R = (V_oc - V_load) / I_load using cfg->load_current_a.
 *
 * Returns BATTERY_OK with *ohm filled, BATTERY_ERR_USER_ABORT if the user
 * cancels, or BATTERY_ERR_TIMEOUT if SELECT is not pressed in time.
 *
 * Requires cfg->load_current_a > 0; otherwise returns BATTERY_ERR_INVALID_ARG. */
battery_status_t battery_test_internal_resistance(battery_t *bt, float *ohm,
                                                   uint32_t timeout_ms);

/* Estimate Cold Cranking Amps (CCA).
 *
 * Reads resting voltage and estimates CCA based on the configured
 * cca_rating_a and voltage level:
 *   - V >= 12.6V:  ~100% of rated CCA
 *   - 12.4-12.6V:  ~75%
 *   - 12.2-12.4V:  ~50%
 *   - < 12.2V:     ~25% or less (discharged)
 *
 * Returns BATTERY_OK with *cca filled, or an error status. */
battery_status_t battery_test_cca(battery_t *bt, int *cca, uint32_t timeout_ms);

/* Classify battery health based on voltage and resistance.
 * Call after battery_read_voltage() and/or battery_test_internal_resistance(). */
battery_health_t battery_classify_health(float voltage_v, float internal_resistance_ohm,
                                          float nominal_voltage_v);

#ifdef __cplusplus
}
#endif

#endif /* APP_BATTERY_TEST_H */
