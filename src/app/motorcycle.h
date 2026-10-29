/*
 * app/motorcycle.h — Motorcycle (OBD1/Euro5) application layer.
 *
 * Brand-specific DTC lookup and blink-code reading for motorcycles.
 * Brand detection probes the ECU via proto_read_dtcs() and matches the
 * returned codes against per-brand tables. Blink-code reading depends on
 * the OBD1 blink parser (protocol/blink_parser.h — not yet implemented),
 * so motorcycle_read_blink_codes() returns MOTORCYCLE_ERR_OBD1_PENDING
 * until that layer exists.
 *
 * Layering: APP -> motorcycle_* -> {dtc_lookup, proto_read_dtcs} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#ifndef APP_MOTORCYCLE_H
#define APP_MOTORCYCLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"   /* proto_read_dtcs, proto_is_initialized, protocol_t */
#include "protocol/dtc.h"        /* dtc_lookup, dtc_list_t, dtc_entry_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Brand
 * ========================================================================= */

typedef enum {
    MOTO_BRAND_GENERIC   = 0,
    MOTO_BRAND_HONDA     = 1,
    MOTO_BRAND_YAMAHA    = 2,
    MOTO_BRAND_SUZUKI    = 3,
    MOTO_BRAND_KAWASAKI  = 4,
} moto_brand_t;

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    MOTORCYCLE_OK                   = 0,
    MOTORCYCLE_ERR_INVALID_ARG      = -1,
    MOTORCYCLE_ERR_NOT_INITIALIZED  = -2,
    MOTORCYCLE_ERR_PROTOCOL         = -3,   /* underlying protocol failure */
    MOTORCYCLE_ERR_NOT_FOUND        = -4,   /* code not in any lookup table */
    MOTORCYCLE_ERR_OBD1_PENDING     = -5,   /* blink parser not yet implemented */
} motorcycle_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
/* The real struct is defined only in app/motorcycle.c. */
typedef struct motorcycle motorcycle_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* already-initialized protocol handle */
} motorcycle_config_t;

/* Default config (designated initializers). */
#define MOTORCYCLE_CONFIG_DEFAULT(p) { .proto = (p) }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the motorcycle layer over an already-initialized protocol
 * handle. The brand defaults to MOTO_BRAND_GENERIC until
 * motorcycle_detect_brand() runs. */
motorcycle_status_t motorcycle_init(const motorcycle_config_t *cfg,
                                    motorcycle_t **out);

/* Release the motorcycle handle. */
motorcycle_status_t motorcycle_deinit(motorcycle_t *moto);

/* True if the handle was successfully initialized. */
bool motorcycle_is_initialized(const motorcycle_t *moto);

/* =========================================================================
 * Brand / DTC operations
 * ========================================================================= */

/* Probe the ECU (confirmed DTCs via proto_read_dtcs()) and guess the
 * motorcycle brand from per-brand table matches. Falls back to
 * MOTO_BRAND_GENERIC when nothing matches. */
motorcycle_status_t motorcycle_detect_brand(motorcycle_t *moto,
                                            moto_brand_t *brand, uint32_t timeout_ms);

/* Look up the human-readable description of a DTC code for the detected
 * brand (falls back to the generic table, then the protocol's generic
 * dtc_lookup()). desc is always NUL-terminated on success.
 * Returns MOTORCYCLE_ERR_NOT_FOUND when the code is unknown. */
motorcycle_status_t motorcycle_dtc_lookup(motorcycle_t *moto, const char *code,
                                          char *desc, size_t desc_len);

/* Read OBD1 blink codes from the ECU.
 * Stub: the OBD1 blink parser does not exist yet, so this returns
 * MOTORCYCLE_ERR_OBD1_PENDING. */
motorcycle_status_t motorcycle_read_blink_codes(motorcycle_t *moto,
                                                uint8_t *codes, size_t max_codes,
                                                size_t *out_count, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_MOTORCYCLE_H */
