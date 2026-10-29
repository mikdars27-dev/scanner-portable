/*
 * app/smartkey_detect.c — Smart Key Detector implementation.
 *
 * Mendeteksi keberadaan smart key (keyless entry/passive entry) di sekitar
 * kendaraan, membaca status baterai kunci, dan kekuatan sinyal (RSSI).
 *
 * Alur deteksi:
 *   1. Kirim SID deteksi keberadaan (0x58) via proto_send()
 *   2. Parse respons: detected, rssi, key_id
 *   3. Opsional: baca baterai (0x59) dan sinyal (0x5A)
 *
 * Safety: deteksi hanya baca (read-only), tidak memerlukan warning screen.
 *
 * Layering: APP -> smartkey_detect_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "smartkey_detect.h"

#include <Arduino.h>
#include <string.h>

#include "ui/warning_screen.h"   /* warning_show, WARN_CUSTOM */

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

/* Default timeouts (ms): scan lebih lama, status lebih cepat. */
#define SMARTKEY_DETECT_TIMEOUT_SCAN_MS   6000U
#define SMARTKEY_DETECT_TIMEOUT_STATUS_MS 3000U

/* SID produsen untuk deteksi smart key. Nilai default mencakup implementasi
 * umum; disesuaikan per merek kendaraan. */
#define SMARTKEY_SID_SCAN_PRESENCE  0x58U   /* scan keberadaan kunci */
#define SMARTKEY_SID_READ_BATTERY   0x59U   /* baca status baterai kunci */
#define SMARTKEY_SID_READ_SIGNAL    0x5AU   /* baca kekuatan sinyal (RSSI) */

/* Maksimum ukuran buffer request deteksi smart key. */
#define SMARTKEY_DETECT_REQ_BUF_SIZE 16U

/* ------------------------------------------------------------------------
 * Status mapping helpers
 * ------------------------------------------------------------------------ */

/* Map proto_status_t -> smartkey_detect_status_t. */
static smartkey_detect_status_t smartkey_detect_from_proto(proto_status_t st)
{
    switch (st) {
    case PROTO_OK:                    return SMARTKEY_DETECT_OK;
    case PROTO_ERR_INVALID_ARG:       return SMARTKEY_DETECT_ERR_INVALID_ARG;
    case PROTO_ERR_NOT_INITIALIZED:   return SMARTKEY_DETECT_ERR_NOT_INITIALIZED;
    case PROTO_ERR_TIMEOUT:           return SMARTKEY_DETECT_ERR_TIMEOUT;
    case PROTO_ERR_PROTOCOL:          return SMARTKEY_DETECT_ERR_PROTOCOL;
    case PROTO_ERR_BUFFER_TOO_SMALL:  return SMARTKEY_DETECT_ERR_PROTOCOL;
    case PROTO_ERR_NEGATIVE_RESPONSE: return SMARTKEY_DETECT_ERR_PROTOCOL;
    default:                          return SMARTKEY_DETECT_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Opaque handle
 * ------------------------------------------------------------------------ */

struct smartkey_detect {
    smartkey_detect_config_t cfg;
    bool                      initialized;
};

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

smartkey_detect_status_t smartkey_detect_init(const smartkey_detect_config_t *cfg,
                                               smartkey_detect_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL || cfg->disp == NULL) {
        return SMARTKEY_DETECT_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return SMARTKEY_DETECT_ERR_NOT_INITIALIZED;
    }

    smartkey_detect_t *sk = (smartkey_detect_t *)calloc(1, sizeof(*sk));
    if (sk == NULL) {
        return SMARTKEY_DETECT_ERR_INVALID_ARG;
    }

    sk->cfg         = *cfg;
    sk->initialized = true;

    *out = sk;
    return SMARTKEY_DETECT_OK;
}

smartkey_detect_status_t smartkey_detect_deinit(smartkey_detect_t *sk)
{
    if (sk == NULL) {
        return SMARTKEY_DETECT_ERR_INVALID_ARG;
    }
    sk->initialized = false;
    free(sk);
    return SMARTKEY_DETECT_OK;
}

bool smartkey_detect_is_initialized(const smartkey_detect_t *sk)
{
    return sk != NULL && sk->initialized;
}

/* ------------------------------------------------------------------------
 * Protocol helper: raw request/response
 * ------------------------------------------------------------------------ */

/* Kirim request deteksi smart key dengan SID tertentu dan parsing sederhana.
 * resp_len diisi dengan panjang respons yang diterima. */
static smartkey_detect_status_t smartkey_detect_send_cmd(smartkey_detect_t *sk,
                                                          uint8_t sid,
                                                          const uint8_t *req, size_t req_len,
                                                          uint8_t *resp, size_t *resp_len,
                                                          uint32_t timeout_ms)
{
    if (sk == NULL || !sk->initialized || resp == NULL || resp_len == NULL) {
        return SMARTKEY_DETECT_ERR_INVALID_ARG;
    }

    proto_status_t st = proto_send(sk->cfg.proto, sid, req, req_len,
                                   resp, resp_len, timeout_ms);
    return smartkey_detect_from_proto(st);
}

/* ------------------------------------------------------------------------
 * Scan presence
 * ------------------------------------------------------------------------ */

smartkey_detect_status_t smartkey_detect_scan(smartkey_detect_t *sk,
                                               smartkey_info_t *info,
                                               uint32_t timeout_ms)
{
    if (sk == NULL || !sk->initialized) {
        return SMARTKEY_DETECT_ERR_NOT_INITIALIZED;
    }
    if (info == NULL) {
        return SMARTKEY_DETECT_ERR_INVALID_ARG;
    }

    /* Reset info ke default. */
    memset(info, 0, sizeof(*info));
    info->rssi_dbm   = -128;
    info->battery_level = 255;

    /* Kirim request scan keberadaan. Tidak perlu data tambahan. */
    uint8_t req[] = { SMARTKEY_SID_SCAN_PRESENCE };
    uint8_t resp[64];
    size_t  resp_len = sizeof(resp);

    smartkey_detect_status_t st = smartkey_detect_send_cmd(
            sk, SMARTKEY_SID_SCAN_PRESENCE, req, sizeof(req),
            resp, &resp_len, timeout_ms);
    if (st != SMARTKEY_DETECT_OK) {
        return st;
    }

    /* Parse respons (format manufacturer-specific, asumsi sederhana):
     * resp[0] = detected (0/1)
     * resp[1] = rssi (signed dBm)
     * resp[2] = key_id_len
     * resp[3..] = key_id bytes */
    size_t offset = 0;
    if (offset < resp_len) {
        info->detected = resp[offset++];
    }
    if (offset < resp_len) {
        info->rssi_dbm = (int8_t)resp[offset++];
    }
    if (offset < resp_len) {
        uint8_t kid_len = resp[offset++];
        if (kid_len > SMARTKEY_ID_MAX_LEN) {
            kid_len = SMARTKEY_ID_MAX_LEN;
        }
        if (offset + kid_len <= resp_len) {
            memcpy(info->key_id, resp + offset, kid_len);
            info->key_id_len = kid_len;
            offset += kid_len;
        }
    }

    info->last_seen_ms = millis();

    if (!info->detected) {
        return SMARTKEY_DETECT_ERR_KEY_NOT_FOUND;
    }

    return SMARTKEY_DETECT_OK;
}

/* ------------------------------------------------------------------------
 * Read battery
 * ------------------------------------------------------------------------ */

smartkey_detect_status_t smartkey_detect_battery(smartkey_detect_t *sk,
                                                  uint8_t *level,
                                                  uint32_t timeout_ms)
{
    if (sk == NULL || !sk->initialized) {
        return SMARTKEY_DETECT_ERR_NOT_INITIALIZED;
    }
    if (level == NULL) {
        return SMARTKEY_DETECT_ERR_INVALID_ARG;
    }

    *level = 255; /* default: tidak diketahui */

    uint8_t req[] = { SMARTKEY_SID_READ_BATTERY };
    uint8_t resp[16];
    size_t  resp_len = sizeof(resp);

    smartkey_detect_status_t st = smartkey_detect_send_cmd(
            sk, SMARTKEY_SID_READ_BATTERY, req, sizeof(req),
            resp, &resp_len, timeout_ms);
    if (st != SMARTKEY_DETECT_OK) {
        return st;
    }

    if (resp_len > 0) {
        *level = resp[0];
    }

    return SMARTKEY_DETECT_OK;
}

/* ------------------------------------------------------------------------
 * Read signal strength
 * ------------------------------------------------------------------------ */

smartkey_detect_status_t smartkey_detect_signal_strength(smartkey_detect_t *sk,
                                                          int8_t *rssi,
                                                          uint32_t timeout_ms)
{
    if (sk == NULL || !sk->initialized) {
        return SMARTKEY_DETECT_ERR_NOT_INITIALIZED;
    }
    if (rssi == NULL) {
        return SMARTKEY_DETECT_ERR_INVALID_ARG;
    }

    *rssi = -128; /* default: tidak diketahui */

    uint8_t req[] = { SMARTKEY_SID_READ_SIGNAL };
    uint8_t resp[16];
    size_t  resp_len = sizeof(resp);

    smartkey_detect_status_t st = smartkey_detect_send_cmd(
            sk, SMARTKEY_SID_READ_SIGNAL, req, sizeof(req),
            resp, &resp_len, timeout_ms);
    if (st != SMARTKEY_DETECT_OK) {
        return st;
    }

    if (resp_len > 0) {
        *rssi = (int8_t)resp[0];
    }

    return SMARTKEY_DETECT_OK;
}
