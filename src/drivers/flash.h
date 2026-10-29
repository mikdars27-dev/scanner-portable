#ifndef DRIVERS_FLASH_H
#define DRIVERS_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Safe flash abstraction (dual-slot / A-B partition aware).
 *
 * Digunakan oleh firmware_update.c dan ecu_file.c untuk memastikan bahwa
 * flash hanya dilakukan setelah verifikasi integritas, dan bahwa terdapat
 * mekanisme rollback (dual-slot) jika flash gagal atau image baru bermasalah.
 *
 * Implementasi saat ini adalah interface/stub. Body fungsi perlu
 * diimplementasikan sesuai partisi flash ESP32 (OTA0 / OTA1).
 * ========================================================================= */

typedef enum {
    FLASH_OK             = 0,
    FLASH_ERR_INVALID_ARG= -1,
    FLASH_ERR_NOT_READY  = -2,
    FLASH_ERR_WRITE_FAIL = -3,
    FLASH_ERR_VERIFY_FAIL= -4,
    FLASH_ERR_NO_SLOT    = -5,
} flash_status_t;

/* Slot ID untuk dual-slot (A/B) flash. */
typedef enum {
    FLASH_SLOT_CURRENT = 0,
    FLASH_SLOT_NEXT    = 1,
} flash_slot_t;

/* Menulis `len` bytes dari `data` ke alamat `addr` pada slot `slot`.
 * Harus dipanggil setelah verifikasi integritas (CRC/HMAC). */
flash_status_t flash_write(flash_slot_t slot, uint32_t addr,
                           const uint8_t *data, size_t len);

/* Memverifikasi bahwa image di slot `slot` cocok dengan `expected_crc32`.
 * Returns 1 jika cocok, 0 jika tidak, negative jika error. */
int flash_verify_crc32(flash_slot_t slot, uint32_t expected_crc32);

/* Menandai slot `slot` sebagai slot aktif (bootable). Setelah dipanggil,
 * perangkat akan boot dari slot ini pada restart berikutnya. */
flash_status_t flash_set_active_slot(flash_slot_t slot);

/* Mengembalikan slot yang saat ini aktif. */
flash_slot_t flash_get_active_slot(void);

/* Lakukan rollback ke slot sebelumnya (digunakan jika flash baru gagal
 * verifikasi saat boot). */
flash_status_t flash_rollback(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_FLASH_H */
