/*
 * drivers/adc.c — ADC driver implementation for ESP32 DevKit V4.
 *
 * Backend: Arduino analogRead() on ESP32 ADC1 pins.
 * Toolchain note: C++ (Arduino). Compile as C++ (.cpp or -x c++).
 */

#include "adc.h"

#include <Arduino.h>

/* ------------------------------------------------------------------------
 * Static state (singleton — ESP32 ADC is a shared peripheral)
 * ------------------------------------------------------------------------ */
static bool          g_adc_initialized = false;
static adc_config_t  g_adc_cfg;

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
adc_status_t adc_init(const adc_config_t *cfg)
{
    if (cfg == NULL) {
        return ADC_ERR_INVALID_ARG;
    }
    if (cfg->gpio_pin < 0 || cfg->gpio_pin > 39) {
        return ADC_ERR_INVALID_ARG;
    }
    if (cfg->sample_count == 0) {
        return ADC_ERR_INVALID_ARG;
    }
    if (cfg->vref_mv == 0.0f) {
        return ADC_ERR_INVALID_ARG;
    }
    if (cfg->divider_ratio == 0.0f) {
        return ADC_ERR_INVALID_ARG;
    }

    pinMode(cfg->gpio_pin, INPUT);

    g_adc_cfg     = *cfg;
    g_adc_initialized = true;

    return ADC_OK;
}

adc_status_t adc_deinit(void)
{
    g_adc_initialized = false;
    return ADC_OK;
}

bool adc_is_initialized(void)
{
    return g_adc_initialized;
}

/* ------------------------------------------------------------------------
 * Read
 * ------------------------------------------------------------------------ */
adc_status_t adc_read_raw(uint32_t *raw)
{
    if (!g_adc_initialized) {
        return ADC_ERR_NOT_INITIALIZED;
    }
    if (raw == NULL) {
        return ADC_ERR_INVALID_ARG;
    }

    uint32_t sum = 0;
    for (uint32_t i = 0; i < g_adc_cfg.sample_count; i++) {
        sum += (uint32_t)analogRead(g_adc_cfg.gpio_pin);
        if (i + 1 < g_adc_cfg.sample_count) {
            delay(g_adc_cfg.sample_interval_ms);
        }
    }

    *raw = sum / g_adc_cfg.sample_count;
    return ADC_OK;
}

adc_status_t adc_read_voltage_mv(float *voltage_mv)
{
    if (!g_adc_initialized) {
        return ADC_ERR_NOT_INITIALIZED;
    }
    if (voltage_mv == NULL) {
        return ADC_ERR_INVALID_ARG;
    }

    uint32_t raw;
    adc_status_t st = adc_read_raw(&raw);
    if (st != ADC_OK) {
        return st;
    }

    /* ESP32 ADC is 12-bit: 0-4095 maps to 0-vref_mv */
    float adc_mv = (float)raw * g_adc_cfg.vref_mv / 4095.0f;
    *voltage_mv  = adc_mv * g_adc_cfg.divider_ratio;

    return ADC_OK;
}

adc_status_t adc_read_voltage(float *voltage_v)
{
    if (!g_adc_initialized) {
        return ADC_ERR_NOT_INITIALIZED;
    }
    if (voltage_v == NULL) {
        return ADC_ERR_INVALID_ARG;
    }

    float mv;
    adc_status_t st = adc_read_voltage_mv(&mv);
    if (st != ADC_OK) {
        return st;
    }

    *voltage_v = mv / 1000.0f;
    return ADC_OK;
}
