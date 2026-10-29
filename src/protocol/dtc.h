/*
 * protocol/dtc.h — Diagnostic Trouble Code (DTC) decoding and lookup.
 *
 * Decodes raw 2-byte DTC values (SAE J1979 / ISO 14229 format) into
 * 5-character strings (P0300, U0100, B1234, etc.) and provides a lookup
 * table for human-readable descriptions per vehicle make.
 *
 * Layering: APP -> DTC service -> dtc_* -> (internal tables)
 */

#ifndef PROTOCOL_DTC_H
#define PROTOCOL_DTC_H

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
    DTC_OK                    = 0,
    DTC_ERR_INVALID_ARG       = -1,
    DTC_ERR_NOT_FOUND         = -2,   /* code not in lookup table */
    DTC_ERR_BUFFER_TOO_SMALL  = -3,
} dtc_status_t;

/* =========================================================================
 * DTC type (first character of the 5-char string)
 * ========================================================================= */
typedef enum {
    DTC_TYPE_POWERTRAIN   = 0,   /* P */
    DTC_TYPE_BODY         = 1,   /* B */
    DTC_TYPE_CHASSIS      = 2,   /* C */
    DTC_TYPE_NETWORK      = 3,   /* U */
} dtc_type_t;

/* =========================================================================
 * DTC entry
 * ========================================================================= */
typedef struct {
    char  code[6];       /* "P0300\0" */
    char  desc[64];      /* human-readable description */
} dtc_entry_t;

/* =========================================================================
 * Decode / encode
 * ========================================================================= */

/*
 * Decode a raw 2-byte DTC into a 5-char string and optional description.
 *
 *   raw[0] bits 7:6 = type (00=P, 01=B, 10=C, 11=U)
 *   raw[0] bits 5:4 = first digit  (0..3)
 *   raw[0] bits 3:0 = second digit (0..F)
 *   raw[1] bits 7:4 = third digit  (0..F)
 *   raw[1] bits 3:0 = fourth digit (0..F)
 *
 * Example: P0300 -> raw = {0x03, 0x00}
 * Example: U0100 -> raw = {0xC1, 0x00}
 *
 * Returns DTC_OK on success, DTC_ERR_BUFFER_TOO_SMALL if code_buf < 6 bytes
 * or desc_buf too small for the description.
 */
dtc_status_t dtc_decode(const uint8_t raw[2], char *code_buf, size_t code_buf_len,
                        char *desc_buf, size_t desc_buf_len);

/*
 * Encode a 5-char DTC string (e.g. "P0300") into raw 2-byte form.
 * Returns DTC_OK on success, DTC_ERR_INVALID_ARG for malformed strings.
 */
dtc_status_t dtc_encode(const char *code, uint8_t raw[2]);

/*
 * Look up a DTC description by code string (e.g. "P0300").
 * brand may be NULL (generic lookup) or a make name ("honda", "toyota", etc.).
 * Returns DTC_OK with desc filled, DTC_ERR_NOT_FOUND if no entry matches.
 */
dtc_status_t dtc_lookup(const char *brand, const char *code,
                        char *desc_buf, size_t desc_buf_len);

/*
 * Register an additional DTC entry (for OEM-specific codes).
 * Copies the entry into an internal static table.
 */
dtc_status_t dtc_register(const dtc_entry_t *entry);

/* =========================================================================
 * DTC list helpers (used by J1979 / UDS response parsing)
 * ========================================================================= */

typedef struct {
    dtc_entry_t *entries;   /* caller-allocated array */
    size_t       count;     /* number of entries filled */
    size_t       capacity;  /* size of entries array */
} dtc_list_t;

/* Decode a sequence of raw 2-byte DTCs from a response buffer into dtc_list_t. */
dtc_status_t dtc_decode_list(const uint8_t *raw, size_t raw_len,
                             dtc_list_t *list);

/* Release resources held by a dtc_list_t (frees entries array). */
void dtc_list_free(dtc_list_t *list);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_DTC_H */