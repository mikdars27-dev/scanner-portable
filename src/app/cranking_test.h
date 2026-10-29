/*
 * app/cranking_test.h — Cranking / start-up test (Feature 9: Tes Cranking).
 *
 * Monitors battery voltage via the ADC driver while the engine is cranking,
 * capturing the minimum, maximum, and voltage drop. Used to assess battery
 * health under cranking load.
 *
 * Layering: APP -> cranking_test_* -> adc_* -> analogRead()
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_CRANKING_TEST_H
#define APP_CRANKING_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "drivers/adc.h"   /* adc_config_t, adc_* */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    CRANKING_OK                    = 0,
    CRANKING_ERR_INVALID_ARG       = -1,
    CRANKING_ERR_NOT_INITIALIZED   = -2,
    CRANKING_ERR_PROTOCOL          = -3,   /* underlying ADC/driver failure */
    CRANKING_ERR_TIMEOUT           = -4,   /* test timed out */
    CRANKING_ERR_USER_ABORT        = -5,   /* user aborted via button */
    CRANKING_ERR_NOT_SUPPORTED     = -6,
} cranking_status_t;

/* =========================================================================
 * Cranking health verdicts
 * ========================================================================= */

typedef enum {
    CRANKING_HEALTH_UNKNOWN = 0,
    CRANKING_HEALTH_GOOD,    /* min voltage >= 10.5 V */
    CRANKING_HEALTH_MARGINAL,/* 9.0 V <= min voltage < 10.5 V */
    CRANKING_HEALTH_POOR,    /* min voltage < 9.0 V */
} cranking_health_t;

/* Voltage thresholds for cranking health (from plan). */
#define CRANKING_V_GOOD_MIN 10.5f   /* min >= 10.5V: good */
#define CRANKING_V_MARG_MIN 9.0f    /* 9.0V <= min < 10.5V: marginal */

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct cranking cranking_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    adc_config_t  adc;           /* ADC driver config (GPIO, divider, etc.) */
    float         nominal_voltage_v; /* nominal battery voltage (12.0V) */
    uint32_t      poll_interval_ms; /* voltage sample interval during cranking */
} cranking_config_t;

/* Default config (designated initializers). */
#define CRANKING_CONFIG_DEFAULT                                              \
    {                                                                        \
        .adc             = ADC_CONFIG_DEFAULT,                               \
        .nominal_voltage_v = 12.0f,                                          \
        .poll_interval_ms = 100UL,                                           \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the cranking test service. Initializes the ADC driver. */
cranking_status_t cranking_init(const cranking_config_t *cfg,
                                cranking_t **out);

/* Release the cranking test handle and deinitialize the ADC driver. */
cranking_status_t cranking_deinit(cranking_t *ct);

/* True if cranking_init() was called successfully. */
bool cranking_is_initialized(const cranking_t *ct);

/* =========================================================================
 * Cranking test
 * ========================================================================= */

/* Run the cranking test for timeout_ms.
 *
 * Samples battery voltage every poll_interval_ms and records the minimum,
 * maximum, and voltage drop (max - min). The user cranks the engine during
 * this window; the voltage sag during cranking is captured in min_voltage_v
 * and drop_voltage_v.
 *
 * Returns CRANKING_OK with min/max/drop filled, or CRANKING_ERR_TIMEOUT if
 * the test window expires.
 */
cranking_status_t cranking_run(cranking_t *ct, float *min_voltage_v,
                                float *max_voltage_v, float *drop_voltage_v,
                                uint32_t timeout_ms);

/* Classify cranking health based on the minimum voltage observed during
 * cranking. */
cranking_health_t cranking_classify(float min_voltage_v);

#ifdef __cplusplus
}
#endif

#endif /* APP_CRANKING_TEST_H */
