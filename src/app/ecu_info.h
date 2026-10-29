/*
 * app/ecu_info.h — ECU system information (Feature 1: Membaca Informasi Sistem).
 *
 * Reads ECU identification data (VIN, ECU name, software version) via
 * J1979 Mode $09 (CAN) or UDS $22 ReadDID (K-Line).
 *
 * Layering: APP -> ecu_info_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_ECU_INFO_H
#define APP_ECU_INFO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    ECU_INFO_OK                    = 0,
    ECU_INFO_ERR_INVALID_ARG       = -1,
    ECU_INFO_ERR_NOT_INITIALIZED   = -2,
    ECU_INFO_ERR_PROTOCOL          = -3,
    ECU_INFO_ERR_BUFFER_TOO_SMALL  = -4,
    ECU_INFO_ERR_NOT_SUPPORTED     = -5,   /* transport or PID/DID not supported */
    ECU_INFO_ERR_TIMEOUT           = -6,   /* underlying transport timeout */
} ecu_info_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef struct {
    protocol_t *proto;   /* unified protocol handle (CAN or K-Line) */
} ecu_info_config_t;

#define ECU_INFO_CONFIG_DEFAULT(p)  { .proto = (p) }

typedef struct ecu_info ecu_info_t;   /* opaque handle */

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

ecu_info_status_t ecu_info_init(const ecu_info_config_t *cfg,
                                ecu_info_t **out);

ecu_info_status_t ecu_info_deinit(ecu_info_t *info);

bool ecu_info_is_initialized(const ecu_info_t *info);

/* =========================================================================
 * ECU identification
 * ========================================================================= */

/* Read VIN (Vehicle Identification Number).
 * CAN/J1979: Mode $09 PID $01.  K-Line/UDS: $22 ReadDID 0xF190. */
ecu_info_status_t ecu_info_read_vin(ecu_info_t *info, char *buf, size_t len,
                                    uint32_t timeout_ms);

/* Read ECU name.
 * CAN/J1979: Mode $09 PID $02.  K-Line: not universally supported. */
ecu_info_status_t ecu_info_read_ecu_name(ecu_info_t *info, char *buf, size_t len,
                                         uint32_t timeout_ms);

/* Read software version.
 * K-Line/UDS: $22 ReadDID 0xF18C (manufacturer-specific).  CAN: N/A. */
ecu_info_status_t ecu_info_read_sw_version(ecu_info_t *info, char *buf, size_t len,
                                           uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_ECU_INFO_H */
