/*
 * app/dtc_service.h — Application-layer Diagnostic Trouble Code (DTC) service.
 *
 * Thin layer over the unified protocol (protocol_*) that reads and clears DTCs
 * and formats DTC entries for the UI. The brand carried in the config is used
 * for make-specific description lookup via dtc_lookup().
 *
 * Layering: APP -> dtc_service_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 */

#ifndef APP_DTC_SERVICE_H
#define APP_DTC_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"   /* proto_read_dtcs, proto_clear_dtcs, protocol_t */
#include "protocol/dtc.h"        /* dtc_list_t, dtc_entry_t, dtc_lookup */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    DTC_SERVICE_OK                    = 0,
    DTC_SERVICE_ERR_INVALID_ARG       = -1,
    DTC_SERVICE_ERR_NOT_INITIALIZED   = -2,
    DTC_SERVICE_ERR_PROTOCOL          = -3,   /* underlying protocol failure */
    DTC_SERVICE_ERR_BUFFER_TOO_SMALL  = -4,   /* output buffer cannot hold the result */
    DTC_SERVICE_ERR_NOT_FOUND         = -5,   /* code/entry not in lookup table */
} dtc_service_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
typedef struct dtc_service dtc_service_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    protocol_t *proto;   /* already-initialized protocol handle (CAN or K-Line) */
    const char *brand;   /* make name for brand-specific lookup (may be NULL) */
} dtc_service_config_t;

/* Default config (designated initializers). */
#define DTC_SERVICE_CONFIG_DEFAULT(p, b) { .proto = (p), .brand = (b) }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the DTC service over an already-initialized protocol handle. */
dtc_service_status_t dtc_service_init(const dtc_service_config_t *cfg,
                                      dtc_service_t **out);

/* Release the DTC service handle. */
dtc_service_status_t dtc_service_deinit(dtc_service_t *svc);

/* True if the handle was successfully initialized. */
bool dtc_service_is_initialized(const dtc_service_t *svc);

/* =========================================================================
 * DTC operations
 * ========================================================================= */

/* Read DTCs (confirmed/pending/permanent per mode) into list.
 * mode: J1979_MODE_DTC_CONFIRMED ($03), DTC_PENDING ($07), DTC_PERMANENT ($0A),
 *       or a KWP2000/UDS ReadDTCInformation subfunction. */
dtc_service_status_t dtc_service_read_dtcs(dtc_service_t *svc, uint8_t mode,
                                           dtc_list_t *list, uint32_t timeout_ms);

/* Clear emission-related DTCs. */
dtc_service_status_t dtc_service_clear_dtcs(dtc_service_t *svc,
                                            uint32_t timeout_ms);

/* Format a dtc_entry_t into buf as "CODE - description" (or just "CODE"
 * when the description is empty). Returns DTC_SERVICE_ERR_BUFFER_TOO_SMALL
 * when the output does not fit (including the NUL terminator). */
dtc_service_status_t dtc_service_format_entry(const dtc_entry_t *entry,
                                              char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* APP_DTC_SERVICE_H */
