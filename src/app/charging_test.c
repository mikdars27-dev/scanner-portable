/*
 * app/charging_test.c — Charging system test implementation.
 *
 * Monitors alternator/regulator performance via the ADC driver: measures
 * battery voltage at engine idle and at revved RPM.
 *
 * Layering: APP -> charging_test_* -> adc_* -> analogRead()
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "charging_test.h"

#include <Arduino.h>
#include <string.h>

#include "ui/warning_screen.h"   /* warning_show, WARN_CUSTOM */
#include "ui/buttons.h"          /* buttons_read, BTN_BACK, buttons_wait_release */

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

#define CHARGING_CMD_TIMEOUT_MS 2000UL   /* per-read ADC timeout */
#define CHARGING_TICK_MS         20UL    /* main loop tick */

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static charging_status_t charging_from_adc(adc_status_t st)
{
    switch (st) {
        case ADC_OK:                    return CHARGING_OK;
        case ADC_ERR_INVALID_ARG:       return CHARGING_ERR_INVALID_ARG;
        case ADC_ERR_NOT_INITIALIZED:   return CHARGING_ERR_NOT_INITIALIZED;
        case ADC_ERR_TIMEOUT:           return CHARGING_ERR_TIMEOUT;
        default:                        return CHARGING_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

struct charging {
    charging_config_t cfg;
    bool               initialized;
};

charging_status_t charging_init(const charging_config_t *cfg,
                                charging_t **out)
{
    if (cfg == NULL || out == NULL || cfg->disp == NULL) {
        return CHARGING_ERR_INVALID_ARG;
    }

    adc_status_t ast = adc_init(&cfg->adc);
    if (ast != ADC_OK) {
        return charging_from_adc(ast);
    }

    charging_t *ch = (charging_t *)calloc(1, sizeof(*ch));
    if (ch == NULL) {
        return CHARGING_ERR_INVALID_ARG;
    }

    ch->cfg         = *cfg;
    ch->initialized = true;

    *out = ch;
    return CHARGING_OK;
}

charging_status_t charging_deinit(charging_t *ch)
{
    if (ch == NULL) {
        return CHARGING_ERR_INVALID_ARG;
    }
    ch->initialized = false;
    adc_deinit();
    free(ch);
    return CHARGING_OK;
}

bool charging_is_initialized(const charging_t *ch)
{
    return ch != NULL && ch->initialized;
}

/* ------------------------------------------------------------------------
 * Helper: average voltage over a duration
 * ------------------------------------------------------------------------ */

/* Sample voltage every CHARGING_TICK_MS for duration_ms and return the
 * average. Allows user abort via BACK button. */
static charging_status_t charging_sample_average(charging_t *ch,
                                                  uint32_t duration_ms,
                                                  float *avg_voltage_v)
{
    if (ch == NULL || avg_voltage_v == NULL || duration_ms == 0) {
        return CHARGING_ERR_INVALID_ARG;
    }

    float sum = 0.0f;
    uint32_t count = 0;
    uint32_t start_ms = millis();
    uint32_t next_sample = start_ms;

    while ((uint32_t)(millis() - start_ms) < duration_ms) {
        uint32_t now = millis();
        if ((int32_t)(now - next_sample) >= 0) {
            next_sample = now + CHARGING_TICK_MS;

            float voltage = 0.0f;
            adc_status_t ast = adc_read_voltage(&voltage);
            if (ast != ADC_OK) {
                return charging_from_adc(ast);
            }

            sum += voltage;
            count++;
        }

        /* Allow user to abort via BACK button. */
        if ((buttons_read() & BTN_BACK) != 0) {
            buttons_wait_release();
            return CHARGING_ERR_USER_ABORT;
        }

        delay(CHARGING_TICK_MS);
        yield();
    }

    if (count == 0) {
        return CHARGING_ERR_TIMEOUT;
    }

    *avg_voltage_v = sum / (float)count;
    return CHARGING_OK;
}

/* ------------------------------------------------------------------------
 * Charging test
 * ------------------------------------------------------------------------ */

charging_status_t charging_run(charging_t *ch, float *idle_voltage_v,
                                float *revved_voltage_v, uint32_t timeout_ms)
{
    if (ch == NULL) return CHARGING_ERR_INVALID_ARG;
    if (!ch->initialized) return CHARGING_ERR_NOT_INITIALIZED;
    if (idle_voltage_v == NULL || revved_voltage_v == NULL || timeout_ms == 0) {
        return CHARGING_ERR_INVALID_ARG;
    }

    /* Split the timeout between idle and revved phases. */
    uint32_t phase_ms = timeout_ms / 2U;
    if (phase_ms < ch->cfg.sample_duration_ms) {
        phase_ms = ch->cfg.sample_duration_ms;
    }

    /* Phase 1: sample at engine idle. */
    charging_status_t st = charging_sample_average(ch, phase_ms, idle_voltage_v);
    if (st != CHARGING_OK) {
        return st;
    }

    /* Phase 2: prompt user to rev the engine, then sample at revved RPM. */
    if (!warning_show(ch->cfg.disp, WARN_CUSTOM,
                      "Putar mesin, tekan SELECT",
                      timeout_ms))
    {
        return CHARGING_ERR_USER_ABORT;
    }

    st = charging_sample_average(ch, phase_ms, revved_voltage_v);
    if (st != CHARGING_OK) {
        return st;
    }

    return CHARGING_OK;
}

charging_health_t charging_classify(float idle_voltage_v,
                                     float revved_voltage_v)
{
    /* Healthy alternator: 13.5–14.5 V both at idle and revved. */
    bool idle_good = (idle_voltage_v >= CHARGING_V_GOOD_MIN &&
                      idle_voltage_v <= CHARGING_V_GOOD_MAX);
    bool revved_good = (revved_voltage_v >= CHARGING_V_GOOD_MIN &&
                        revved_voltage_v <= CHARGING_V_GOOD_MAX);

    if (idle_good && revved_good) {
        return CHARGING_HEALTH_GOOD;
    }

    /* Below 13.0 V: not charging. */
    if (idle_voltage_v < CHARGING_V_LOW_MAX ||
        revved_voltage_v < CHARGING_V_LOW_MAX)
    {
        return CHARGING_HEALTH_LOW;
    }

    /* Above 15.0 V: overcharge (regulator fault). */
    if (idle_voltage_v >= CHARGING_V_HIGH_MIN ||
        revved_voltage_v >= CHARGING_V_HIGH_MIN)
    {
        return CHARGING_HEALTH_HIGH;
    }

    /* Voltage did not rise when revved: alternator not responding. */
    if (revved_voltage_v <= idle_voltage_v + 0.2f) {
        return CHARGING_HEALTH_NO_CHANGE;
    }

    return CHARGING_HEALTH_UNKNOWN;
}
