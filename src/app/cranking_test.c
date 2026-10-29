/*
 * app/cranking_test.c — Cranking / start-up test implementation.
 *
 * Monitors battery voltage via the ADC driver while the engine is cranking,
 * capturing the minimum, maximum, and voltage drop.
 *
 * Layering: APP -> cranking_test_* -> adc_* -> analogRead()
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "cranking_test.h"

#include <Arduino.h>
#include <string.h>

#include "ui/buttons.h"   /* buttons_read, BTN_BACK */

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

#define CRANKING_CMD_TIMEOUT_MS 2000UL   /* per-read ADC timeout */
#define CRANKING_TICK_MS         20UL    /* main loop tick */

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static cranking_status_t cranking_from_adc(adc_status_t st)
{
    switch (st) {
        case ADC_OK:                    return CRANKING_OK;
        case ADC_ERR_INVALID_ARG:       return CRANKING_ERR_INVALID_ARG;
        case ADC_ERR_NOT_INITIALIZED:   return CRANKING_ERR_NOT_INITIALIZED;
        case ADC_ERR_TIMEOUT:           return CRANKING_ERR_TIMEOUT;
        default:                        return CRANKING_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

struct cranking {
    cranking_config_t cfg;
    bool               initialized;
};

cranking_status_t cranking_init(const cranking_config_t *cfg,
                                cranking_t **out)
{
    if (cfg == NULL || out == NULL) {
        return CRANKING_ERR_INVALID_ARG;
    }

    adc_status_t ast = adc_init(&cfg->adc);
    if (ast != ADC_OK) {
        return cranking_from_adc(ast);
    }

    cranking_t *ct = (cranking_t *)calloc(1, sizeof(*ct));
    if (ct == NULL) {
        return CRANKING_ERR_INVALID_ARG;
    }

    ct->cfg         = *cfg;
    ct->initialized = true;

    *out = ct;
    return CRANKING_OK;
}

cranking_status_t cranking_deinit(cranking_t *ct)
{
    if (ct == NULL) {
        return CRANKING_ERR_INVALID_ARG;
    }
    ct->initialized = false;
    adc_deinit();
    free(ct);
    return CRANKING_OK;
}

bool cranking_is_initialized(const cranking_t *ct)
{
    return ct != NULL && ct->initialized;
}

/* ------------------------------------------------------------------------
 * Cranking test
 * ------------------------------------------------------------------------ */

cranking_status_t cranking_run(cranking_t *ct, float *min_voltage_v,
                                float *max_voltage_v, float *drop_voltage_v,
                                uint32_t timeout_ms)
{
    if (ct == NULL) return CRANKING_ERR_INVALID_ARG;
    if (!ct->initialized) return CRANKING_ERR_NOT_INITIALIZED;
    if (min_voltage_v == NULL || max_voltage_v == NULL || drop_voltage_v == NULL ||
        timeout_ms == 0)
    {
        return CRANKING_ERR_INVALID_ARG;
    }

    /* Take an initial reading (resting voltage, likely the max). */
    float voltage = 0.0f;
    adc_status_t ast = adc_read_voltage(&voltage);
    if (ast != ADC_OK) {
        return cranking_from_adc(ast);
    }

    float min_v = voltage;
    float max_v = voltage;

    uint32_t start_ms = millis();
    uint32_t next_sample = start_ms;

    for (;;) {
        uint32_t now = millis();
        if ((uint32_t)(now - start_ms) >= timeout_ms) {
            break;
        }

        if ((int32_t)(now - next_sample) >= 0) {
            next_sample = now + ct->cfg.poll_interval_ms;

            ast = adc_read_voltage(&voltage);
            if (ast != ADC_OK) {
                return cranking_from_adc(ast);
            }

            if (voltage < min_v) min_v = voltage;
            if (voltage > max_v) max_v = voltage;
        }

        /* Allow user to abort via BACK button. */
        if ((buttons_read() & BTN_BACK) != 0) {
            buttons_wait_release();
            return CRANKING_ERR_USER_ABORT;
        }

        delay(CRANKING_TICK_MS);
        yield();
    }

    *min_voltage_v = min_v;
    *max_voltage_v = max_v;
    *drop_voltage_v = max_v - min_v;

    return CRANKING_OK;
}

cranking_health_t cranking_classify(float min_voltage_v)
{
    if (min_voltage_v >= CRANKING_V_GOOD_MIN) {
        return CRANKING_HEALTH_GOOD;
    }
    if (min_voltage_v >= CRANKING_V_MARG_MIN) {
        return CRANKING_HEALTH_MARGINAL;
    }
    return CRANKING_HEALTH_POOR;
}
