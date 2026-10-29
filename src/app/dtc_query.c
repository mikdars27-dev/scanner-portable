/*
 * app/dtc_query.c — DTC Query / built-in lookup implementation.
 *
 * Wraps dtc_lookup()/dtc_register() from protocol/dtc.h.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "dtc_query.h"

#include <string.h>

struct dtc_query {
    const char *brand;
    bool        initialized;
};

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */
static dtc_query_status_t dtc_query_from_dtc(dtc_status_t st)
{
    switch (st) {
        case DTC_OK:                    return DTC_QUERY_OK;
        case DTC_ERR_INVALID_ARG:       return DTC_QUERY_ERR_INVALID_ARG;
        case DTC_ERR_NOT_FOUND:         return DTC_QUERY_ERR_NOT_FOUND;
        case DTC_ERR_BUFFER_TOO_SMALL:  return DTC_QUERY_ERR_BUFFER_TOO_SMALL;
        default:                        return DTC_QUERY_ERR_NOT_FOUND;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
dtc_query_status_t dtc_query_init(const dtc_query_config_t *cfg,
                                  dtc_query_t **out)
{
    if (cfg == NULL || out == NULL) {
        return DTC_QUERY_ERR_INVALID_ARG;
    }

    dtc_query_t *dq = (dtc_query_t *)calloc(1, sizeof(*dq));
    if (dq == NULL) {
        return DTC_QUERY_ERR_INVALID_ARG;
    }

    dq->brand       = cfg->brand;
    dq->initialized = true;

    *out = dq;
    return DTC_QUERY_OK;
}

dtc_query_status_t dtc_query_deinit(dtc_query_t *dq)
{
    if (dq == NULL) {
        return DTC_QUERY_ERR_INVALID_ARG;
    }
    dq->initialized = false;
    free(dq);
    return DTC_QUERY_OK;
}

bool dtc_query_is_initialized(const dtc_query_t *dq)
{
    return dq != NULL && dq->initialized;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */
dtc_query_status_t dtc_query_lookup(dtc_query_t *dq, const char *code,
                                    char *desc, size_t desc_len)
{
    if (dq == NULL || !dq->initialized) {
        return DTC_QUERY_ERR_NOT_INITIALIZED;
    }
    if (code == NULL || desc == NULL || desc_len == 0) {
        return DTC_QUERY_ERR_INVALID_ARG;
    }

    dtc_status_t st = dtc_lookup(dq->brand, code, desc, desc_len);
    return dtc_query_from_dtc(st);
}

dtc_query_status_t dtc_query_register_oem(dtc_query_t *dq,
                                          const dtc_entry_t *entry)
{
    if (dq == NULL || !dq->initialized) {
        return DTC_QUERY_ERR_NOT_INITIALIZED;
    }
    if (entry == NULL) {
        return DTC_QUERY_ERR_INVALID_ARG;
    }

    dtc_status_t st = dtc_register(entry);
    return dtc_query_from_dtc(st);
}
