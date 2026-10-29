/*
 * app/battery_test.c — Battery testing implementation (Feature 8: Deteksi Baterai).
 *
 * Measures battery voltage via the ADC driver (GPIO 35-39, ADC1 input-only,
 * external voltage divider), tests internal resistance (open circuit vs
 * loaded voltage), and estimates Cold Cranking Amps (CCA).
 *
 * Layering: APP -> battery_test_* -> adc_* -> analogRead()
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "battery_test.h"

#include <Arduino.h>
#include <string.h>

#include "ui/warning_screen.h"   /* warning_show, WARN_CUSTOM */

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

#define BATTERY_CMD_TIMEOUT_MS 2000UL   /* per-read ADC timeout */

/* Internal resistance thresholds (ohm) for health classification. */
#define BATTERY_R_GOOD_MAX  0.05f
#define BATTERY_R_MARG_MAX  0.10f
#define BATTERY_R_POOR_MAX  0.20f

/* Voltage ratio thresholds (V / nominal) for health classification. */
#define BATTERY_V_GOOD_MIN  0.95f
#define BATTERY_V_MARG_MIN  0.90f
#define BATTERY_V_POOR_MIN  0.85f

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static battery_status_t battery_from_adc(adc_status_t st)
{
    switch (st) {
        case ADC_OK:                    return BATTERY_OK;
        case ADC_ERR_INVALID_ARG:       return BATTERY_ERR_INVALID_ARG;
        case ADC_ERR_NOT_INITIALIZED:   return BATTERY_ERR_NOT_INITIALIZED;
        case ADC_ERR_TIMEOUT:           return BATTERY_ERR_TIMEOUT;
        default:                        return BATTERY_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

struct battery {
    battery_config_t cfg;
    bool              initialized;
};

battery_status_t battery_init(const battery_config_t *cfg, battery_t **out)
{
    if (cfg == NULL || out == NULL || cfg->disp == NULL) {
        return BATTERY_ERR_INVALID_ARG;
    }

    adc_status_t ast = adc_init(&cfg->adc);
    if (ast != ADC_OK) {
        return battery_from_adc(ast);
    }

    battery_t *bt = (battery_t *)calloc(1, sizeof(*bt));
    if (bt == NULL) {
        return BATTERY_ERR_INVALID_ARG;
    }

    bt->cfg         = *cfg;
    bt->initialized = true;

    *out = bt;
    return BATTERY_OK;
}

battery_status_t battery_deinit(battery_t *bt)
{
    if (bt == NULL) {
        return BATTERY_ERR_INVALID_ARG;
    }
    bt->initialized = false;
    adc_deinit();
    free(bt);
    return BATTERY_OK;
}

bool battery_is_initialized(const battery_t *bt)
{
    return bt != NULL && bt->initialized;
}

/* ------------------------------------------------------------------------
 * Battery measurements
 * ------------------------------------------------------------------------ */

battery_status_t battery_read_voltage(battery_t *bt, float *voltage_v,
                                       uint32_t timeout_ms)
{
    (void)timeout_ms; /* ADC read uses its own internal sampling timing */

    if (bt == NULL) return BATTERY_ERR_INVALID_ARG;
    if (!bt->initialized) return BATTERY_ERR_NOT_INITIALIZED;
    if (voltage_v == NULL) return BATTERY_ERR_INVALID_ARG;

    float voltage_mv = 0.0f;
    adc_status_t ast = adc_read_voltage_mv(&voltage_mv);
    if (ast != ADC_OK) {
        return battery_from_adc(ast);
    }

    *voltage_v = voltage_mv / 1000.0f;
    return BATTERY_OK;
}

battery_status_t battery_test_internal_resistance(battery_t *bt, float *ohm,
                                                   uint32_t timeout_ms)
{
    if (bt == NULL) return BATTERY_ERR_INVALID_ARG;
    if (!bt->initialized) return BATTERY_ERR_NOT_INITIALIZED;
    if (ohm == NULL) return BATTERY_ERR_INVALID_ARG;
    if (timeout_ms == 0) return BATTERY_ERR_INVALID_ARG;
    if (bt->cfg.load_current_a <= 0.0f) {
        return BATTERY_ERR_INVALID_ARG;
    }

    /* Step 1: measure open-circuit voltage. */
    float v_oc = 0.0f;
    battery_status_t st = battery_read_voltage(bt, &v_oc, BATTERY_CMD_TIMEOUT_MS);
    if (st != BATTERY_OK) {
        return st;
    }

    /* Step 2: prompt the user to apply a known load and press SELECT. */
    if (!warning_show(bt->cfg.disp, WARN_CUSTOM,
                      "Terapkan beban, tekan SELECT",
                      timeout_ms))
    {
        return BATTERY_ERR_USER_ABORT;
    }

    /* Step 3: measure loaded voltage. */
    float v_load = 0.0f;
    st = battery_read_voltage(bt, &v_load, BATTERY_CMD_TIMEOUT_MS);
    if (st != BATTERY_OK) {
        return st;
    }

    /* R = (V_oc - V_load) / I_load */
    *ohm = (v_oc - v_load) / bt->cfg.load_current_a;
    return BATTERY_OK;
}

battery_status_t battery_test_cca(battery_t *bt, int *cca, uint32_t timeout_ms)
{
    (void)timeout_ms; /* CCA estimate uses a single voltage reading */

    if (bt == NULL) return BATTERY_ERR_INVALID_ARG;
    if (!bt->initialized) return BATTERY_ERR_NOT_INITIALIZED;
    if (cca == NULL) return BATTERY_ERR_INVALID_ARG;

    float voltage = 0.0f;
    battery_status_t st = battery_read_voltage(bt, &voltage, BATTERY_CMD_TIMEOUT_MS);
    if (st != BATTERY_OK) {
        return st;
    }

    /* Estimate CCA as a fraction of rated CCA based on resting voltage. */
    float ratio;
    if (voltage >= 12.6f) {
        ratio = 1.00f;
    } else if (voltage >= 12.4f) {
        ratio = 0.75f;
    } else if (voltage >= 12.2f) {
        ratio = 0.50f;
    } else {
        ratio = 0.25f;
    }

    *cca = (int)(bt->cfg.cca_rating_a * ratio);
    return BATTERY_OK;
}

battery_health_t battery_classify_health(float voltage_v, float internal_resistance_ohm,
                                          float nominal_voltage_v)
{
    if (nominal_voltage_v <= 0.0f) {
        return BATTERY_HEALTH_UNKNOWN;
    }

    float v_ratio = voltage_v / nominal_voltage_v;

    if (v_ratio >= BATTERY_V_GOOD_MIN &&
        internal_resistance_ohm < BATTERY_R_GOOD_MAX)
    {
        return BATTERY_HEALTH_GOOD;
    }
    if (v_ratio >= BATTERY_V_MARG_MIN &&
        internal_resistance_ohm < BATTERY_R_MARG_MAX)
    {
        return BATTERY_HEALTH_MARGINAL;
    }
    if (v_ratio >= BATTERY_V_POOR_MIN &&
        internal_resistance_ohm < BATTERY_R_POOR_MAX)
    {
        return BATTERY_HEALTH_POOR;
    }
    return BATTERY_HEALTH_BAD;
}
