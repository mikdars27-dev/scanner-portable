/*
 * drivers/adc.h — ADC driver for ESP32 DevKit V4 (Arduino-ESP32).
 *
 * Reads analog voltage on ADC1 pins (GPIO 35-39, input-only). Used by
 * battery_test, cranking_test, and charging_test features.
 *
 * Layering: APP -> battery/cranking/charging -> adc_* -> analogRead()
 *
 * The ESP32 ADC is 12-bit (0-4095) with a default reference of ~3.3 V.
 * An external voltage divider is required for battery voltage > 3.3 V.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (.cpp or -x c++).
 */

#ifndef DRIVERS_ADC_H
#define DRIVERS_ADC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    ADC_OK                    = 0,
    ADC_ERR_INVALID_ARG       = -1,
    ADC_ERR_NOT_INITIALIZED   = -2,
    ADC_ERR_TIMEOUT           = -3,
} adc_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef struct {
    int         gpio_pin;            /* ADC pin (35-39 recommended, ADC1 input-only) */
    uint32_t    sample_count;        /* number of samples to average */
    uint32_t    sample_interval_ms;  /* ms between samples */
    float       vref_mv;             /* reference voltage in mV (default 3300) */
    float       divider_ratio;       /* voltage divider ratio (V_in / V_adc) */
} adc_config_t;

typedef struct adc adc_t;            /* opaque handle */

/* Default config: GPIO 35, 16 samples, 5 ms interval, 3.3 V ref, 11:1 divider. */
#define ADC_CONFIG_DEFAULT                                             \
    {                                                                  \
        .gpio_pin           = ADC_DEFAULT_GPIO,                        \
        .sample_count       = ADC_SAMPLE_COUNT,                        \
        .sample_interval_ms = ADC_SAMPLE_INTERVAL_MS,                  \
        .vref_mv            = ADC_VREF_MV,                             \
        .divider_ratio      = ADC_DIVIDER_RATIO,                       \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the ADC driver. Validates GPIO pin and config values. */
adc_status_t adc_init(const adc_config_t *cfg);

/* Release the ADC driver. */
adc_status_t adc_deinit(void);

/* True if adc_init() has been called successfully. */
bool adc_is_initialized(void);

/* =========================================================================
 * Read
 * ========================================================================= */

/* Read raw ADC value (0-4095 for 12-bit ADC), averaged over sample_count. */
adc_status_t adc_read_raw(uint32_t *raw);

/* Read voltage in mV, applying divider ratio. */
adc_status_t adc_read_voltage_mv(float *voltage_mv);

/* Read voltage in V, applying divider ratio. */
adc_status_t adc_read_voltage(float *voltage_v);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_ADC_H */
