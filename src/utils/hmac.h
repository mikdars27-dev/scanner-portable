#ifndef UTILS_HMAC_H
#define UTILS_HMAC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * HMAC-SHA256 (RFC 4231).
 *
 * Digunakan untuk verifikasi tanda tangan (signature) sebelum ECU write /
 * flash, sebagaimana dijanjikan MASTER_PLAN.md Bab 9 dan
 * STRUKTUR_PROYEK_FIRMWARE.md Section 3.7.
 *
 * Implementasi saat ini adalah interface/stub: fungsi-fungsi di bawah
 * dideklarasikan tetapi body-nya perlu diimplementasikan (SHA-256 block
 * cipher + HMAC construction). Ganti dengan implementasi lengkap sebelum
 * produksi.
 * ========================================================================= */

/* HMAC-SHA256 over `data` (len bytes) with `key` (key_len bytes), writing
 * 32-byte result into `out` (must point to at least 32 bytes). Returns 0 on
 * success, nonzero on failure. */
int hmac_sha256(const uint8_t *key, size_t key_len,
                const uint8_t *data, size_t data_len,
                uint8_t *out);

/* Verify HMAC-SHA256 signature. Computes HMAC over `data` with `key` and
 * compares with `sig` (32 bytes). Returns 1 if match, 0 if mismatch, negative
 * on error. */
int hmac_sha256_verify(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       const uint8_t *sig);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_HMAC_H */
