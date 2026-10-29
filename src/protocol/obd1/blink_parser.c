/*
 * protocol/obd1/blink_parser.c — OBD1 blink-code parser implementation.
 *
 * State machine:
 *   IDLE        — waiting for LED to turn on (active level)
 *   PULSE       — LED is on, measuring pulse width
 *   GAP         — LED is off, measuring gap width
 *   CODE_DONE   — a full code has been assembled, awaiting read/reset
 *
 * A "flash" is a pulse within [min_pulse_ms, max_pulse_ms]. Consecutive
 * flashes separated by gaps < inter_digit_gap_ms belong to the same digit.
 * A gap in [inter_digit_gap_ms, inter_code_gap_ms) ends the current digit
 * and starts the next. A gap >= inter_code_gap_ms ends the entire code.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "blink_parser.h"

#include <Arduino.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct blink_parser {
    blink_parser_config_t cfg;
    bool                  initialized;

    /* State machine. */
    typedef enum {
        ST_IDLE,       /* waiting for LED on */
        ST_PULSE,      /* LED on, measuring width */
        ST_GAP,        /* LED off, measuring gap */
        ST_CODE_DONE,  /* complete code ready, awaiting read/reset */
    } state_t;

    state_t       state;
    uint32_t      t_edge;        /* millis() at last level change */
    uint8_t       flash_count;   /* flashes in current digit */
    uint8_t       digit_count;   /* digits in current code */
    uint8_t       digits[4];     /* decoded digits */
    bool          led_on;        /* current latched level */
    uint32_t      total_codes;   /* codes detected since init/reset */
    blink_code_t  last_code;     /* last completed code, cached for poll() */
};

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/* True when the physical pin reads the active (LED-on) level. */
static bool bp_led_active(const blink_parser_t *bp)
{
    bool level = digitalRead(bp->cfg.gpio_pin) == HIGH;
    return bp->cfg.active_level ? level : !level;
}

/* Classify a pulse width: 0 = noise/too short, 1 = valid flash, 2 = too long. */
static uint8_t bp_classify_pulse(const blink_parser_t *bp, uint32_t ms)
{
    if (ms < bp->cfg.min_pulse_ms) {
        return 0; /* noise / bounce */
    }
    if (ms > bp->cfg.max_pulse_ms) {
        return 2; /* not a flash */
    }
    return 1; /* valid flash */
}

/* Classify a gap width: 0 = intra-digit, 1 = inter-digit, 2 = inter-code. */
static uint8_t bp_classify_gap(const blink_parser_t *bp, uint32_t ms)
{
    if (ms < bp->cfg.inter_digit_gap_ms) {
        return 0; /* same digit, keep collecting flashes */
    }
    if (ms < bp->cfg.inter_code_gap_ms) {
        return 1; /* digit boundary */
    }
    return 2; /* code boundary / end of sequence */
}

/* Assemble the digit array into code_value and code_str. */
static void bp_assemble_code(blink_parser_t *bp)
{
    uint16_t value = 0;
    for (uint8_t i = 0; i < bp->digit_count; i++) {
        value = (uint16_t)(value * 10 + bp->digits[i]);
    }
    bp->code_value = value;

    char *p = bp->code_str;
    for (uint8_t i = 0; i < bp->digit_count; i++) {
        *p++ = (char)('0' + bp->digits[i]);
    }
    *p = '\0';
}

/* Cache the current digits into last_code. */
static void bp_cache_code(blink_parser_t *bp)
{
    bp->last_code.type        = (bp->digit_count > 1)
                                    ? BLINK_CODE_2_DIGIT
                                    : BLINK_CODE_1_DIGIT;
    bp->last_code.digit_count = bp->digit_count;
    memcpy(bp->last_code.digits, bp->digits, sizeof(bp->digits));
    bp->last_code.code_value  = bp->code_value;
    snprintf(bp->last_code.code_str, sizeof(bp->last_code.code_str), "%s",
             bp->code_str);
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

blink_parser_status_t blink_parser_init(const blink_parser_config_t *cfg,
                                        blink_parser_t **out_parser)
{
    if (cfg == NULL || out_parser == NULL) {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }
    if (cfg->gpio_pin < 0) {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }
    if (cfg->min_pulse_ms == 0 || cfg->max_pulse_ms <= cfg->min_pulse_ms) {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }
    if (cfg->inter_digit_gap_ms == 0 ||
        cfg->inter_code_gap_ms <= cfg->inter_digit_gap_ms)
    {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }
    if (cfg->timeout_ms == 0) {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }

    blink_parser_t *bp = (blink_parser_t *)calloc(1, sizeof(*bp));
    if (bp == NULL) {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }

    bp->cfg       = *cfg;
    bp->state     = ST_IDLE;
    bp->t_edge    = 0;
    bp->flash_count = 0;
    bp->digit_count = 0;
    bp->led_on    = false;
    bp->total_codes = 0;
    memset(bp->digits, 0, sizeof(bp->digits));
    memset(&bp->last_code, 0, sizeof(bp->last_code));

    pinMode(cfg->gpio_pin, INPUT);

    bp->initialized = true;
    *out_parser = bp;
    return BLINK_PARSER_OK;
}

blink_parser_status_t blink_parser_deinit(blink_parser_t *parser)
{
    if (parser == NULL) {
        return BLINK_PARSER_OK;
    }
    if (!parser->initialized) {
        return BLINK_PARSER_ERR_NOT_INITIALIZED;
    }

    free(parser);
    return BLINK_PARSER_OK;
}

bool blink_parser_is_initialized(const blink_parser_t *parser)
{
    return parser != NULL && parser->initialized;
}

blink_parser_status_t blink_parser_reset(blink_parser_t *parser)
{
    if (parser == NULL || !parser->initialized) {
        return BLINK_PARSER_ERR_NOT_INITIALIZED;
    }

    parser->state        = ST_IDLE;
    parser->t_edge       = 0;
    parser->flash_count  = 0;
    parser->digit_count  = 0;
    parser->led_on       = false;
    memset(parser->digits, 0, sizeof(parser->digits));
    memset(&parser->last_code, 0, sizeof(parser->last_code));
    return BLINK_PARSER_OK;
}

uint32_t blink_parser_code_count(const blink_parser_t *parser)
{
    if (parser == NULL || !parser->initialized) {
        return 0;
    }
    return parser->total_codes;
}

blink_parser_status_t blink_parser_poll(blink_parser_t *parser,
                                        blink_code_t *out_code)
{
    if (parser == NULL || !parser->initialized) {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }

    /* If a code is already complete, keep returning it until reset(). */
    if (parser->state == ST_CODE_DONE) {
        if (out_code != NULL) {
            *out_code = parser->last_code;
        }
        return BLINK_PARSER_OK;
    }

    if (out_code != NULL) {
        memset(out_code, 0, sizeof(*out_code));
    }

    bool   level_now = bp_led_active(parser);
    uint32_t now     = millis();

    /* Initialize edge timestamp on first call. */
    if (parser->t_edge == 0) {
        parser->t_edge = now;
        parser->led_on = level_now;
        return BLINK_PARSER_ERR_NO_PATTERN;
    }

    uint32_t elapsed = (uint32_t)(now - parser->t_edge);

    switch (parser->state) {
    case ST_IDLE:
        /* Waiting for LED to turn on. */
        if (level_now) {
            parser->state = ST_PULSE;
            parser->t_edge = now;
            parser->led_on = true;
        }
        return BLINK_PARSER_ERR_NO_PATTERN;

    case ST_PULSE:
        /* LED is on; waiting for it to turn off. */
        if (!level_now) {
            /* Pulse ended. Classify its width. */
            uint8_t cls = bp_classify_pulse(parser, elapsed);
            if (cls == 1) {
                parser->flash_count++;
            }
            /* cls == 0 (noise) or == 2 (too long): ignore count, but the
             * line went off so move to gap state. */
            parser->state = ST_GAP;
            parser->t_edge = now;
            parser->led_on = false;
        } else if (elapsed > parser->cfg.max_pulse_ms * 4UL) {
            /* Stuck HIGH too long — likely a solid light, not blinking.
             * Reset to avoid hanging. */
            parser->state = ST_IDLE;
            parser->t_edge = now;
            parser->led_on = true;
        }
        return BLINK_PARSER_ERR_NO_PATTERN;

    case ST_GAP:
        /* LED is off; waiting for next flash or end of code. */
        if (level_now) {
            /* Next flash started. Classify the gap that just ended. */
            uint8_t cls = bp_classify_gap(parser, elapsed);

            if (cls == 0) {
                /* Intra-digit gap: same digit continues. */
                /* flash_count already incremented; nothing more to do. */
            } else if (cls == 1) {
                /* Inter-digit gap: current digit is complete. */
                if (parser->flash_count > 0 &&
                    parser->digit_count < 4)
                {
                    parser->digits[parser->digit_count] =
                        parser->flash_count;
                    parser->digit_count++;
                    parser->flash_count = 0;
                }
            } else { /* cls == 2 */
                /* Inter-code gap: end of the entire code. */
                if (parser->flash_count > 0 &&
                    parser->digit_count < 4)
                {
                    parser->digits[parser->digit_count] =
                        parser->flash_count;
                    parser->digit_count++;
                }
                if (parser->digit_count > 0) {
                    bp_assemble_code(parser);
                    bp_cache_code(parser);
                    parser->total_codes++;
                    parser->state = ST_CODE_DONE;
                    if (out_code != NULL) {
                        *out_code = parser->last_code;
                    }
                    return BLINK_PARSER_OK;
                }
                /* No valid digits; reset and keep listening. */
                parser->state = ST_IDLE;
                parser->flash_count = 0;
                parser->digit_count = 0;
                memset(parser->digits, 0, sizeof(parser->digits));
            }

            parser->t_edge = now;
            parser->led_on = true;
        } else if (elapsed > parser->cfg.inter_code_gap_ms * 2UL) {
            /* Gap excessively long with no new flash: abort current pattern. */
            parser->state = ST_IDLE;
            parser->flash_count = 0;
            parser->digit_count = 0;
            memset(parser->digits, 0, sizeof(parser->digits));
            parser->t_edge = now;
        }
        return BLINK_PARSER_ERR_NO_PATTERN;

    case ST_CODE_DONE:
        /* Handled at the top of the function. */
        return BLINK_PARSER_OK;
    }

    return BLINK_PARSER_ERR_NO_PATTERN;
}

blink_parser_status_t blink_parser_read_code(blink_parser_t *parser,
                                             blink_code_t *code,
                                             uint32_t timeout_ms)
{
    if (parser == NULL || !parser->initialized || code == NULL) {
        return BLINK_PARSER_ERR_INVALID_ARG;
    }

    uint32_t effective_timeout = timeout_ms > 0 ? timeout_ms
                                                : parser->cfg.timeout_ms;
    uint32_t start = millis();

    blink_parser_reset(parser);

    for (;;) {
        blink_code_t tmp;
        blink_parser_status_t st = blink_parser_poll(parser, &tmp);
        if (st == BLINK_PARSER_OK) {
            *code = tmp;
            return BLINK_PARSER_OK;
        }
        if (st != BLINK_PARSER_ERR_NO_PATTERN) {
            return st;
        }

        if ((uint32_t)(millis() - start) >= effective_timeout) {
            return BLINK_PARSER_ERR_TIMEOUT;
        }

        delay(10);
        yield();
    }
}
