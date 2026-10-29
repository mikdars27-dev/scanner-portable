/*
 * app/key_program.c — Key Programming / Smartkey implementation.
 *
 * Menduplikasi atau mendaftarkan remote/keyless baru langsung melalui scanner.
 * Alur operasi:
 *   1. Buka sesi pemrograman (UDS $10 session = programming)
 *   2. Security access (UDS $27 seed/key)
 *   3. Kirim data kunci via proto_send() dengan SID pemrograman kunci
 *   4. Tutup sesi pemrograman
 *
 * Safety: warning_show(WARN_CUSTOM) sebelum add/delete/duplicate.
 *
 * Layering: APP -> key_prog_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "key_program.h"

#include <Arduino.h>
#include <string.h>

#include "ui/warning_screen.h"   /* warning_show, WARN_CUSTOM */
#include "ui/buttons.h"          /* BTN_SELECT (via warning_show internal) */

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

/* Default timeouts (ms): operasi baca lebih pendek, pemrograman lebih lama. */
#define KEY_PROG_TIMEOUT_READ_MS   5000U
#define KEY_PROG_TIMEOUT_PROG_MS  10000U

/* SID produsen untuk pemrograman kunci (key programming). Nilai default
 * mencakup implementasi umum; disesuaikan per merek kendaraan. */
#define KEY_PROG_SID_READ_KEYS   0x51U   /* baca daftar kunci */
#define KEY_PROG_SID_ADD_KEY     0x52U   /* tambah kunci */
#define KEY_PROG_SID_DELETE_KEY  0x53U   /* hapus kunci */
#define KEY_PROG_SID_DUPLICATE   0x54U   /* duplikasi kunci */

/* Security access level untuk key programming. */
#define KEY_PROG_SECURITY_LEVEL  0x01U

/* Maksimum ukuran buffer request pemrograman kunci. */
#define KEY_PROG_REQ_BUF_SIZE 32U

/* ------------------------------------------------------------------------
 * Status mapping helpers
 * ------------------------------------------------------------------------ */

/* Map proto_status_t -> key_prog_status_t. */
static key_prog_status_t key_prog_from_proto(proto_status_t st)
{
    switch (st) {
    case PROTO_OK:                    return KEY_PROG_OK;
    case PROTO_ERR_INVALID_ARG:       return KEY_PROG_ERR_INVALID_ARG;
    case PROTO_ERR_NOT_INITIALIZED:   return KEY_PROG_ERR_NOT_INITIALIZED;
    case PROTO_ERR_TIMEOUT:           return KEY_PROG_ERR_TIMEOUT;
    case PROTO_ERR_PROTOCOL:          return KEY_PROG_ERR_PROTOCOL;
    case PROTO_ERR_BUFFER_TOO_SMALL:  return KEY_PROG_ERR_PROTOCOL;
    case PROTO_ERR_NEGATIVE_RESPONSE: return KEY_PROG_ERR_PROTOCOL;
    default:                          return KEY_PROG_ERR_PROTOCOL;
    }
}

/* Derive the security-access key from the seed using a manufacturer-specific
 * algorithm. This replaces the placeholder that used key_data directly as the
 * key. In production, replace the XOR constant below with the actual OEM key
 * derivation algorithm (hash, cipher, or lookup per manufacturer spec). */
static void key_prog_derive_key(const uint8_t *seed, size_t seed_len,
                                 uint8_t *key, size_t *key_len)
{
    static const uint8_t mfg_constant[] = {
        0xA5, 0x5A, 0x3C, 0xC3, 0x7E, 0xE7, 0x96, 0x69
    };
    size_t n = seed_len < *key_len ? seed_len : *key_len;
    for (size_t i = 0; i < n; i++) {
        key[i] = (uint8_t)(seed[i] ^ mfg_constant[i % sizeof(mfg_constant)]);
    }
    *key_len = n;
}

/* ------------------------------------------------------------------------
 * Opaque handle
 * ------------------------------------------------------------------------ */

struct key_prog {
    key_prog_config_t cfg;
    bool               initialized;
};

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

key_prog_status_t key_prog_init(const key_prog_config_t *cfg,
                                key_prog_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL || cfg->disp == NULL) {
        return KEY_PROG_ERR_INVALID_ARG;
    }
    if (!proto_is_initialized(cfg->proto)) {
        return KEY_PROG_ERR_NOT_INITIALIZED;
    }

    key_prog_t *kp = (key_prog_t *)calloc(1, sizeof(*kp));
    if (kp == NULL) {
        return KEY_PROG_ERR_INVALID_ARG;
    }

    kp->cfg         = *cfg;
    kp->initialized = true;

    *out = kp;
    return KEY_PROG_OK;
}

key_prog_status_t key_prog_deinit(key_prog_t *kp)
{
    if (kp == NULL) {
        return KEY_PROG_ERR_INVALID_ARG;
    }
    kp->initialized = false;
    free(kp);
    return KEY_PROG_OK;
}

bool key_prog_is_initialized(const key_prog_t *kp)
{
    return kp != NULL && kp->initialized;
}

/* ------------------------------------------------------------------------
 * Protocol helpers: session + security access
 * ------------------------------------------------------------------------ */

/* Masuk ke sesi pemrograman (UDS $10 DiagnosticSessionControl,
 * session = programming). Mengembalikan KEY_PROG_OK jika berhasil. */
static key_prog_status_t key_prog_enter_programming_session(key_prog_t *kp,
                                                             uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized) {
        return KEY_PROG_ERR_NOT_INITIALIZED;
    }

    /* Coba via K-Line/UDS session control terlebih dahulu. */
    proto_status_t st = kline_proto_uds_session(proto_kline_handle(kp->cfg.proto),
            KLINE_PROTO_SESSION_PROGRAMMING, timeout_ms);
    if (st == PROTO_OK) {
        return KEY_PROG_OK;
    }

    /* Fallback: kirim request sesi pemrograman via proto_send() dengan
     * SID produsen untuk key programming. */
    uint8_t req[] = { 0x10, KLINE_PROTO_SESSION_PROGRAMMING };
    uint8_t resp[64];
    size_t  resp_len = sizeof(resp);
    st = proto_send(kp->cfg.proto, KEY_PROG_SID_READ_KEYS, req, sizeof(req),
                    resp, &resp_len, timeout_ms);
    return key_prog_from_proto(st);
}

/* Keluar dari sesi pemrograman (kembali ke default session). */
static key_prog_status_t key_prog_exit_programming_session(key_prog_t *kp,
                                                            uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized) {
        return KEY_PROG_ERR_NOT_INITIALIZED;
    }

    proto_status_t st = kline_proto_uds_session(kp->cfg.proto,
            KLINE_PROTO_SESSION_DEFAULT, timeout_ms);
    return key_prog_from_proto(st);
}

/* Security access: request seed (UDS $27, level ganjil). */
static key_prog_status_t key_prog_request_seed(key_prog_t *kp,
                                                uint8_t *seed, size_t *seed_len,
                                                uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized || seed == NULL || seed_len == NULL) {
        return KEY_PROG_ERR_INVALID_ARG;
    }

    proto_status_t st = kline_proto_uds_seed(proto_kline_handle(kp->cfg.proto),
            KEY_PROG_SECURITY_LEVEL, seed, seed_len, timeout_ms);
    return key_prog_from_proto(st);
}

/* Security access: send key (UDS $27, level genap). */
static key_prog_status_t key_prog_send_key(key_prog_t *kp,
                                            const uint8_t *key, size_t key_len,
                                            uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized || key == NULL) {
        return KEY_PROG_ERR_INVALID_ARG;
    }

    proto_status_t st = kline_proto_uds_key(proto_kline_handle(kp->cfg.proto),
            KEY_PROG_SECURITY_LEVEL + 1U, key, key_len, timeout_ms);
    return key_prog_from_proto(st);
}

/* ------------------------------------------------------------------------
 * Read existing keys
 * ------------------------------------------------------------------------ */

key_prog_status_t key_prog_read_keys(key_prog_t *kp,
                                      key_info_t *keys, size_t *count,
                                      uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized) {
        return KEY_PROG_ERR_NOT_INITIALIZED;
    }
    if (keys == NULL || count == NULL || *count == 0) {
        return KEY_PROG_ERR_INVALID_ARG;
    }

    /* Masuk ke sesi pemrograman (read-only tidak perlu security access penuh,
     * tapi beberapa ECU memerlukan session programming untuk baca key data). */
    key_prog_status_t st = key_prog_enter_programming_session(kp, timeout_ms);
    if (st != KEY_PROG_OK) {
        return st;
    }

    /* Kirim request baca kunci. */
    uint8_t req[] = { KEY_PROG_SID_READ_KEYS };
    uint8_t resp[256];
    size_t  resp_len = sizeof(resp);

    st = key_prog_from_proto(
            proto_send(kp->cfg.proto, KEY_PROG_SID_READ_KEYS, req, sizeof(req),
                       resp, &resp_len, timeout_ms));
    if (st != KEY_PROG_OK) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return st;
    }

    /* Parse respons: format manufacturer-specific. Asumsi sederhana:
     * resp[0] = jumlah kunci, diikuti entri kunci (index, type, data...). */
    size_t key_count = resp_len > 0 ? resp[0] : 0;
    if (key_count > *count) {
        key_count = *count;
    }

    size_t offset = 1;
    for (size_t i = 0; i < key_count && offset < resp_len; i++) {
        keys[i].key_index   = (offset < resp_len) ? resp[offset++] : 0;
        keys[i].key_type    = (offset < resp_len)
                ? (key_prog_key_type_t)resp[offset++] : KEY_TYPE_TRANSPONDER;
        keys[i].transponder_len = 0;
        keys[i].remote_len    = 0;
        keys[i].programmed    = 1;

        /* Baca transponder ID (banyaknya byte bervariasi per produsen). */
        uint8_t tlen = (offset < resp_len) ? resp[offset++] : 0;
        if (tlen > 16) tlen = 16;
        if (offset + tlen <= resp_len) {
            memcpy(keys[i].transponder_id, resp + offset, tlen);
            keys[i].transponder_len = tlen;
            offset += tlen;
        }

        /* Baca remote ID. */
        uint8_t rlen = (offset < resp_len) ? resp[offset++] : 0;
        if (rlen > 16) rlen = 16;
        if (offset + rlen <= resp_len) {
            memcpy(keys[i].remote_id, resp + offset, rlen);
            keys[i].remote_len = rlen;
            offset += rlen;
        }
    }

    *count = key_count;

    /* Keluar dari sesi pemrograman. */
    (void)key_prog_exit_programming_session(kp, timeout_ms);

    return KEY_PROG_OK;
}

/* ------------------------------------------------------------------------
 * Add key
 * ------------------------------------------------------------------------ */

key_prog_status_t key_prog_add_key(key_prog_t *kp,
                                    key_prog_key_type_t type,
                                    const uint8_t *key_data, size_t key_data_len,
                                    uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized) {
        return KEY_PROG_ERR_NOT_INITIALIZED;
    }
    if (key_data == NULL || key_data_len == 0 || key_data_len > 16) {
        return KEY_PROG_ERR_INVALID_ARG;
    }

    /* Safety: konfirmasi pengguna sebelum memprogram kunci baru. */
    if (!warning_show(kp->cfg.disp, WARN_CUSTOM,
                      "Tambah kunci baru? Tekan SELECT", timeout_ms)) {
        return KEY_PROG_ERR_USER_ABORT;
    }

    /* Masuk ke sesi pemrograman. */
    key_prog_status_t st = key_prog_enter_programming_session(kp, timeout_ms);
    if (st != KEY_PROG_OK) {
        return st;
    }

    /* Security access: request seed. */
    uint8_t seed[16];
    size_t  seed_len = sizeof(seed);
    st = key_prog_request_seed(kp, seed, &seed_len, timeout_ms);
    if (st != KEY_PROG_OK) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return st;
    }

    /* Derive the security key from the seed (manufacturer-specific algorithm),
     * replacing the placeholder that used key_data directly as the key. */
    uint8_t derived_key[16];
    size_t  derived_key_len = sizeof(derived_key);
    key_prog_derive_key(seed, seed_len, derived_key, &derived_key_len);

    st = key_prog_send_key(kp, derived_key, derived_key_len, timeout_ms);
    if (st != KEY_PROG_OK) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return st;
    }

    /* Kirim perintah tambah kunci. */
    size_t req_len = 1U + 1U + key_data_len; /* SID + type + data */
    if (req_len > KEY_PROG_REQ_BUF_SIZE) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return KEY_PROG_ERR_PROTOCOL;
    }
    uint8_t req[KEY_PROG_REQ_BUF_SIZE];
    req[0] = KEY_PROG_SID_ADD_KEY;
    req[1] = (uint8_t)type;
    memcpy(req + 2, key_data, key_data_len);

    uint8_t resp[64];
    size_t  resp_len = sizeof(resp);
    st = key_prog_from_proto(
            proto_send(kp->cfg.proto, KEY_PROG_SID_ADD_KEY, req, req_len,
                       resp, &resp_len, timeout_ms));

    /* Keluar dari sesi pemrograman. */
    (void)key_prog_exit_programming_session(kp, timeout_ms);

    return st;
}

/* ------------------------------------------------------------------------
 * Delete key
 * ------------------------------------------------------------------------ */

key_prog_status_t key_prog_delete_key(key_prog_t *kp,
                                       uint8_t key_index,
                                       uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized) {
        return KEY_PROG_ERR_NOT_INITIALIZED;
    }

    /* Safety: konfirmasi pengguna sebelum menghapus kunci. */
    if (!warning_show(kp->cfg.disp, WARN_CUSTOM,
                      "Hapus kunci? Tekan SELECT", timeout_ms)) {
        return KEY_PROG_ERR_USER_ABORT;
    }

    key_prog_status_t st = key_prog_enter_programming_session(kp, timeout_ms);
    if (st != KEY_PROG_OK) {
        return st;
    }

    /* Security access. */
    uint8_t seed[16];
    size_t  seed_len = sizeof(seed);
    st = key_prog_request_seed(kp, seed, &seed_len, timeout_ms);
    if (st != KEY_PROG_OK) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return st;
    }

    uint8_t derived_key[16];
    size_t  derived_key_len = sizeof(derived_key);
    key_prog_derive_key(seed, seed_len, derived_key, &derived_key_len);

    st = key_prog_send_key(kp, derived_key, derived_key_len, timeout_ms);
    if (st != KEY_PROG_OK) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return st;
    }

    /* Kirim perintah hapus kunci. */
    uint8_t req[] = { KEY_PROG_SID_DELETE_KEY, key_index };
    uint8_t resp[64];
    size_t  resp_len = sizeof(resp);
    st = key_prog_from_proto(
            proto_send(kp->cfg.proto, KEY_PROG_SID_DELETE_KEY, req, sizeof(req),
                       resp, &resp_len, timeout_ms));

    (void)key_prog_exit_programming_session(kp, timeout_ms);
    return st;
}

/* ------------------------------------------------------------------------
 * Duplicate key
 * ------------------------------------------------------------------------ */

key_prog_status_t key_prog_duplicate_key(key_prog_t *kp,
                                          uint8_t source_key_index,
                                          uint32_t timeout_ms)
{
    if (kp == NULL || !kp->initialized) {
        return KEY_PROG_ERR_NOT_INITIALIZED;
    }

    /* Safety: konfirmasi pengguna sebelum duplikasi kunci. */
    if (!warning_show(kp->cfg.disp, WARN_CUSTOM,
                      "Duplikasi kunci? Tekan SELECT", timeout_ms)) {
        return KEY_PROG_ERR_USER_ABORT;
    }

    /* Baca data kunci sumber terlebih dahulu. */
    key_info_t keys[KEY_PROG_MAX_KEYS];
    size_t     count = sizeof(keys) / sizeof(keys[0]);
    key_prog_status_t st = key_prog_read_keys(kp, keys, &count, timeout_ms);
    if (st != KEY_PROG_OK) {
        return st;
    }

    /* Cari kunci sumber. */
    const key_info_t *src = NULL;
    for (size_t i = 0; i < count; i++) {
        if (keys[i].key_index == source_key_index && keys[i].programmed) {
            src = &keys[i];
            break;
        }
    }
    if (src == NULL) {
        return KEY_PROG_ERR_INVALID_ARG;
    }

    /* Masuk ke sesi pemrograman. */
    st = key_prog_enter_programming_session(kp, timeout_ms);
    if (st != KEY_PROG_OK) {
        return st;
    }

    /* Security access. */
    uint8_t seed[16];
    size_t  seed_len = sizeof(seed);
    st = key_prog_request_seed(kp, seed, &seed_len, timeout_ms);
    if (st != KEY_PROG_OK) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return st;
    }

    uint8_t derived_key[16];
    size_t  derived_key_len = sizeof(derived_key);
    key_prog_derive_key(seed, seed_len, derived_key, &derived_key_len);

    st = key_prog_send_key(kp, derived_key, derived_key_len, timeout_ms);
    if (st != KEY_PROG_OK) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return st;
    }

    /* Kirim data kunci sumber untuk diduplikasi. */
    size_t data_len = src->transponder_len > 0 ? src->transponder_len
                                               : src->remote_len;
    if (data_len == 0 || data_len > 16) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return KEY_PROG_ERR_INVALID_ARG;
    }

    size_t req_len = 1U + 1U + 1U + data_len; /* SID + source_index + type + data */
    if (req_len > KEY_PROG_REQ_BUF_SIZE) {
        key_prog_exit_programming_session(kp, timeout_ms);
        return KEY_PROG_ERR_PROTOCOL;
    }
    uint8_t req[KEY_PROG_REQ_BUF_SIZE];
    req[0] = KEY_PROG_SID_DUPLICATE;
    req[1] = source_key_index;
    req[2] = (uint8_t)src->key_type;
    if (src->transponder_len > 0) {
        memcpy(req + 3, src->transponder_id, data_len);
    } else {
        memcpy(req + 3, src->remote_id, data_len);
    }

    uint8_t resp[64];
    size_t  resp_len = sizeof(resp);
    st = key_prog_from_proto(
            proto_send(kp->cfg.proto, KEY_PROG_SID_DUPLICATE, req, req_len,
                       resp, &resp_len, timeout_ms));

    (void)key_prog_exit_programming_session(kp, timeout_ms);
    return st;
}
