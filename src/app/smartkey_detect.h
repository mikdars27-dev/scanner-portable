/*
 * app/smartkey_detect.h — Smart Key Detector (Feature 29: Detektor Smart Key).
 *
 * Mendeteksi keberadaan smart key (keyless entry/passive entry) di sekitar
 * kendaraan, membaca status baterai kunci, dan kekuatan sinyal (RSSI) melalui
 * sistem immobilizer kendaraan menggunakan UDS $22 ReadDataByIdentifier atau
 * SID produsen spesifik via proto_send().
 *
 * Safety: deteksi hanya baca (read-only), tidak memerlukan warning screen.
 * Operasi yang memicu respons immobilizer (mis. challenge-response) tetap
 * dibatarkan dengan timeout paksa.
 *
 * Layering: APP -> smartkey_detect_* -> protocol_* -> {j1979_*, kline_proto_*} -> drivers
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_SMARTKEY_DETECT_H
#define APP_SMARTKEY_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/protocol.h"     /* protocol_t, proto_send, proto_is_initialized */
#include "protocol/kline_proto.h"  /* kline_proto_uds_read_did */
#include "ui/warning_screen.h"     /* warning_show, WARN_CUSTOM, display_t */
#include "ui/display.h"            /* display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    SMARTKEY_DETECT_OK                    = 0,
    SMARTKEY_DETECT_ERR_INVALID_ARG       = -1,
    SMARTKEY_DETECT_ERR_NOT_INITIALIZED   = -2,
    SMARTKEY_DETECT_ERR_PROTOCOL          = -3,   /* underlying protocol failure */
    SMARTKEY_DETECT_ERR_TIMEOUT           = -4,   /* detection timeout */
    SMARTKEY_DETECT_ERR_KEY_NOT_FOUND     = -5,   /* smart key not detected */
    SMARTKEY_DETECT_ERR_SAFETY            = -6,   /* safety condition not met */
    SMARTKEY_DETECT_ERR_NOT_SUPPORTED     = -7,   /* transport not supported */
} smartkey_detect_status_t;

/* =========================================================================
 * Smart key info
 * ========================================================================= */

#define SMARTKEY_ID_MAX_LEN 16

typedef struct {
    uint8_t  detected;           /* 1 = terdeteksi, 0 = tidak terdeteksi */
    int8_t   rssi_dbm;           /* kekuatan sinyal (dBm), -128 jika tidak diketahui */
    uint8_t  battery_level;      /* 0..100 persen, 255 jika tidak diketahui */
    uint8_t  key_id[SMARTKEY_ID_MAX_LEN]; /* ID unik smart key */
    size_t   key_id_len;         /* panjang bytes key_id yang valid */
    uint32_t last_seen_ms;       /* timestamp terakhir terdeteksi (millis) */
} smartkey_info_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* unified protocol handle (already initialized) */
    display_t  *disp;    /* display untuk prompt dan status */
} smartkey_detect_config_t;

/* Default config (designated initializers). */
#define SMARTKEY_DETECT_CONFIG_DEFAULT(p, d)  { .proto = (p), .disp = (d) }

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct smartkey_detect smartkey_detect_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Inisialisasi layanan deteksi smart key. cfg->proto dan cfg->disp harus
 * non-NULL. */
smartkey_detect_status_t smartkey_detect_init(const smartkey_detect_config_t *cfg,
                                               smartkey_detect_t **out);

/* Lepas handle deteksi smart key. */
smartkey_detect_status_t smartkey_detect_deinit(smartkey_detect_t *sk);

/* True jika smartkey_detect_init() berhasil. */
bool smartkey_detect_is_initialized(const smartkey_detect_t *sk);

/* =========================================================================
 * Smart key detection
 * ========================================================================= */

/* Scan keberadaan smart key di sekitar kendaraan.
 * info: buffer yang dialokasi pemanggil, diisi dengan hasil deteksi.
 * timeout_ms: waktu maksimum menunggu respons dari sistem immobilizer.
 *
 * Read-only: tidak memerlukan warning screen. Jika kunci tidak terdeteksi,
 * mengembalikan SMARTKEY_DETECT_ERR_KEY_NOT_FOUND dengan info->detected = 0. */
smartkey_detect_status_t smartkey_detect_scan(smartkey_detect_t *sk,
                                               smartkey_info_t *info,
                                               uint32_t timeout_ms);

/* Baca status baterai smart key.
 * level: diisi dengan 0..100 persen, atau 255 jika tidak diketahui.
 * Memerlukan kunci terdeteksi dalam scan sebelumnya. */
smartkey_detect_status_t smartkey_detect_battery(smartkey_detect_t *sk,
                                                  uint8_t *level,
                                                  uint32_t timeout_ms);

/* Baca kekuatan sinyal smart key (RSSI).
 * rssi: diisi dengan nilai dBm (negatif), atau -128 jika tidak diketahui. */
smartkey_detect_status_t smartkey_detect_signal_strength(smartkey_detect_t *sk,
                                                          int8_t *rssi,
                                                          uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_SMARTKEY_DETECT_H */
