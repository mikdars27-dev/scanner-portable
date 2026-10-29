/*
 * protocol/ecu_id.h — Automatic ECU identification.
 *
 * Detects ECU type, vehicle type, and model year by querying the ECU
 * (J1979 Mode $09 on CAN, UDS $22 on K-Line) and matching the returned
 * ECU part number / VIN against a lookup table.
 *
 * Layering: APP -> ecu_id_* -> {j1979_*, kline_proto_*, iso15765_2_*} -> drivers
 */

#ifndef PROTOCOL_ECU_ID_H
#define PROTOCOL_ECU_ID_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/protocol.h"   /* protocol_t, proto_status_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * ECU identification result
 * ========================================================================= */
typedef struct {
    char  ecu_part_number[32];   /* ECU part number (e.g. "37820-P0A-E01") */
    char  ecu_software_version[32]; /* ECU software version */
    char  vin[18];               /* Vehicle Identification Number (17 chars + NUL) */
    char  ecu_type[32];          /* e.g. "PGM-FI", "KEIHIN", "DELPHI", "BOSCH" */
    char  vehicle_type[32];      /* e.g. "motorcycle", "passenger_car", "scooter" */
    uint16_t year;               /* Model year (0 = unknown) */
    bool  valid;                 /* true if detection succeeded */
} ecu_id_t;

/* =========================================================================
 * Detection
 * ========================================================================= */

/*
 * Detect ECU identity over the given protocol handle.
 *
 * Tries, in order:
 *   1. CAN/J1979: Mode $09 PID $01 (VIN) and PID $02 (ECU name)
 *   2. K-Line/UDS: $22 ReadDataByIdentifier with common ECU part DIDs
 *
 * proto  : initialized protocol handle (CAN or K-Line)
 * ecu_id : output struct filled with detected values
 * timeout_ms: per-request timeout
 *
 * Returns PROTO_OK if at least ECU type/year could be determined,
 * PROTO_ERR_TIMEOUT if no response, PROTO_ERR_PROTOCOL on protocol error.
 */
proto_status_t ecu_id_detect(protocol_t *proto, ecu_id_t *ecu_id,
                             uint32_t timeout_ms);

/* =========================================================================
 * VIN decoding helpers (can be used independently)
 * ========================================================================= */

/* Decode model year from VIN position 10 (0-indexed: vin[9]).
 * Returns 0 if the character cannot be mapped. */
uint16_t ecu_id_vin_year(const char *vin);

/* Decode manufacturer (WMI = first 3 chars) into a short string.
 * Writes at most buf_len bytes including NUL. Returns bytes written
 * (excluding NUL), or 0 if unknown. */
size_t ecu_id_vin_manufacturer(const char *vin, char *buf, size_t buf_len);

/* =========================================================================
 * Lookup table
 * ========================================================================= */

typedef struct {
    char  prefix[16];     /* ECU part number or VIN prefix to match */
    char  ecu_type[32];
    char  vehicle_type[32];
    uint16_t year;        /* 0 = not specified in table */
} ecu_id_entry_t;

/* Register a custom ECU ID entry. Returns PROTO_OK on success. */
proto_status_t ecu_id_register(const ecu_id_entry_t *entry);

/* Look up an ECU part number (or VIN) in the table. Fills ecu_type,
 * vehicle_type, and year. Returns PROTO_OK if matched. */
proto_status_t ecu_id_lookup(const char *part_or_vin,
                             char *ecu_type, size_t ecu_type_len,
                             char *vehicle_type, size_t vehicle_type_len,
                             uint16_t *year);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_ECU_ID_H */