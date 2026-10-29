/*
 * app/motorcycle.c — Motorcycle (OBD1/Euro5) application layer.
 *
 * Implements the app/motorcycle.h API on top of dtc_lookup() and
 * proto_read_dtcs(). Brand-specific tables cover Honda, Yamaha, Suzuki
 * and Kawasaki; everything else uses the generic table. Blink-code
 * reading is a stub until the OBD1 blink parser exists.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "motorcycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct motorcycle {
    motorcycle_config_t cfg;
    moto_brand_t        brand;     /* MOTO_BRAND_GENERIC until detected */
    bool                initialized;
};

/* ------------------------------------------------------------------------
 * Brand-specific DTC tables
 * ------------------------------------------------------------------------ */

typedef struct {
    const char *code;   /* e.g. "P0300", "P1300" */
    const char *desc;   /* human-readable description */
} moto_dtc_entry_t;

static const moto_dtc_entry_t moto_dtc_table_generic[] = {
    { "P0300", "Random/multiple cylinder misfire" },
    { "P0301", "Cylinder 1 misfire" },
    { "P0302", "Cylinder 2 misfire" },
    { "P0100", "Mass air flow sensor circuit malfunction" },
    { "P0110", "Intake air temperature sensor circuit malfunction" },
    { "P0120", "Throttle position sensor circuit malfunction" },
    { "P0500", "Vehicle speed sensor malfunction" },
    { "P0560", "System voltage malfunction" },
    { "U0100", "Lost communication with ECM" },
};

static const moto_dtc_entry_t moto_dtc_table_honda[] = {
    { "P0300", "Random/multiple cylinder misfire (PGM-FI)" },
    { "P1300", "Ignition pulse generator circuit malfunction" },
    { "P1455", "Fuel tank pressure sensor malfunction" },
    { "P1456", "EVAP control system leak detected" },
    { "P1600", "ECM internal circuit malfunction" },
    { "P0115", "Engine coolant temperature sensor malfunction" },
};

static const moto_dtc_entry_t moto_dtc_table_yamaha[] = {
    { "P0300", "Cylinder misfire detected (YDET)" },
    { "P1200", "Crankshaft position sensor malfunction" },
    { "P1500", "Fuel pump circuit malfunction" },
    { "P1600", "ECU communication error" },
    { "P0115", "Coolant temperature sensor malfunction" },
};

static const moto_dtc_entry_t moto_dtc_table_suzuki[] = {
    { "P0300", "Misfire detected" },
    { "P1000", "ECM malfunction" },
    { "P1100", "Intake air pressure sensor malfunction" },
    { "P1200", "Crankshaft position sensor malfunction" },
    { "P1600", "ECU internal fault" },
};

static const moto_dtc_entry_t moto_dtc_table_kawasaki[] = {
    { "P0300", "Misfire detected" },
    { "P1100", "Intake air pressure sensor malfunction" },
    { "P1200", "Crankshaft position sensor malfunction" },
    { "P1500", "Fuel pump malfunction" },
    { "P1600", "ECU malfunction" },
};

typedef struct {
    moto_brand_t            brand;
    const moto_dtc_entry_t *table;
    size_t                  count;
} moto_brand_table_t;

static const moto_brand_table_t moto_brand_tables[] = {
    { MOTO_BRAND_HONDA,    moto_dtc_table_honda,
        sizeof(moto_dtc_table_honda) / sizeof(moto_dtc_table_honda[0]) },
    { MOTO_BRAND_YAMAHA,   moto_dtc_table_yamaha,
        sizeof(moto_dtc_table_yamaha) / sizeof(moto_dtc_table_yamaha[0]) },
    { MOTO_BRAND_SUZUKI,   moto_dtc_table_suzuki,
        sizeof(moto_dtc_table_suzuki) / sizeof(moto_dtc_table_suzuki[0]) },
    { MOTO_BRAND_KAWASAKI, moto_dtc_table_kawasaki,
        sizeof(moto_dtc_table_kawasaki) / sizeof(moto_dtc_table_kawasaki[0]) },
};

#define MOTO_BRAND_TABLE_COUNT \
    (sizeof(moto_brand_tables) / sizeof(moto_brand_tables[0]))

/* Map a moto_brand_t to the brand name understood by protocol/dtc.c.
 * NULL means generic lookup. */
static const char *moto_brand_name(moto_brand_t brand)
{
    switch (brand) {
        case MOTO_BRAND_HONDA:    return "honda";
        case MOTO_BRAND_YAMAHA:   return "yamaha";
        case MOTO_BRAND_SUZUKI:   return "suzuki";
        case MOTO_BRAND_KAWASAKI: return "kawasaki";
        default:                  return NULL;   /* MOTO_BRAND_GENERIC / unknown */
    }
}

/* Search one brand table for `code`. Returns the description or NULL. */
static const char *moto_table_lookup(moto_brand_t brand, const char *code)
{
    const moto_dtc_entry_t *table = NULL;
    size_t count = 0;

    for (size_t t = 0; t < MOTO_BRAND_TABLE_COUNT; t++) {
        if (moto_brand_tables[t].brand == brand) {
            table = moto_brand_tables[t].table;
            count = moto_brand_tables[t].count;
            break;
        }
    }
    if (table == NULL || code == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        if (strcmp(table[i].code, code) == 0) {
            return table[i].desc;
        }
    }
    return NULL;
}

/* Score how many of the ECU's codes appear in one brand table. */
static size_t moto_table_score(const moto_dtc_entry_t *table, size_t count,
                               const dtc_entry_t *entries, size_t n_entries)
{
    size_t score = 0;
    for (size_t i = 0; i < n_entries; i++) {
        for (size_t j = 0; j < count; j++) {
            if (strcmp(entries[i].code, table[j].code) == 0) {
                score++;
                break;
            }
        }
    }
    return score;
}

/* ------------------------------------------------------------------------
 * Map protocol status codes onto motorcycle status codes.
 * ------------------------------------------------------------------------ */

static motorcycle_status_t motorcycle_from_proto(proto_status_t st)
{
    switch (st) {
        case PROTO_OK:                    return MOTORCYCLE_OK;
        case PROTO_ERR_INVALID_ARG:       return MOTORCYCLE_ERR_INVALID_ARG;
        case PROTO_ERR_NOT_INITIALIZED:   return MOTORCYCLE_ERR_NOT_INITIALIZED;
        default:                          return MOTORCYCLE_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

motorcycle_status_t motorcycle_init(const motorcycle_config_t *cfg,
                                    motorcycle_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL) {
        return MOTORCYCLE_ERR_INVALID_ARG;
    }

    motorcycle_t *moto = (motorcycle_t *)calloc(1, sizeof(*moto));
    if (moto == NULL) {
        return MOTORCYCLE_ERR_INVALID_ARG;
    }

    moto->cfg         = *cfg;        /* shallow copy: proto pointer only */
    moto->brand       = MOTO_BRAND_GENERIC;
    moto->initialized = true;

    *out = moto;
    return MOTORCYCLE_OK;
}

motorcycle_status_t motorcycle_deinit(motorcycle_t *moto)
{
    if (moto == NULL) {
        return MOTORCYCLE_ERR_INVALID_ARG;
    }
    moto->initialized = false;
    free(moto);
    return MOTORCYCLE_OK;
}

bool motorcycle_is_initialized(const motorcycle_t *moto)
{
    return moto != NULL && moto->initialized;
}

/* ------------------------------------------------------------------------
 * Brand / DTC operations
 * ------------------------------------------------------------------------ */

motorcycle_status_t motorcycle_detect_brand(motorcycle_t *moto,
                                            moto_brand_t *brand, uint32_t timeout_ms)
{
    if (moto == NULL || brand == NULL) {
        return MOTORCYCLE_ERR_INVALID_ARG;
    }
    if (!moto->initialized) {
        return MOTORCYCLE_ERR_NOT_INITIALIZED;
    }
    if (moto->proto == NULL || !proto_is_initialized(moto->proto)) {
        return MOTORCYCLE_ERR_NOT_INITIALIZED;
    }

    dtc_entry_t entries[8];
    dtc_list_t list = { .entries = entries, .count = 0, .capacity = sizeof(entries) / sizeof(entries[0]) };

    proto_status_t pst = proto_read_dtcs(moto->proto, J1979_MODE_DTC_CONFIRMED,
                                         &list, timeout_ms);
    if (pst != PROTO_OK) {
        return MOTORCYCLE_ERR_PROTOCOL;
    }

    moto_brand_t best = MOTO_BRAND_GENERIC;
    size_t best_score = 0;
    for (size_t t = 0; t < MOTO_BRAND_TABLE_COUNT; t++) {
        size_t score = moto_table_score(moto_brand_tables[t].table,
                                        moto_brand_tables[t].count,
                                        list.entries, list.count);
        if (score > best_score) {
            best_score = score;
            best = moto_brand_tables[t].brand;
        }
    }

    moto->brand = best;
    *brand = best;
    return MOTORCYCLE_OK;
}

motorcycle_status_t motorcycle_dtc_lookup(motorcycle_t *moto, const char *code,
                                          char *desc, size_t desc_len)
{
    if (moto == NULL || code == NULL || desc == NULL || desc_len == 0) {
        return MOTORCYCLE_ERR_INVALID_ARG;
    }
    if (!moto->initialized) {
        return MOTORCYCLE_ERR_NOT_INITIALIZED;
    }

    /* 1. Brand-specific table first. */
    const char *found = moto_table_lookup(moto->brand, code);
    if (found != NULL) {
        snprintf(desc, desc_len, "%s", found);
        return MOTORCYCLE_OK;
    }

    /* 2. Fall back to the protocol's generic/brand lookup table. */
    const char *brand_name = moto_brand_name(moto->brand);
    dtc_status_t dst = dtc_lookup(brand_name, code, desc, desc_len);
    if (dst == DTC_OK) {
        return MOTORCYCLE_OK;
    }

    return MOTORCYCLE_ERR_NOT_FOUND;
}

motorcycle_status_t motorcycle_read_blink_codes(motorcycle_t *moto,
                                                uint8_t *codes, size_t max_codes,
                                                size_t *out_count, uint32_t timeout_ms)
{
    if (moto == NULL) {
        return MOTORCYCLE_ERR_INVALID_ARG;
    }
    if (!moto->initialized) {
        return MOTORCYCLE_ERR_NOT_INITIALIZED;
    }
    (void)codes;
    (void)max_codes;
    (void)out_count;
    (void)timeout_ms;

    /* Stub: the OBD1 blink parser (protocol/blink_parser.h) is not yet
     * implemented, so blink-code reading cannot be done yet. */
    return MOTORCYCLE_ERR_OBD1_PENDING;
}
