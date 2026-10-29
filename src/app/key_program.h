/*
 * app/key_program.h — Key Programming / Smartkey (Fitur: Program Kunci).
 *
 * Menduplikasi atau mendaftarkan remote/keyless baru langsung melalui scanner.
 * Menggunakan sesi diagnostik (UDS $10 programming), security access (UDS $27
 * seed/key), dan pemrograman data kunci via proto_send() / kline_proto_*.
 *
 * Safety: warning_show(WARN_CUSTOM) sebelum operasi add/delete/duplicate —
 * pengaturan kunci memengaruhi keamanan immobilizer kendaraan. SELECT wajib.
 * Read-only (read_keys) tidak memerlukan warning screen.
 *
 * Layering: APP -> key_prog_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_KEY_PROG_H
#define APP_KEY_PROG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/protocol.h"     /* protocol_t, proto_send, proto_is_initialized */
#include "protocol/kline_proto.h"  /* kline_proto_uds_session, kline_proto_uds_seed, kline_proto_uds_key */
#include "ui/warning_screen.h"     /* warning_show, WARN_CUSTOM, display_t */
#include "ui/display.h"            /* display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    KEY_PROG_OK                    = 0,
    KEY_PROG_ERR_INVALID_ARG       = -1,
    KEY_PROG_ERR_NOT_INITIALIZED   = -2,
    KEY_PROG_ERR_PROTOCOL          = -3,   /* underlying protocol failure */
    KEY_PROG_ERR_TIMEOUT           = -4,   /* command timeout */
    KEY_PROG_ERR_USER_ABORT        = -5,   /* user declined via warning screen */
    KEY_PROG_ERR_SAFETY            = -6,   /* safety condition not met */
    KEY_PROG_ERR_NOT_SUPPORTED     = -7,   /* transport or operation not supported */
} key_prog_status_t;

/* =========================================================================
 * Key programming operation types
 * ========================================================================= */

typedef enum {
    KEY_PROG_OP_READ_EXISTING   = 0,   /* Baca kunci yang sudah terdaftar di ECU */
    KEY_PROG_OP_ADD_KEY         = 1,   /* Tambah / daftarkan kunci baru */
    KEY_PROG_OP_DELETE_KEY      = 2,   /* Hapus kunci dari ECU */
    KEY_PROG_OP_DUPLICATE_KEY   = 3,   /* Duplikasi kunci dari slot sumber */
} key_prog_op_t;

/* =========================================================================
 * Key types
 * ========================================================================= */

typedef enum {
    KEY_TYPE_TRANSPONDER  = 0,   /* chip transponder immobilizer */
    KEY_TYPE_REMOTE       = 1,   /* remote keyless entry (RF) */
    KEY_TYPE_SMARTKEY     = 2,   /* passive smart key / passive entry */
} key_prog_key_type_t;

/* =========================================================================
 * Key info
 * ========================================================================= */

#define KEY_PROG_MAX_KEYS 16

typedef struct {
    uint8_t              key_index;       /* slot index di ECU */
    key_prog_key_type_t  key_type;        /* tipe kunci */
    uint8_t              transponder_id[16]; /* ID chip transponder */
    uint8_t              remote_id[16];    /* ID remote/keyless */
    size_t               transponder_len;  /* panjang bytes transponder yang valid */
    size_t               remote_len;       /* panjang bytes remote yang valid */
    uint8_t              programmed;       /* 1 = terprogram, 0 = slot kosong */
} key_info_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* unified protocol handle (already initialized) */
    display_t  *disp;    /* display untuk warning screen dan prompt */
} key_prog_config_t;

/* Default config (designated initializers). */
#define KEY_PROG_CONFIG_DEFAULT(p, d)  { .proto = (p), .disp = (d) }

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct key_prog key_prog_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Inisialisasi layanan pemrograman kunci. cfg->proto dan cfg->disp harus
 * non-NULL. */
key_prog_status_t key_prog_init(const key_prog_config_t *cfg,
                                key_prog_t **out);

/* Lepas handle pemrograman kunci. */
key_prog_status_t key_prog_deinit(key_prog_t *kp);

/* True jika key_prog_init() berhasil. */
bool key_prog_is_initialized(const key_prog_t *kp);

/* =========================================================================
 * Key programming operations
 * ========================================================================= */

/* Baca daftar kunci yang sudah terdaftar di ECU.
 * Hanya baca (read-only), tidak memerlukan warning screen.
 * keys: buffer yang dialokasi pemanggil; *count diisi dengan jumlah kunci.
 */
key_prog_status_t key_prog_read_keys(key_prog_t *kp,
                                      key_info_t *keys, size_t *count,
                                      uint32_t timeout_ms);

/* Tambah / daftarkan kunci baru ke ECU.
 * type: tipe kunci (transponder/remote/smartkey).
 * key_data: data kunci (transponder ID atau remote ID).
 * key_data_len: panjang key_data.
 *
 * warning_show(WARN_CUSTOM) ditampilkan dan SELECT diperlukan;
 * jika ditolak, KEY_PROG_ERR_USER_ABORT dikembalikan. */
key_prog_status_t key_prog_add_key(key_prog_t *kp,
                                    key_prog_key_type_t type,
                                    const uint8_t *key_data, size_t key_data_len,
                                    uint32_t timeout_ms);

/* Hapus kunci dari ECU berdasarkan key_index.
 * warning_show(WARN_CUSTOM) ditampilkan dan SELECT diperlukan. */
key_prog_status_t key_prog_delete_key(key_prog_t *kp,
                                       uint8_t key_index,
                                       uint32_t timeout_ms);

/* Duplikasi kunci dari slot sumber ke slot kosong.
 * source_key_index: indeks kunci yang akan diduplikasi.
 * warning_show(WARN_CUSTOM) ditampilkan dan SELECT diperlukan. */
key_prog_status_t key_prog_duplicate_key(key_prog_t *kp,
                                          uint8_t source_key_index,
                                          uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_KEY_PROG_H */
