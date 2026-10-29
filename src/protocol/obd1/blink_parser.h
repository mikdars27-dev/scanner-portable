/*
 * protocol/obd1/blink_parser.h — OBD1 blink-code parser (motorcycle MIL/LED).
 *
 * Parses blink-code flash patterns (e.g. Honda/Yamaha 2-digit codes) from a
 * GPIO input connected to the ECU's MIL (Malfunction Indicator Lamp) or
 * diagnostic LED. Measures pulse widths and inter-pulse gaps to decode the
 * number of flashes per digit, then assembles multi-digit DTC codes.
 *
 * Common OBD1 blink-code format:
 *   - LED flashes N times for the first digit, pause, then flashes M times
 *     for the second digit (2-digit code "NM").
 *   - A longer gap separates one complete code from the next.
 *
 * Layering: APP -> blink_parser_* -> GPIO digitalRead + millis
 */

#ifndef PROTOCOL_OBD1_BLINK_PARSER_H
#define PROTOCOL_OBD1_BLINK_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    BLINK_PARSER_OK                    = 0,
    BLINK_PARSER_ERR_INVALID_ARG       = -1,
    BLINK_PARSER_ERR_NOT_INITIALIZED   = -2,
    BLINK_PARSER_ERR_TIMEOUT           = -3,
    BLINK_PARSER_ERR_NO_PATTERN        = -4,   /* still collecting flashes */
    BLINK_PARSER_ERR_BUFFER_TOO_SMALL  = -5,
} blink_parser_status_t;

/* =========================================================================
 * Blink code type
 * ========================================================================= */
typedef enum {
    BLINK_CODE_2_DIGIT,   /* digit flashes, gap, digit flashes (Honda/Yamaha style) */
    BLINK_CODE_1_DIGIT,   /* single flash count = code */
} blink_code_type_t;

/* =========================================================================
 * Decoded blink code
 * ========================================================================= */
typedef struct {
    blink_code_type_t type;
    uint8_t           digit_count;   /* number of digits decoded */
    uint8_t           digits[4];     /* individual digits, max 4 */
    uint16_t          code_value;    /* assembled numeric code (e.g. 12) */
    char              code_str[8];   /* human-readable string, e.g. "12" */
} blink_code_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
typedef struct blink_parser blink_parser_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    int         gpio_pin;            /* GPIO connected to MIL/LED */
    bool        active_level;        /* true: LED HIGH = on; false: LED LOW = on */
    uint32_t    min_pulse_ms;        /* minimum flash pulse width to count (ms) */
    uint32_t    max_pulse_ms;        /* maximum flash pulse width (ms) */
    uint32_t    inter_digit_gap_ms;  /* gap between digits within a code (ms) */
    uint32_t    inter_code_gap_ms;   /* gap between codes / end of sequence (ms) */
    uint32_t    timeout_ms;          /* overall pattern detection timeout (ms) */
} blink_parser_config_t;

/* Default config tuned for typical motorcycle OBD1 blink codes. */
#define BLINK_PARSER_CONFIG_DEFAULT(gpio_pin)                                \
    {                                                                        \
        .gpio_pin            = (gpio_pin),                                   \
        .active_level        = true,                                         \
        .min_pulse_ms        = 80UL,                                         \
        .max_pulse_ms        = 400UL,                                        \
        .inter_digit_gap_ms  = 500UL,                                        \
        .inter_code_gap_ms   = 1500UL,                                       \
        .timeout_ms          = 10000UL,                                      \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the blink-code parser on the given GPIO pin.
 * Configures the pin as INPUT. */
blink_parser_status_t blink_parser_init(const blink_parser_config_t *cfg,
                                        blink_parser_t **out_parser);

/* Release the parser handle. Safe to call with NULL. */
blink_parser_status_t blink_parser_deinit(blink_parser_t *parser);

/* True if the handle was successfully initialized. */
bool blink_parser_is_initialized(const blink_parser_t *parser);

/* =========================================================================
 * Pattern detection
 * ========================================================================= */

/*
 * Poll the GPIO once and advance the blink-code state machine.
 * Call repeatedly (e.g. every 10-20 ms) until a complete code is detected.
 *
 * Returns BLINK_PARSER_OK when out_code is filled with a decoded code.
 * Returns BLINK_PARSER_ERR_NO_PATTERN when still collecting flashes
 * (call again later). Returns BLINK_PARSER_ERR_TIMEOUT when the overall
 * deadline passes without a complete code.
 */
blink_parser_status_t blink_parser_poll(blink_parser_t *parser,
                                        blink_code_t *out_code);

/*
 * Blocking variant: poll repeatedly until a complete code is detected or
 * timeout_ms elapses. timeout_ms = 0 uses the configured timeout.
 * Blocks with delay()/yield() so the watchdog is fed.
 */
blink_parser_status_t blink_parser_read_code(blink_parser_t *parser,
                                             blink_code_t *code,
                                             uint32_t timeout_ms);

/* Reset the internal state machine, discarding any partial pattern. */
blink_parser_status_t blink_parser_reset(blink_parser_t *parser);

/* Total number of complete codes detected since init or last reset. */
uint32_t blink_parser_code_count(const blink_parser_t *parser);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_OBD1_BLINK_PARSER_H */