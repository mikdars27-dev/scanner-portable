/*
 * app/dtc_service.c — Application-layer Diagnostic Trouble Code (DTC) service.
 *
 * Implements the app/dtc_service.h API on top of the unified protocol.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "dtc_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct dtc_service {
    dtc_service_config_t cfg;
    bool                 initialized;
};

/* ------------------------------------------------------------------------
 * Map protocol status codes onto dtc_service status codes.
 * ------------------------------------------------------------------------ */
static dtc_service_status_t dtc_service_from_proto(proto_status_t st)
{
    switch (st) {
        case PROTO_OK:                    return DTC_SERVICE_OK;
        case PROTO_ERR_INVALID_ARG:       return DTC_SERVICE_ERR_INVALID_ARG;
        case PROTO_ERR_NOT_INITIALIZED:   return DTC_SERVICE_ERR_NOT_INITIALIZED;
        default:                          return DTC_SERVICE_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
dtc_service_status_t dtc_service_init(const dtc_service_config_t *cfg,
                                      dtc_service_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL) {
        return DTC_SERVICE_ERR_INVALID_ARG;
    }

    dtc_service_t *svc = (dtc_service_t *)calloc(1, sizeof(*svc));
    if (svc == NULL) {
        return DTC_SERVICE_ERR_INVALID_ARG;
    }

    svc->cfg         = *cfg;          /* shallow copy: proto + brand pointers */
    svc->initialized = true;

    *out = svc;
    return DTC_SERVICE_OK;
}

dtc_service_status_t dtc_service_deinit(dtc_service_t *svc)
{
    if (svc == NULL) {
        return DTC_SERVICE_ERR_INVALID_ARG;
    }
    svc->initialized = false;
    free(svc);
    return DTC_SERVICE_OK;
}

bool dtc_service_is_initialized(const dtc_service_t *svc)
{
    return svc != NULL && svc->initialized;
}

/* ------------------------------------------------------------------------
 * DTC operations
 * ------------------------------------------------------------------------ */
dtc_service_status_t dtc_service_read_dtcs(dtc_service_t *svc, uint8_t mode,
                                           dtc_list_t *list, uint32_t timeout_ms)
{
    if (svc == NULL) return DTC_SERVICE_ERR_INVALID_ARG;
    if (!svc->initialized) return DTC_SERVICE_ERR_NOT_INITIALIZED;
    if (list == NULL) return DTC_SERVICE_ERR_INVALID_ARG;
    if (svc->proto == NULL) return DTC_SERVICE_ERR_INVALID_ARG;

    proto_status_t pst = proto_read_dtcs(svc->proto, mode, list, timeout_ms);
    if (pst != PROTO_OK) {
        return dtc_service_from_proto(pst);
    }

    /* Enrich descriptions with a brand-specific lookup when a brand is set.
     * Keep the protocol-supplied description when no brand entry matches. */
    if (svc->brand != NULL) {
        for (size_t i = 0; i < list->count; i++) {
            char desc[64];
            dtc_status_t dst = dtc_lookup(svc->brand, list->entries[i].code,
                                          desc, sizeof(desc));
            if (dst == DTC_OK) {
                strncpy(list->entries[i].desc, desc,
                        sizeof(list->entries[i].desc) - 1);
                list->entries[i].desc[sizeof(list->entries[i].desc) - 1] = '\0';
            }
        }
    }

    return DTC_SERVICE_OK;
}

dtc_service_status_t dtc_service_clear_dtcs(dtc_service_t *svc,
                                            uint32_t timeout_ms)
{
    if (svc == NULL) return DTC_SERVICE_ERR_INVALID_ARG;
    if (!svc->initialized) return DTC_SERVICE_ERR_NOT_INITIALIZED;
    if (svc->proto == NULL) return DTC_SERVICE_ERR_INVALID_ARG;

    proto_status_t pst = proto_clear_dtcs(svc->proto, timeout_ms);
    return dtc_service_from_proto(pst);
}

dtc_service_status_t dtc_service_format_entry(const dtc_entry_t *entry,
                                              char *buf, size_t buflen)
{
    if (entry == NULL || buf == NULL || buflen == 0) {
        return DTC_SERVICE_ERR_INVALID_ARG;
    }

    int n;
    if (entry->desc[0] != '\0') {
        n = snprintf(buf, buflen, "%s - %s", entry->code, entry->desc);
    } else {
        n = snprintf(buf, buflen, "%s", entry->code);
    }
    if (n < 0 || (size_t)n >= buflen) {
        return DTC_SERVICE_ERR_BUFFER_TOO_SMALL;
    }
    return DTC_SERVICE_OK;
}
