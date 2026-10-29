/*
 * protocol/dtc.c — Diagnostic Trouble Code (DTC) decoding and lookup.
 *
 * Implements SAE J1979 / ISO 14229 2-byte DTC encoding:
 *   raw[0] bits 7:6 = type (00=P, 01=B, 10=C, 11=U)
 *   raw[0] bits 5:4 = first digit
 *   raw[0] bits 3:0 = second digit
 *   raw[1] bits 7:4 = third digit
 *   raw[1] bits 3:0 = fourth digit
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "protocol/dtc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Static DTC table (generic OBD2 / common motorcycle codes)
 * ------------------------------------------------------------------------ */

#define DTC_TABLE_MAX 64

static dtc_entry_t g_dtc_table[DTC_TABLE_MAX];
static size_t      g_dtc_count = 0;

/* Seed the table with common generic powertrain DTCs. */
static void dtc_seed_table(void)
{
    static const struct { const char *code; const char *desc; } seeds[] = {
        /* Generic powertrain (P0xxx) */
        {"P0000", "No DTC detected"},
        {"P0100", "Mass or Volume Air Flow Circuit Malfunction"},
        {"P0101", "Mass or Volume Air Flow Circuit Range/Performance"},
        {"P0102", "Mass or Volume Air Flow Circuit Low Input"},
        {"P0103", "Mass or Volume Air Flow Circuit High Input"},
        {"P0110", "Intake Air Temperature Circuit Malfunction"},
        {"P0111", "Intake Air Temperature Sensor 1 Performance"},
        {"P0112", "Intake Air Temperature Sensor 1 Circuit Low"},
        {"P0113", "Intake Air Temperature Sensor 1 Circuit High"},
        {"P0115", "Engine Coolant Temperature Circuit Malfunction"},
        {"P0116", "Engine Coolant Temperature Circuit Range/Performance"},
        {"P0117", "Engine Coolant Temperature Circuit Low Input"},
        {"P0118", "Engine Coolant Temperature Circuit High Input"},
        {"P0120", "Throttle/Pedal Position Sensor/Switch A Circuit Malfunction"},
        {"P0121", "Throttle/Pedal Position Sensor/Switch A Circuit Range/Performance"},
        {"P0122", "Throttle/Pedal Position Sensor/Switch A Circuit Low Input"},
        {"P0123", "Throttle/Pedal Position Sensor/Switch A Circuit High Input"},
        {"P0130", "O2 Sensor Circuit Malfunction (Bank 1, Sensor 1)"},
        {"P0131", "O2 Sensor Circuit Low Voltage (Bank 1, Sensor 1)"},
        {"P0132", "O2 Sensor Circuit High Voltage (Bank 1, Sensor 1)"},
        {"P0133", "O2 Sensor Circuit Slow Response (Bank 1, Sensor 1)"},
        {"P0134", "O2 Sensor Circuit No Activity Detected (Bank 1, Sensor 1)"},
        {"P0171", "System Too Lean (Bank 1)"},
        {"P0172", "System Too Rich (Bank 1)"},
        {"P0174", "System Too Lean (Bank 2)"},
        {"P0175", "System Too Rich (Bank 2)"},
        {"P0200", "Injector Circuit Malfunction"},
        {"P0300", "Random/Multiple Cylinder Misfire Detected"},
        {"P0301", "Cylinder 1 Misfire Detected"},
        {"P0302", "Cylinder 2 Misfire Detected"},
        {"P0303", "Cylinder 3 Misfire Detected"},
        {"P0304", "Cylinder 4 Misfire Detected"},
        {"P0325", "Knock Sensor 1 Circuit Malfunction (Bank 1)"},
        {"P0335", "Crankshaft Position Sensor A Circuit Malfunction"},
        {"P0340", "Camshaft Position Sensor A Circuit Malfunction (Bank 1)"},
        {"P0400", "Exhaust Gas Recirculation Flow Malfunction"},
        {"P0420", "Catalyst System Efficiency Below Threshold (Bank 1)"},
        {"P0430", "Catalyst System Efficiency Below Threshold (Bank 2)"},
        {"P0440", "Evaporative Emission Control System Malfunction"},
        {"P0442", "Evaporative Emission Control System Leak Detected (small)"},
        {"P0455", "Evaporative Emission Control System Leak Detected (gross)"},
        {"P0500", "Vehicle Speed Sensor Malfunction"},
        {"P0505", "Idle Control System Malfunction"},
        {"P0506", "Idle Control System RPM Lower Than Expected"},
        {"P0507", "Idle Control System RPM Higher Than Expected"},
        {"P0600", "Serial Communication Link Malfunction"},
        {"P0700", "Transmission Control System Malfunction"},
        /* Common motorcycle-specific codes */
        {"P0562", "System Voltage Low"},
        {"P0563", "System Voltage High"},
        {"P1000", "OBDII Monitor Testing Not Complete"},
        {"U0100", "Lost Communication with ECM/PCM"},
        {"U0121", "Lost Communication with Anti-Lock Brake System (ABS)"},
        {"U0140", "Lost Communication with Body Control Module"},
        {"B0010", "Driver Frontal Stage 1 Deployment Control"},
        {"C0035", "Left Front Wheel Speed Sensor Circuit"},
    };

    if (g_dtc_count > 0) return;   /* already seeded */

    size_t n = sizeof(seeds) / sizeof(seeds[0]);
    if (n > DTC_TABLE_MAX) n = DTC_TABLE_MAX;

    for (size_t i = 0; i < n; i++) {
        dtc_entry_t *e = &g_dtc_table[g_dtc_count++];
        strncpy(e->code, seeds[i].code, sizeof(e->code) - 1);
        e->code[sizeof(e->code) - 1] = '\0';
        strncpy(e->desc, seeds[i].desc, sizeof(e->desc) - 1);
        e->desc[sizeof(e->desc) - 1] = '\0';
    }
}

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static const char *dtc_type_char(dtc_type_t type)
{
    switch (type) {
        case DTC_TYPE_POWERTRAIN: return "P";
        case DTC_TYPE_BODY:       return "B";
        case DTC_TYPE_CHASSIS:    return "C";
        case DTC_TYPE_NETWORK:    return "U";
        default:                  return "?";
    }
}

static dtc_type_t dtc_type_from_char(char c)
{
    switch (c) {
        case 'P': case 'p': return DTC_TYPE_POWERTRAIN;
        case 'B': case 'b': return DTC_TYPE_BODY;
        case 'C': case 'c': return DTC_TYPE_CHASSIS;
        case 'U': case 'u': return DTC_TYPE_NETWORK;
        default:            return DTC_TYPE_POWERTRAIN;
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Find a DTC entry by code string. Returns index or -1. */
static int dtc_find_index(const char *code)
{
    if (code == NULL) return -1;
    dtc_seed_table();
    for (size_t i = 0; i < g_dtc_count; i++) {
        if (strcmp(g_dtc_table[i].code, code) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

dtc_status_t dtc_decode(const uint8_t raw[2], char *code_buf, size_t code_buf_len,
                        char *desc_buf, size_t desc_buf_len)
{
    if (raw == NULL || code_buf == NULL || code_buf_len < 6) {
        return DTC_ERR_INVALID_ARG;
    }

    dtc_seed_table();

    uint8_t b0 = raw[0];
    uint8_t b1 = raw[1];

    dtc_type_t type = (dtc_type_t)((b0 >> 6) & 0x03);
    uint8_t    d1   = (b0 >> 4) & 0x03;   /* first digit  */
    uint8_t    d2   =  b0        & 0x0F;   /* second digit */
    uint8_t    d3   = (b1 >> 4) & 0x0F;   /* third digit  */
    uint8_t    d4   =  b1        & 0x0F;   /* fourth digit */

    int n = snprintf(code_buf, code_buf_len, "%s%1X%1X%1X%1X",
                     dtc_type_char(type), d1, d2, d3, d4);
    if (n < 0 || (size_t)n >= code_buf_len) {
        return DTC_ERR_BUFFER_TOO_SMALL;
    }

    /* Look up description. */
    if (desc_buf != NULL && desc_buf_len > 0) {
        int idx = dtc_find_index(code_buf);
        if (idx >= 0) {
            strncpy(desc_buf, g_dtc_table[idx].desc, desc_buf_len - 1);
            desc_buf[desc_buf_len - 1] = '\0';
        } else {
            strncpy(desc_buf, "Manufacturer-specific / not in table",
                    desc_buf_len - 1);
            desc_buf[desc_buf_len - 1] = '\0';
        }
    }

    return DTC_OK;
}

dtc_status_t dtc_encode(const char *code, uint8_t raw[2])
{
    if (code == NULL || raw == NULL) {
        return DTC_ERR_INVALID_ARG;
    }

    /* Expect format: P0300 (5 chars + NUL) */
    if (strlen(code) != 5) {
        return DTC_ERR_INVALID_ARG;
    }

    dtc_type_t type = dtc_type_from_char(code[0]);
    int d1 = hexval(code[1]);
    int d2 = hexval(code[2]);
    int d3 = hexval(code[3]);
    int d4 = hexval(code[4]);

    if (d1 < 0 || d1 > 3 || d2 < 0 || d2 > 15 ||
        d3 < 0 || d3 > 15 || d4 < 0 || d4 > 15) {
        return DTC_ERR_INVALID_ARG;
    }

    /* First digit must be 0..3 for all types per SAE J1979 */
    if (d1 > 3) {
        return DTC_ERR_INVALID_ARG;
    }

    uint8_t b0 = (uint8_t)(((uint8_t)type << 6) |
                           ((uint8_t)d1 << 4) |
                           (uint8_t)d2);
    uint8_t b1 = (uint8_t)(((uint8_t)d3 << 4) | (uint8_t)d4);

    raw[0] = b0;
    raw[1] = b1;
    return DTC_OK;
}

dtc_status_t dtc_lookup(const char *brand, const char *code,
                        char *desc_buf, size_t desc_buf_len)
{
    (void)brand;   /* future: per-make override tables */
    if (code == NULL || desc_buf == NULL || desc_buf_len == 0) {
        return DTC_ERR_INVALID_ARG;
    }

    dtc_seed_table();

    int idx = dtc_find_index(code);
    if (idx < 0) {
        return DTC_ERR_NOT_FOUND;
    }

    strncpy(desc_buf, g_dtc_table[idx].desc, desc_buf_len - 1);
    desc_buf[desc_buf_len - 1] = '\0';
    return DTC_OK;
}

dtc_status_t dtc_register(const dtc_entry_t *entry)
{
    if (entry == NULL) {
        return DTC_ERR_INVALID_ARG;
    }
    dtc_seed_table();
    if (g_dtc_count >= DTC_TABLE_MAX) {
        return DTC_ERR_INVALID_ARG;   /* table full */
    }
    dtc_entry_t *e = &g_dtc_table[g_dtc_count++];
    strncpy(e->code, entry->code, sizeof(e->code) - 1);
    e->code[sizeof(e->code) - 1] = '\0';
    strncpy(e->desc, entry->desc, sizeof(e->desc) - 1);
    e->desc[sizeof(e->desc) - 1] = '\0';
    return DTC_OK;
}

dtc_status_t dtc_decode_list(const uint8_t *raw, size_t raw_len,
                             dtc_list_t *list)
{
    if (raw == NULL || list == NULL) {
        return DTC_ERR_INVALID_ARG;
    }
    if (raw_len % 2 != 0) {
        return DTC_ERR_INVALID_ARG;   /* DTCs are 2 bytes each */
    }

    dtc_seed_table();

    size_t count = raw_len / 2;
    if (list->entries == NULL || list->capacity < count) {
        /* Resize caller's buffer if needed. */
        dtc_entry_t *new_entries = (dtc_entry_t *)realloc(
            list->entries, count * sizeof(dtc_entry_t));
        if (new_entries == NULL && count > 0) {
            return DTC_ERR_INVALID_ARG;
        }
        list->entries = new_entries;
        list->capacity = count;
    }

    list->count = 0;
    for (size_t i = 0; i < count; i++) {
        char code_buf[16];
        char desc_buf[64];
        dtc_status_t st = dtc_decode(&raw[i * 2], code_buf, sizeof(code_buf),
                                     desc_buf, sizeof(desc_buf));
        if (st != DTC_OK) continue;

        dtc_entry_t *e = &list->entries[list->count++];
        strncpy(e->code, code_buf, sizeof(e->code) - 1);
        e->code[sizeof(e->code) - 1] = '\0';
        strncpy(e->desc, desc_buf, sizeof(e->desc) - 1);
        e->desc[sizeof(e->desc) - 1] = '\0';
    }

    return DTC_OK;
}

void dtc_list_free(dtc_list_t *list)
{
    if (list == NULL) return;
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}