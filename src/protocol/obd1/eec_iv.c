/*
 * protocol/obd1/eec_iv.c — Ford EEC-IV OBD1 self-test implementation.
 *
 * Decodes VPW (Variable Pulse Width) self-test pulses from the STO pin or
 * Check Engine Light. The ECU first outputs a "sweep" (long pulse) to signal
 * test start, then outputs code digits as pulse groups:
 *   digit N = N pulses separated by short gaps,
 *   digits separated by a longer gap,
 *   codes separated by an even longer gap.
 *
 * Example: code 34 = pulse,pulse,pulse [long gap] pulse,pulse,pulse,pulse.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "eec_iv.h"

#include <Arduino.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct eec_iv {
    eec_iv_config_t cfg;
    bool            initialized;

    typedef enum {
        ST_IDLE,         /* waiting for sweep / test start */
        ST_SWEEP,        /* sweep pulse detected, test active */
        ST_CODE_PULSE,   /* inside a code digit pulse group */
        ST_CODE_GAP,     /* gap between pulses/digits/codes */
        ST_CODE_DONE,    /* a full code decoded, awaiting read/reset */
        ST_SWEEP_DONE,   /* sweep complete, no more codes (pass) */
    } state_t;

    state_t       state;
    uint32_t      t_edge;       /* millis() at last level change */
    bool          level_on;     /* current latched active level */
    uint8_t       pulse_count;  /* pulses in current digit */
    uint8_t       digit_count;  /* digits in current code */
    uint8_t       digits[3];    /* decoded digits */
    uint32_t      total_codes;  /* codes detected since init/reset */
    bool          sweep_seen;   /* true once sweep pulse detected */
    eec_iv_code_t last_code;    /* last completed code, cached for poll() */
};

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static bool eec_level_active(const eec_iv_t *eec)
{
    bool level = digitalRead(eec->cfg.gpio_pin) == HIGH;
    return eec->cfg.active_level ? level : !level;
}

/* Classify a pulse width: 0=too short, 1=code pulse, 2=sweep (long), 3=too long. */
static uint8_t eec_classify_pulse(const eec_iv_t *eec, uint32_t ms)
{
    if (ms < 40UL) {
        return 0; /* noise */
    }
    if (ms >= eec->cfg.sweep_pulse_ms * 3UL / 4UL &&
        ms <= eec->cfg.sweep_pulse_ms * 2UL)
    {
        return 2; /* sweep pulse */
    }
    if (ms >= eec->cfg.code_pulse_ms / 2UL && ms <= eec->cfg.code_pulse_ms * 2UL) {
        return 1; /* code pulse */
    }
    return 3; /* too long */
}

static uint8_t eec_classify_gap(const eec_iv_t *eec, uint32_t ms)
{
    if (ms < eec->cfg.inter_pulse_gap_ms) {
        return 0; /* same digit */
    }
    if (ms < eec->cfg.inter_digit_gap_ms) {
        return 1; /* digit boundary */
    }
    return 2; /* code boundary / end */
}

static void eec_assemble_code(eec_iv_t *eec)
{
    uint16_t value = 0;
    for (uint8_t i = 0; i < eec->digit_count; i++) {
        value = (uint16_t)(value * 10 + eec->digits[i]);
    }
    eec->code_value = value;

    char *p = eec->code_str;
    for (uint8_t i = 0; i < eec->digit_count; i++) {
        *p++ = (char)('0' + eec->digits[i]);
    }
    *p = '\0';
}

static void eec_cache_code(eec_iv_t *eec, eec_iv_mode_t mode)
{
    eec->last_code.mode        = mode;
    eec->last_code.digit_count = eec->digit_count;
    memcpy(eec->last_code.digits, eec->digits, sizeof(eec->digits));
    eec->last_code.code_value  = eec->code_value;
    snprintf(eec->last_code.code_str, sizeof(eec->last_code.code_str), "%s",
             eec->code_str);
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

eec_iv_status_t eec_iv_init(const eec_iv_config_t *cfg, eec_iv_t **out_eec)
{
    if (cfg == NULL || out_eec == NULL) {
        return EEC_IV_ERR_INVALID_ARG;
    }
    if (cfg->gpio_pin < 0) {
        return EEC_IV_ERR_INVALID_ARG;
    }
    if (cfg->code_pulse_ms == 0 || cfg->sweep_pulse_ms <= cfg->code_pulse_ms) {
        return EEC_IV_ERR_INVALID_ARG;
    }
    if (cfg->inter_pulse_gap_ms == 0 ||
        cfg->inter_digit_gap_ms <= cfg->inter_pulse_gap_ms ||
        cfg->inter_code_gap_ms <= cfg->inter_digit_gap_ms)
    {
        return EEC_IV_ERR_INVALID_ARG;
    }
    if (cfg->timeout_ms == 0) {
        return EEC_IV_ERR_INVALID_ARG;
    }

    eec_iv_t *eec = (eec_iv_t *)calloc(1, sizeof(*eec));
    if (eec == NULL) {
        return EEC_IV_ERR_INVALID_ARG;
    }

    eec->cfg          = *cfg;
    eec->state        = ST_IDLE;
    eec->t_edge       = 0;
    eec->level_on     = false;
    eec->pulse_count  = 0;
    eec->digit_count  = 0;
    eec->sweep_seen   = false;
    eec->total_codes  = 0;
    memset(eec->digits, 0, sizeof(eec->digits));
    memset(&eec->last_code, 0, sizeof(eec->last_code));

    pinMode(cfg->gpio_pin, INPUT);

    eec->initialized = true;
    *out_eec = eec;
    return EEC_IV_OK;
}

eec_iv_status_t eec_iv_deinit(eec_iv_t *eec)
{
    if (eec == NULL) {
        return EEC_IV_OK;
    }
    if (!eec->initialized) {
        return EEC_IV_ERR_NOT_INITIALIZED;
    }

    free(eec);
    return EEC_IV_OK;
}

bool eec_iv_is_initialized(const eec_iv_t *eec)
{
    return eec != NULL && eec->initialized;
}

eec_iv_status_t eec_iv_reset(eec_iv_t *eec)
{
    if (eec == NULL || !eec->initialized) {
        return EEC_IV_ERR_NOT_INITIALIZED;
    }

    eec->state       = ST_IDLE;
    eec->t_edge      = 0;
    eec->level_on    = false;
    eec->pulse_count = 0;
    eec->digit_count = 0;
    eec->sweep_seen  = false;
    memset(eec->digits, 0, sizeof(eec->digits));
    memset(&eec->last_code, 0, sizeof(eec->last_code));
    return EEC_IV_OK;
}

uint32_t eec_iv_code_count(const eec_iv_t *eec)
{
    if (eec == NULL || !eec->initialized) {
        return 0;
    }
    return eec->total_codes;
}

eec_iv_status_t eec_iv_poll(eec_iv_t *eec, eec_iv_code_t *out_code)
{
    if (eec == NULL || !eec->initialized) {
        return EEC_IV_ERR_INVALID_ARG;
    }

    /* If a code or sweep-done is already reported, keep returning it. */
    if (eec->state == ST_CODE_DONE) {
        if (out_code != NULL) {
            *out_code = eec->last_code;
        }
        return EEC_IV_OK;
    }
    if (eec->state == ST_SWEEP_DONE) {
        if (out_code != NULL) {
            memset(out_code, 0, sizeof(*out_code));
            out_code->digit_count = 0;
            out_code->code_value  = 0;
            out_code->code_str[0] = '\0';
        }
        return EEC_IV_ERR_NO_CODES;
    }

    if (out_code != NULL) {
        memset(out_code, 0, sizeof(*out_code));
    }

    bool   level_now = eec_level_active(eec);
    uint32_t now     = millis();

    if (eec->t_edge == 0) {
        eec->t_edge   = now;
        eec->level_on = level_now;
        return EEC_IV_ERR_NO_PATTERN;
    }

    uint32_t elapsed = (uint32_t)(now - eec->t_edge);

    switch (eec->state) {
    case ST_IDLE:
        /* Waiting for sweep pulse to start the test. */
        if (level_now) {
            uint8_t cls = eec_classify_pulse(eec, elapsed);
            if (cls == 2) {
                eec->sweep_seen = true;
                eec->state = ST_SWEEP;
                eec->t_edge = now;
                eec->level_on = true;
            } else if (cls == 1) {
                /* Code pulse directly (no sweep): start digit collection. */
                eec->state = ST_CODE_PULSE;
                eec->t_edge = now;
                eec->level_on = true;
                eec->pulse_count = 1;
            }
        }
        return EEC_IV_ERR_NO_PATTERN;

    case ST_SWEEP:
        /* Sweep pulse is high; waiting for it to go low. */
        if (!level_now) {
            /* Sweep ended. Test is now active; wait for first code pulse. */
            eec->state = ST_CODE_GAP;
            eec->t_edge = now;
            eec->level_on = false;
        } else if (elapsed > eec->cfg.sweep_pulse_ms * 3UL) {
            /* Sweep stuck HIGH too long — abort. */
            eec->state = ST_IDLE;
            eec->t_edge = now;
            eec->level_on = true;
        }
        return EEC_IV_ERR_NO_PATTERN;

    case ST_CODE_GAP:
        /* Waiting for next pulse or end of test. */
        if (level_now) {
            uint8_t gcls = eec_classify_gap(eec, elapsed);

            if (gcls == 0) {
                /* Intra-digit gap: same digit continues. */
            } else if (gcls == 1) {
                /* Digit boundary: save current digit. */
                if (eec->pulse_count > 0 && eec->digit_count < 3) {
                    eec->digits[eec->digit_count] = eec->pulse_count;
                    eec->digit_count++;
                    eec->pulse_count = 0;
                }
            } else {
                /* Code boundary: finish current digit then code. */
                if (eec->pulse_count > 0 && eec->digit_count < 3) {
                    eec->digits[eec->digit_count] = eec->pulse_count;
                    eec->digit_count++;
                    eec->pulse_count = 0;
                }
                if (eec->digit_count > 0) {
                    eec_assemble_code(eec);
                    eec_cache_code(eec, EEC_IV_MODE_KOEO);
                    eec->total_codes++;
                    eec->state = ST_CODE_DONE;
                    if (out_code != NULL) {
                        *out_code = eec->last_code;
                    }
                    return EEC_IV_OK;
                }
                /* No digits — sweep done / pass. */
                eec->state = ST_SWEEP_DONE;
            }
            eec->t_edge = now;
            eec->level_on = true;
        } else if (elapsed > eec->cfg.inter_code_gap_ms * 3UL) {
            /* Very long gap: end of test sequence. */
            if (eec->sweep_seen && eec->total_codes == 0) {
                eec->state = ST_SWEEP_DONE;
            } else {
                eec->state = ST_IDLE;
                eec->pulse_count = 0;
                eec->digit_count = 0;
                memset(eec->digits, 0, sizeof(eec->digits));
            }
            eec->t_edge = now;
        }
        return EEC_IV_ERR_NO_PATTERN;

    case ST_CODE_PULSE:
        /* Inside a code digit pulse group. */
        if (!level_now) {
            /* Pulse ended; move to gap state. */
            eec->state = ST_CODE_GAP;
            eec->t_edge = now;
            eec->level_on = false;
        } else if (elapsed > eec->cfg.code_pulse_ms * 4UL) {
            /* Stuck HIGH — abort. */
            eec->state = ST_IDLE;
            eec->t_edge = now;
            eec->level_on = true;
        }
        return EEC_IV_ERR_NO_PATTERN;

    case ST_CODE_DONE:
        return EEC_IV_OK;

    case ST_SWEEP_DONE:
        return EEC_IV_ERR_NO_CODES;
    }

    return EEC_IV_ERR_NO_PATTERN;
}

eec_iv_status_t eec_iv_run_test(eec_iv_t *eec, eec_iv_mode_t mode,
                                eec_iv_code_t *out_code, uint32_t timeout_ms)
{
    if (eec == NULL || !eec->initialized || out_code == NULL) {
        return EEC_IV_ERR_INVALID_ARG;
    }

    uint32_t effective = timeout_ms > 0 ? timeout_ms : eec->cfg.timeout_ms;
    uint32_t start = millis();

    eec_iv_reset(eec);

    for (;;) {
        eec_iv_code_t tmp;
        eec_iv_status_t st = eec_iv_poll(eec, &tmp);
        if (st == EEC_IV_OK) {
            *out_code = tmp;
            out_code->mode = mode;
            return EEC_IV_OK;
        }
        if (st == EEC_IV_ERR_NO_CODES) {
            return EEC_IV_ERR_NO_CODES;
        }
        if (st != EEC_IV_ERR_NO_PATTERN) {
            return st;
        }

        if ((uint32_t)(millis() - start) >= effective) {
            return EEC_IV_ERR_TIMEOUT;
        }

        delay(10);
        yield();
    }

    return EEC_IV_ERR_TIMEOUT;
}
