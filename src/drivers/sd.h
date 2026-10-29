/*
 * drivers/sd.h — SD card driver (microSD via separate SPI) for ESP32 DevKit V4.
 *
 * Digunakan untuk: penyimpanan file .bin original ECU, backup ECU, dan staging
 * citra flash. Filesystem FAT32 melalui Arduino SD library (backend SdFat).
 *
 * Pin (lihat config/pinout.h):
 *   SCK 14, MOSI 12, MISO 25, CS 13 — HSPI terpisah dari CAN (VSPI).
 *
 * Layering: APP -> ECU flash/backup -> sd_* (SD + FATFS) -> Arduino SD/SPI
 */

#ifndef DRIVERS_SD_H
#define DRIVERS_SD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    SD_OK                    = 0,
    SD_ERR_INVALID_ARG       = -1,
    SD_ERR_NOT_INITIALIZED   = -2,
    SD_ERR_CARD_NOT_FOUND    = -3,
    SD_ERR_FILE_NOT_FOUND    = -4,
    SD_ERR_FILE_EXISTS       = -5,
    SD_ERR_OPEN_FAILED       = -6,
    SD_ERR_READ_FAILED       = -7,
    SD_ERR_WRITE_FAILED      = -8,
    SD_ERR_CLOSE_FAILED      = -9,
    SD_ERR_NO_FREE_SPACE     = -10,
    SD_ERR_FILESYSTEM_FAILED = -11,
    SD_ERR_DIR_FAILED        = -12,
} sd_status_t;

typedef struct sd_file sd_file_t;   /* opaque handle */

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    int     cs_gpio;       /* microSD chip select (GPIO 13) */
    int     sck_gpio;      /* SPI clock (GPIO 14) */
    int     mosi_gpio;     /* SPI MOSI (GPIO 12) */
    int     miso_gpio;     /* SPI MISO (GPIO 25) */
    uint32_t clock_hz;     /* SPI clock (default 20 MHz, SD spec <= 25 MHz) */
} sd_config_t;

#define SD_CONFIG_DEFAULT                                             \
    {                                                                 \
        .cs_gpio    = SD_CS_GPIO,                                     \
        .sck_gpio   = SD_SCK_GPIO,                                    \
        .mosi_gpio  = SD_MOSI_GPIO,                                   \
        .miso_gpio  = SD_MISO_GPIO,                                   \
        .clock_hz   = 20000000UL,                                     \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Inisialisasi kartu SD dan filesystem FAT32. Harus dipanggil sebelum
 * operasi apapun. */
sd_status_t sd_init(const sd_config_t *cfg);

/* Nonaktifkan kartu SD, tutup semua file yang terbuka. */
sd_status_t sd_deinit(void);

bool sd_is_initialized(void);

/* =========================================================================
 * Card info
 * ========================================================================= */

/* Ambil ukuran kartu (MB) dan ruang bebas (MB). */
sd_status_t sd_card_info(uint32_t *card_size_mb, uint32_t *free_mb);

/* =========================================================================
 * File operations
 * ========================================================================= */

/* Buka file. mode: "r", "w", "a", "r+" (seperti fopen). */
sd_status_t sd_open(const char *path, const char *mode, sd_file_t **out_file);

/* Tutup file. */
sd_status_t sd_close(sd_file_t *file);

/* Baca dari file. *out_len diisi dengan byte yang benar-benar dibaca.
 * Kembali SD_OK meski EOF (out_len = 0). */
sd_status_t sd_read(sd_file_t *file, uint8_t *buf, size_t len, size_t *out_len);

/* Tulis ke file. */
sd_status_t sd_write(sd_file_t *file, const uint8_t *buf, size_t len);

/* Flush buffer ke kartu. */
sd_status_t sd_flush(sd_file_t *file);

/* Pindah posisi file (offset dari awal). */
sd_status_t sd_seek(sd_file_t *file, uint32_t offset);

/* Ambil posisi file saat ini. */
sd_status_t sd_tell(sd_file_t *file, uint32_t *offset);

/* Ambil ukuran file. */
sd_status_t sd_size(sd_file_t *file, uint32_t *size);

/* =========================================================================
 * Directory / file management
 * ========================================================================= */

/* Periksa apakah path ada. */
sd_status_t sd_exists(const char *path, bool *exists);

/* Hapus file. */
sd_status_t sd_remove(const char *path);

/* Buat direktori (rekursif tidak didukung — satu level saja). */
sd_status_t sd_mkdir(const char *path);

/* Baca isi direktori. out_names dialokasi dengan malloc, pemanggil harus
 * membebaskan dengan sd_readdir_free(). */
sd_status_t sd_readdir(const char *dir_path, char ***out_names, size_t *out_count);

/* Bebaskan hasil sd_readdir. */
void sd_readdir_free(char **names, size_t count);

/* =========================================================================
 * ECU backup helpers
 * ========================================================================= */
#define SD_ECU_DIR      "/ecu"
#define SD_BACKUP_DIR   "/ecu/backup"
#define SD_ORIGINAL_DIR "/ecu/original"

/* Buat direktori /ecu, /ecu/backup, /ecu/original jika belum ada. */
sd_status_t sd_ecu_dirs_init(void);

/* Simpan cadangan ECU ke /ecu/backup/<ecu_id>.bin. */
sd_status_t sd_save_ecu_backup(const char *ecu_id, const uint8_t *data,
                               size_t len, char *out_path, size_t out_path_len);

/* Muat cadangan ECU dari path. buf dialokasi pemanggil. */
sd_status_t sd_load_ecu_backup(const char *path, uint8_t *buf, size_t buf_len,
                               size_t *out_len);

/* Daftar file backup di /ecu/backup. out_names dialokasi malloc. */
sd_status_t sd_list_ecu_backups(char ***out_names, size_t *out_count);

/* Path helper untuk file original ECU. */
void sd_ecu_original_path(const char *ecu_id, char *buf, size_t buf_len);

/* Path helper untuk file backup ECU. */
void sd_ecu_backup_path(const char *ecu_id, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_SD_H */