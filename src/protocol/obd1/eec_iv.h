/*
 * protocol/obd1/eec_iv.h — Ford EEC-IV OBD1 self-test.
 *
 * Ford's pre-OBD2 engine control system (1980s–1990s). Self-test codes are
 * output as sweep/blink patterns on the Check Engine Light (CEL) or the
 * STO (Self Test Output) pin using VPW (Variable Pulse Width) signaling.
 *
 * Test modes:
 *   - KOEO (Key On, Engine Off): hard / continuous memory codes
 *   - KOER (Key On, Engine Running): running codes (intermittent + hard)
 *
 * The parser measures pulse widths on a GPIO input and decodes them into
 * numeric DTC codes (e.g. code 34, code 41).
 *
 * Layering: APP -> eec_iv_* -> GPIO digitalRead + millis
 */

#ifndef PROTOCOL_OBD1_EEC_IV_H
#define PROTOCOL_OBD1_EEC_IV_H

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
    EEC_IV_OK                    = 0,
    EEC_IV_ERR_INVALID_ARG       = -1,
    EEC_IV_ERR_NOT_INITIALIZED   = -2,
    EEC_IV_ERR_TIMEOUT           = -3,
    EEC_IV_ERR_PROTOCOL          = -4,   /* unexpected sweep / bad pattern */
    EEC_IV_ERR_NO_CODES          = -5,   /* no codes detected (pass) */
    EEC_IV_ERR_BUFFER_TOO_SMALL  = -6,
} eec_iv_status_t;

/* =========================================================================
 * Self-test mode
 * ========================================================================= */
typedef enum {
    EEC_IV_MODE_KOEO,   /* Key On Engine Off — continuous memory / hard codes */
    EEC_IV_MODE_KOER,   /* Key On Engine Running — running codes */
} eec_iv_mode_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
typedef struct eec_iv eec_iv_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    int         gpio_pin;      /* GPIO connected to STO pin or CEL */
    bool        active_level;  /* true: CEL/STO HIGH = on; false: LOW = on */
    uint32_t    sweep_pulse_ms;/* expected sweep pulse width (KOEO/KOER sweep) */
    uint32_t    code_pulse_ms; /* nominal code pulse width (pulse = digit) */
    uint32_t    inter_pulse_gap_ms; /* gap between pulses of a digit (ms) */
    uint32_t    inter_digit_gap_ms; /* gap between digits (ms) */
    uint32_t    inter_code_gap_ms;  /* gap between codes / end (ms) */
    uint32_t    timeout_ms;    /* overall test timeout (ms) */
} eec_iv_config_t;

/* Default config for typical Ford EEC-IV VPW self-test. */
#define EEC_IV_CONFIG_DEFAULT                                                \
    {                                                                        \
        .gpio_pin             = -1,                                          \
        .active_level         = true,                                        \
        .sweep_pulse_ms       = 400UL,                                       \
        .code_pulse_ms        = 200UL,                                       \
        .inter_pulse_gap_ms   = 100UL,                                       \
        .inter_digit_gap_ms   = 500UL,                                       \
        .inter_code_gap_ms    = 1500UL,                                      \
        .timeout_ms           = 60000UL,                                     \
    }

/* =========================================================================
 * Decoded EEC-IV code
 * ========================================================================= */
typedef struct {
    eec_iv_mode_t mode;       /* test mode in which the code was found */
    uint8_t       digit_count;
    uint8_t       digits[3];  /* max 3 digits (e.g. 345) */
    uint16_t      code_value; /* assembled numeric code (e.g. 34) */
    char          code_str[8];/* human-readable string, e.g. "34" */
} eec_iv_code_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the EEC-IV self-test parser on the given GPIO pin. */
eec_iv_status_t eec_iv_init(const eec_iv_config_t *cfg, eec_iv_t **out_eec);

/* Release the EEC-IV handle. Safe to call with NULL. */
eec_iv_status_t eec_iv_deinit(eec_iv_t *eec);

/* True if the handle was successfully initialized. */
bool eec_iv_is_initialized(const eec_iv_t *eec);

/* =========================================================================
 * Self-test execution
 * ========================================================================= */

/*
 * Start a self-test sequence. The caller must physically set the vehicle
 * to KOEO or KOER mode before calling this. The function then polls the
 * GPIO and decodes the sweep + code pulses.
 *
 * Returns EEC_IV_OK with out_code filled when a complete code is detected,
 * EEC_IV_ERR_NO_CODES when the sweep completes with no codes (pass),
 * EEC_IV_ERR_TIMEOUT when the deadline passes.
 */
eec_iv_status_t eec_iv_run_test(eec_iv_t *eec, eec_iv_mode_t mode,
                                eec_iv_code_t *out_code, uint32_t timeout_ms);

/* Poll once (non-blocking). Call repeatedly during a test. Returns
 * EEC_IV_OK when a code is complete, EEC_IV_ERR_NO_CODES when sweep done
 * with no codes, EEC_IV_ERR_NO_PATTERN while still collecting. */
eec_iv_status_t eec_iv_poll(eec_iv_t *eec, eec_iv_code_t *out_code);

/* Reset the internal state machine. */
eec_iv_status_t eec_iv_reset(eec_iv_t *eec);

/* Total codes detected since init/reset. */
uint32_t eec_iv_code_count(const eec_iv_t *eec);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_OBD1_EEC_IV_H */