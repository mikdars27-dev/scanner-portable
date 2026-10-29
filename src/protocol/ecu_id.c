/*
 * protocol/ecu_id.c — Automatic ECU identification implementation.
 *
 * Queries the ECU (J1979 Mode $09 on CAN, UDS $22 on K-Line) and matches
 * the returned ECU part number / VIN against a lookup table to determine
 * ECU type, vehicle type, and model year.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "protocol/ecu_id.h"

#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */
#define ECU_ID_TABLE_MAX 64

static ecu_id_entry_t g_ecu_table[ECU_ID_TABLE_MAX];
static size_t         g_ecu_count = 0;

/* ------------------------------------------------------------------------
 * Seed table: common ECU part number / VIN prefixes (Indonesian market)
 * ------------------------------------------------------------------------ */

static void ecu_id_seed_table(void)
{
    static const struct {
        const char *prefix;
        const char *ecu_type;
        const char *vehicle_type;
        uint16_t    year;
    } seeds[] = {
        /* Honda motorcycle ECUs (Indonesia) */
        {"37820", "PGM-FI", "motorcycle", 0},
        {"38770", "KEIHIN PGM-FI", "motorcycle", 0},
        {"11300", "Honda ECM", "motorcycle", 0},
        /* Yamaha motorcycle ECUs (Indonesia) */
        {"4D1", "Yamaha ECU", "motorcycle", 0},
        {"5SL", "Yamaha ECU", "motorcycle", 0},
        {"1D1", "Yamaha ECU", "scooter", 0},
        /* Suzuki motorcycle ECUs (Indonesia) */
        {"32800", "Suzuki ECM", "motorcycle", 0},
        /* VIN WMIs (first 3 chars) */
        {"MH2", "Honda", "motorcycle", 0},     /* Honda Indonesia */
        {"MD2", "Yamaha", "motorcycle", 0},    /* Yamaha Indonesia */
        {"MB8", "Suzuki", "motorcycle", 0},    /* Suzuki Indonesia */
    };

    if (g_ecu_count > 0) return;   /* already seeded */

    size_t n = sizeof(seeds) / sizeof(seeds[0]);
    if (n > ECU_ID_TABLE_MAX) n = ECU_ID_TABLE_MAX;

    for (size_t i = 0; i < n; i++) {
        ecu_id_entry_t *e = &g_ecu_table[g_ecu_count++];
        strncpy(e->prefix, seeds[i].prefix, sizeof(e->prefix) - 1);
        e->prefix[sizeof(e->prefix) - 1] = '\0';
        strncpy(e->ecu_type, seeds[i].ecu_type, sizeof(e->ecu_type) - 1);
        e->ecu_type[sizeof(e->ecu_type) - 1] = '\0';
        strncpy(e->vehicle_type, seeds[i].vehicle_type,
                sizeof(e->vehicle_type) - 1);
        e->vehicle_type[sizeof(e->vehicle_type) - 1] = '\0';
        e->year = seeds[i].year;
    }
}

/* ------------------------------------------------------------------------
 * VIN decoding
 * ------------------------------------------------------------------------ */

/* VIN model year character table (position 10, 0-indexed index 9).
 * I, O, Q, U, Z are not used. Two cycles: 1980-2000 (letters), 2001-2009
 * (digits), 2010-2030 (letters again). */
static uint16_t vin_year_char(char c)
{
    if (c >= 'A' && c <= 'Y') {
        int idx = c - 'A';   /* 0..24 */
        /* Cycle 1: A=1980 ... Y=2000 (idx 0..20, skipping I,O,Q) */
        if (idx <= 20) {
            /* A=0,B=1,C=2,D=3,E=4,F=5,G=6,H=7,J=8,K=9,L=10,M=11,N=12,P=13,R=14,S=15,T=16,V=17,W=18,X=19,Y=20 */
            const int skip[] = {8, 14, 16};   /* I, O, Q positions in 0..25 */
            int adj = idx;
            for (int s = 0; s < 3; s++) {
                if (adj >= skip[s]) adj++;
            }
            return (uint16_t)(1980 + adj);
        }
        /* Cycle 2: A=2010 ... T=2026 (idx 21..24 after skipping) */
        return (uint16_t)(2010 + (idx - 21));
    }
    if (c >= '1' && c <= '9') {
        return (uint16_t)(2000 + (c - '0'));
    }
    return 0;
}

uint16_t ecu_id_vin_year(const char *vin)
{
    if (vin == NULL) return 0;
    if (strlen(vin) < 10) return 0;
    return vin_year_char(vin[9]);
}

size_t ecu_id_vin_manufacturer(const char *vin, char *buf, size_t buf_len)
{
    if (vin == NULL || buf == NULL || buf_len == 0) return 0;
    if (strlen(vin) < 3) return 0;

    char wmi[4];
    memcpy(wmi, vin, 3);
    wmi[3] = '\0';

    /* Simple WMI -> manufacturer mapping (common Indonesian market) */
    static const struct {
        const char *wmi;
        const char *mfr;
    } wmi_table[] = {
        {"MH2", "Honda"},
        {"MD2", "Yamaha"},
        {"MB8", "Suzuki"},
        {"JM1", "Mazda"},
        {"JH2", "Honda"},
        {"JTE", "Toyota"},
        {"WBA", "BMW"},
        {"WVW", "Volkswagen"},
    };

    for (size_t i = 0; i < sizeof(wmi_table) / sizeof(wmi_table[0]); i++) {
        if (strcmp(wmi, wmi_table[i].wmi) == 0) {
            size_t n = strlen(wmi_table[i].mfr);
            if (n >= buf_len) n = buf_len - 1;
            memcpy(buf, wmi_table[i].mfr, n);
            buf[n] = '\0';
            return n;
        }
    }

    /* Unknown: return WMI as-is */
    size_t n = strlen(wmi);
    if (n >= buf_len) n = buf_len - 1;
    memcpy(buf, wmi, n);
    buf[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------------
 * Lookup table
 * ------------------------------------------------------------------------ */

static int ecu_id_find_index(const char *prefix)
{
    if (prefix == NULL) return -1;
    ecu_id_seed_table();
    for (size_t i = 0; i < g_ecu_count; i++) {
        if (strcmp(g_ecu_table[i].prefix, prefix) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Check if `value` starts with `prefix`. */
static int starts_with(const char *value, const char *prefix)
{
    size_t plen = strlen(prefix);
    size_t vlen = strlen(value);
    if (vlen < plen) return 0;
    return strncmp(value, prefix, plen) == 0;
}

proto_status_t ecu_id_register(const ecu_id_entry_t *entry)
{
    if (entry == NULL) return PROTO_ERR_INVALID_ARG;
    ecu_id_seed_table();
    if (g_ecu_count >= ECU_ID_TABLE_MAX) {
        return PROTO_ERR_INVALID_ARG;   /* table full */
    }
    ecu_id_entry_t *e = &g_ecu_table[g_ecu_count++];
    strncpy(e->prefix, entry->prefix, sizeof(e->prefix) - 1);
    e->prefix[sizeof(e->prefix) - 1] = '\0';
    strncpy(e->ecu_type, entry->ecu_type, sizeof(e->ecu_type) - 1);
    e->ecu_type[sizeof(e->ecu_type) - 1] = '\0';
    strncpy(e->vehicle_type, entry->vehicle_type,
            sizeof(e->vehicle_type) - 1);
    e->vehicle_type[sizeof(e->vehicle_type) - 1] = '\0';
    e->year = entry->year;
    return PROTO_OK;
}

proto_status_t ecu_id_lookup(const char *part_or_vin,
                             char *ecu_type, size_t ecu_type_len,
                             char *vehicle_type, size_t vehicle_type_len,
                             uint16_t *year)
{
    if (part_or_vin == NULL) return PROTO_ERR_INVALID_ARG;
    ecu_id_seed_table();

    /* Try ECU part number prefixes first, then VIN WMIs. */
    for (size_t i = 0; i < g_ecu_count; i++) {
        if (starts_with(part_or_vin, g_ecu_table[i].prefix)) {
            if (ecu_type && ecu_type_len > 0) {
                strncpy(ecu_type, g_ecu_table[i].ecu_type, ecu_type_len - 1);
                ecu_type[ecu_type_len - 1] = '\0';
            }
            if (vehicle_type && vehicle_type_len > 0) {
                strncpy(vehicle_type, g_ecu_table[i].vehicle_type,
                        vehicle_type_len - 1);
                vehicle_type[vehicle_type_len - 1] = '\0';
            }
            if (year) *year = g_ecu_table[i].year;
            return PROTO_OK;
        }
    }

    return PROTO_ERR_NOT_FOUND;   /* not in table */
}

/* ------------------------------------------------------------------------
 * Transport-specific queries
 * ------------------------------------------------------------------------ */

/* Query VIN via J1979 Mode $09 PID $01. */
static proto_status_t ecu_id_query_vin_j1979(j1979_t *j1979, char *vin,
                                             size_t vin_len, uint32_t timeout_ms)
{
    uint8_t resp[64];
    size_t resp_len = 0;
    j1979_status_t st = j1979_send(j1979, J1979_MODE_VEHICLE_INFO, 0x01,
                                   NULL, 0, resp, sizeof(resp), &resp_len,
                                   timeout_ms);
    if (st != J1979_OK) return (proto_status_t)st;

    /* Response: [0x49][0x01][VIN chars...] (VIN is ASCII, up to 17 bytes) */
    if (resp_len < 1) return PROTO_ERR_PROTOCOL;
    size_t copy_len = resp_len;
    if (copy_len > vin_len - 1) copy_len = vin_len - 1;
    memcpy(vin, resp, copy_len);
    vin[copy_len] = '\0';
    return PROTO_OK;
}

/* Query ECU name via J1979 Mode $09 PID $02 (CAN) or $03 (K-Line). */
static proto_status_t ecu_id_query_ecu_name_j1979(j1979_t *j1979, char *name,
                                                  size_t name_len,
                                                  uint32_t timeout_ms)
{
    uint8_t resp[64];
    size_t resp_len = 0;
    j1979_status_t st = j1979_send(j1979, J1979_MODE_VEHICLE_INFO, 0x02,
                                   NULL, 0, resp, sizeof(resp), &resp_len,
                                   timeout_ms);
    if (st != J1979_OK) return (proto_status_t)st;

    if (resp_len < 1) return PROTO_ERR_PROTOCOL;
    size_t copy_len = resp_len;
    if (copy_len > name_len - 1) copy_len = name_len - 1;
    memcpy(name, resp, copy_len);
    name[copy_len] = '\0';
    return PROTO_OK;
}

/* Query ECU part number via UDS $22 ReadDataByIdentifier on K-Line. */
static proto_status_t ecu_id_query_ecu_part_kline(kline_proto_t *kp,
                                                  char *part, size_t part_len,
                                                  uint32_t timeout_ms)
{
    /* Common DIDs for ECU part number identification */
    static const uint16_t dids[] = { 0xF190, 0xF18A, 0xF180 };

    for (size_t i = 0; i < sizeof(dids) / sizeof(dids[0]); i++) {
        uint8_t args[2];
        args[0] = (uint8_t)((dids[i] >> 8) & 0xFF);
        args[1] = (uint8_t)(dids[i] & 0xFF);

        uint8_t resp[64];
        size_t resp_len = 0;
        kline_proto_status_t st = kline_proto_send(kp, 0x22,
                                                   args, 2,
                                                   resp, sizeof(resp),
                                                   &resp_len, timeout_ms);
        if (st != KLINE_PROTO_OK) continue;

        /* Response: [0x62][DID_hi][DID_lo][data...] */
        if (resp_len < 3) continue;
        size_t copy_len = resp_len - 3;
        if (copy_len > part_len - 1) copy_len = part_len - 1;
        memcpy(part, &resp[3], copy_len);
        part[copy_len] = '\0';

        /* Trim trailing nulls/spaces */
        while (copy_len > 0 && (part[copy_len - 1] == '\0' ||
                                part[copy_len - 1] == ' ')) {
            part[--copy_len] = '\0';
        }
        if (copy_len > 0) return PROTO_OK;
    }
    return PROTO_ERR_PROTOCOL;
}

/* ------------------------------------------------------------------------
 * Main detection
 * ------------------------------------------------------------------------ */

proto_status_t ecu_id_detect(protocol_t *proto, ecu_id_t *ecu_id,
                             uint32_t timeout_ms)
{
    if (proto == NULL || ecu_id == NULL) {
        return PROTO_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(proto)) {
        return PROTO_ERR_NOT_INITIALIZED;
    }

    /* Initialize output. */
    memset(ecu_id, 0, sizeof(*ecu_id));

    proto_transport_t transport = proto_get_transport(proto);

    if (transport == PROTO_TRANSPORT_CAN) {
        /* --- CAN / J1979 --- */
        j1979_t *j1979 = proto_j1979_handle(proto);
        if (j1979 == NULL) return PROTO_ERR_PROTOCOL;

        /* Try VIN via Mode $09 PID $01. */
        proto_status_t st = ecu_id_query_vin_j1979(j1979, ecu_id->vin,
                                                   sizeof(ecu_id->vin),
                                                   timeout_ms);
        if (st == PROTO_OK && ecu_id->vin[0] != '\0') {
            /* Decode year from VIN position 10. */
            ecu_id->year = ecu_id_vin_year(ecu_id->vin);

            /* Look up ECU/vehicle type by VIN WMI (first 3 chars). */
            if (ecu_id_lookup(ecu_id->vin,
                              ecu_id->ecu_type, sizeof(ecu_id->ecu_type),
                              ecu_id->vehicle_type,
                              sizeof(ecu_id->vehicle_type),
                              &ecu_id->year) != PROTO_OK) {
                strncpy(ecu_id->vehicle_type, "unknown",
                        sizeof(ecu_id->vehicle_type) - 1);
            }
            ecu_id->valid = true;
            return PROTO_OK;
        }

        /* Fallback: ECU name via Mode $09 PID $02. */
        st = ecu_id_query_ecu_name_j1979(j1979, ecu_id->ecu_part_number,
                                         sizeof(ecu_id->ecu_part_number),
                                         timeout_ms);
        if (st == PROTO_OK && ecu_id->ecu_part_number[0] != '\0') {
            if (ecu_id_lookup(ecu_id->ecu_part_number,
                              ecu_id->ecu_type, sizeof(ecu_id->ecu_type),
                              ecu_id->vehicle_type,
                              sizeof(ecu_id->vehicle_type),
                              &ecu_id->year) != PROTO_OK) {
                strncpy(ecu_id->ecu_type, ecu_id->ecu_part_number,
                        sizeof(ecu_id->ecu_type) - 1);
                strncpy(ecu_id->vehicle_type, "unknown",
                        sizeof(ecu_id->vehicle_type) - 1);
            }
            ecu_id->valid = true;
            return PROTO_OK;
        }

        return PROTO_ERR_NO_RESPONSE;

    } else {
        /* --- K-Line / KWP2000 / UDS --- */
        kline_proto_t *kp = proto_kline_handle(proto);
        if (kp == NULL) return PROTO_ERR_PROTOCOL;

        /* Try ECU part number via UDS $22. */
        proto_status_t st = ecu_id_query_ecu_part_kline(kp,
                                                        ecu_id->ecu_part_number,
                                                        sizeof(ecu_id->ecu_part_number),
                                                        timeout_ms);
        if (st == PROTO_OK && ecu_id->ecu_part_number[0] != '\0') {
            if (ecu_id_lookup(ecu_id->ecu_part_number,
                              ecu_id->ecu_type, sizeof(ecu_id->ecu_type),
                              ecu_id->vehicle_type,
                              sizeof(ecu_id->vehicle_type),
                              &ecu_id->year) != PROTO_OK) {
                strncpy(ecu_id->ecu_type, ecu_id->ecu_part_number,
                        sizeof(ecu_id->ecu_type) - 1);
                strncpy(ecu_id->vehicle_type, "unknown",
                        sizeof(ecu_id->vehicle_type) - 1);
            }
            ecu_id->valid = true;
            return PROTO_OK;
        }

        return PROTO_ERR_NO_RESPONSE;
    }
}