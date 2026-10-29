/*
 * drivers/flash.c — Safe flash abstraction (dual-slot / A-B partition aware).
 *
 * Digunakan oleh firmware_update.c dan ecu_file.c untuk memastikan bahwa
 * flash hanya dilakukan setelah verifikasi integritas, dan bahwa terdapat
 * mekanisme rollback (dual-slot) jika flash gagal atau image baru bermasalah.
 *
 * Implementasi untuk ESP32 DevKit V4 (Arduino-ESP32). Menggunakan partisi
 * OTA bawaan ESP32 (ota_0 / ota_1) dan API esp_partition + Update.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "drivers/flash.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <Arduino.h>
#include <Update.h>
#include <esp_partition.h>

/* =========================================================================
 * Internal state.
 * ========================================================================= */

static flash_slot_t flash_active_slot = FLASH_SLOT_CURRENT;

/* Ambil pointer ke partisi OTA untuk slot yang diberikan. */
static const esp_partition_t *flash_partition_for_slot(flash_slot_t slot)
{
    if (slot == FLASH_SLOT_NEXT) {
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                        ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                        NULL);
    }
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                    NULL);
}

/* Ambil partisi yang saat ini ditandai sebagai bootable. */
static const esp_partition_t *flash_running_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                    NULL);
}

/* =========================================================================
 * Public API.
 * ========================================================================= */

flash_status_t flash_write(flash_slot_t slot, uint32_t addr,
                           const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return FLASH_ERR_INVALID_ARG;
    }

    const esp_partition_t *part = flash_partition_for_slot(slot);
    if (part == NULL) {
        return FLASH_ERR_NO_SLOT;
    }

    /* Catat slot yang sedang ditulis. */
    if (slot == FLASH_SLOT_CURRENT) {
        flash_active_slot = FLASH_SLOT_CURRENT;
    } else {
        flash_active_slot = FLASH_SLOT_NEXT;
    }

    /* Mulai write ke partisi OTA target. Ukuran harus cocok dengan
     * partisi agar Update.end() tidak gagal. */
    if (!Update.begin(len, U_FLASH, part->address)) {
        return FLASH_ERR_WRITE_FAIL;
    }

    size_t written = 0;
    while (written < len) {
        size_t to_write = len - written;
        if (to_write > 4096) {
            to_write = 4096;
        }
        if (Update.write(data + written, to_write) != to_write) {
            return FLASH_ERR_WRITE_FAIL;
        }
        written += to_write;
        yield();
    }

    if (!Update.end()) {
        return FLASH_ERR_WRITE_FAIL;
    }
    if (!Update.isFinished()) {
        return FLASH_ERR_WRITE_FAIL;
    }

    return FLASH_OK;
}

int flash_verify_crc32(flash_slot_t slot, uint32_t expected_crc32)
{
    const esp_partition_t *part = flash_partition_for_slot(slot);
    if (part == NULL) {
        return -1;
    }

    size_t part_size = part->size;
    if (part_size == 0) {
        return -1;
    }

    /* Baca seluruh partisi dan hitung CRC32 streaming. */
    uint8_t buf[1024];
    size_t  read_total = 0;
    uint32_t crc = 0UL;

    while (read_total < part_size) {
        size_t to_read = part_size - read_total;
        if (to_read > sizeof(buf)) {
            to_read = sizeof(buf);
        }
        esp_err_t err = esp_partition_read(part, read_total, buf, to_read);
        if (err != ESP_OK) {
            return -1;
        }
        crc = crc32_compute(buf, to_read, crc);
        read_total += to_read;
    }

    return (crc == expected_crc32) ? 1 : 0;
}

flash_status_t flash_set_active_slot(flash_slot_t slot)
{
    const esp_partition_t *part = flash_partition_for_slot(slot);
    if (part == NULL) {
        return FLASH_ERR_NO_SLOT;
    }

    esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        return FLASH_ERR_WRITE_FAIL;
    }

    flash_active_slot = slot;
    return FLASH_OK;
}

flash_slot_t flash_get_active_slot(void)
{
    return flash_active_slot;
}

flash_status_t flash_rollback(void)
{
    flash_slot_t target = (flash_active_slot == FLASH_SLOT_CURRENT)
                          ? FLASH_SLOT_NEXT : FLASH_SLOT_CURRENT;
    return flash_set_active_slot(target);
}
