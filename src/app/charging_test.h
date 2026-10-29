/*
 * app/charging_test.h — Charging system test (Feature 10: Tes Pengisian Daya).
 *
 * Monitors alternator/regulator performance via the ADC driver: measures
 * battery voltage at engine idle and at revved RPM. A healthy alternator
 * produces 13.5–14.5 V with the engine running.
 *
 * Layering: APP -> charging_test_* -> adc_* -> analogRead()
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_CHARGING_TEST_H
#define APP_CHARGING_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "drivers/adc.h"    /* adc_config_t, adc_* */
#include "ui/display.h"     /* display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    CHARGING_OK                    = 0,
    CHARGING_ERR_INVALID_ARG       = -1,
    CHARGING_ERR_NOT_INITIALIZED   = -2,
    CHARGING_ERR_PROTOCOL          = -3,   /* underlying ADC/driver failure */
    CHARGING_ERR_TIMEOUT           = -4,   /* test timed out */
    CHARGING_ERR_USER_ABORT        = -5,   /* user aborted via button */
    CHARGING_ERR_NOT_SUPPORTED     = -6,
} charging_status_t;

/* =========================================================================
 * Charging health verdicts
 * ========================================================================= */

typedef enum {
    CHARGING_HEALTH_UNKNOWN  = 0,
    CHARGING_HEALTH_GOOD,    /* 13.5–14.5 V both idle and revved */
    CHARGING_HEALTH_LOW,     /* < 13.0 V — alternator not charging */
    CHARGING_HEALTH_HIGH,    /* > 15.0 V — regulator overcharging */
    CHARGING_HEALTH_NO_CHANGE, /* voltage did not rise when revved */
} charging_health_t;

/* Voltage thresholds for charging health (from plan). */
#define CHARGING_V_GOOD_MIN 13.5f   /* healthy charging range minimum */
#define CHARGING_V_GOOD_MAX 14.5f   /* healthy charging range maximum */
#define CHARGING_V_LOW_MAX  13.0f   /* below this: not charging */
#define CHARGING_V_HIGH_MIN 15.0f   /* above this: overcharge */

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct charging charging_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    display_t    *disp;          /* display for the rev-engine prompt */
    adc_config_t  adc;           /* ADC driver config (GPIO, divider, etc.) */
    float         nominal_voltage_v; /* nominal battery voltage (12.0V) */
    uint32_t      sample_duration_ms; /* ms to sample each phase */
} charging_config_t;

/* Default config (designated initializers). */
#define CHARGING_CONFIG_DEFAULT                                             \
    {                                                                       \
        .disp             = NULL,                                           \
        .adc              = ADC_CONFIG_DEFAULT,                             \
        .nominal_voltage_v = 12.0f,                                         \
        .sample_duration_ms = 3000UL,                                       \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the charging test service. Initializes the ADC driver. */
charging_status_t charging_init(const charging_config_t *cfg,
                                charging_t **out);

/* Release the charging test handle and deinitialize the ADC driver. */
charging_status_t charging_deinit(charging_t *ch);

/* True if charging_init() was called successfully. */
bool charging_is_initialized(const charging_t *ch);

/* =========================================================================
 * Charging test
 * ========================================================================= */

/* Run the charging test for timeout_ms.
 *
 * Phase 1: sample battery voltage at engine idle for sample_duration_ms.
 * Phase 2: prompt the user to rev the engine (warning screen), then sample
 *          voltage at revved RPM for sample_duration_ms.
 *
 * Returns CHARGING_OK with idle_voltage_v and revved_voltage_v filled.
 */
charging_status_t charging_run(charging_t *ch, float *idle_voltage_v,
                                float *revved_voltage_v, uint32_t timeout_ms);

/* Classify charging health based on idle and revved voltages. */
charging_health_t charging_classify(float idle_voltage_v,
                                     float revved_voltage_v);

#ifdef __cplusplus
}
#endif

#endif /* APP_CHARGING_TEST_H */
