/*
 * app/dtc_query.h — DTC Query / built-in lookup (Feature 3: DTC Query).
 *
 * Provides a built-in DTC code lookup so users can identify the meaning of
 * a DTC without an external manual. Uses dtc_lookup()/dtc_register() from
 * protocol/dtc.h and supports adding OEM-specific codes at runtime.
 *
 * Layering: APP -> dtc_query_* -> dtc_* (internal tables)
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_DTC_QUERY_H
#define APP_DTC_QUERY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/dtc.h"   /* dtc_lookup, dtc_register, dtc_entry_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    DTC_QUERY_OK                    = 0,
    DTC_QUERY_ERR_INVALID_ARG       = -1,
    DTC_QUERY_ERR_NOT_INITIALIZED   = -2,
    DTC_QUERY_ERR_NOT_FOUND         = -3,   /* code not in lookup table */
    DTC_QUERY_ERR_BUFFER_TOO_SMALL  = -4,
} dtc_query_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef struct {
    const char *brand;   /* vehicle make (e.g. "honda", "yamaha"); NULL = generic */
} dtc_query_config_t;

#define DTC_QUERY_CONFIG_DEFAULT  { .brand = NULL }

typedef struct dtc_query dtc_query_t;   /* opaque handle */

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

dtc_query_status_t dtc_query_init(const dtc_query_config_t *cfg,
                                  dtc_query_t **out);

dtc_query_status_t dtc_query_deinit(dtc_query_t *dq);

bool dtc_query_is_initialized(const dtc_query_t *dq);

/* =========================================================================
 * Query
 * ========================================================================= */

/* Look up the description for a DTC code (e.g. "P0300").
 * Returns DTC_QUERY_OK with desc filled, DTC_QUERY_ERR_NOT_FOUND if unknown. */
dtc_query_status_t dtc_query_lookup(dtc_query_t *dq, const char *code,
                                    char *desc, size_t desc_len);

/* Register an OEM-specific DTC entry into the internal lookup table.
 * Useful for motorcycle/brand-specific codes not in the generic table. */
dtc_query_status_t dtc_query_register_oem(dtc_query_t *dq,
                                          const dtc_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* APP_DTC_QUERY_H */
