/*
 * drivers/sd.c — SD card driver implementation for ESP32 DevKit V4.
 *
 * Backend: Arduino <SD.h> / <SPI.h> (SdFat, FAT32).
 * Toolchain note: C++ (Arduino). Compile as C++ (.cpp or -x c++).
 *
 * microSD pada SPI terpisah (HSPI): SCK 14, MOSI 12, MISO 25, CS 13.
 * Tidak condivisi bus dengan drivers/spi.c (CAN pakai VSPI).
 */

#include "sd.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */
#define SD_MAX_OPEN_FILES 8

typedef struct {
    File file;
    bool in_use;
} sd_file_handle_t;

static sd_file_handle_t g_files[SD_MAX_OPEN_FILES];
static SPIClass  g_sd_spi = HSPI;
static sd_config_t g_cfg;
static bool      g_initialized = false;

static sd_file_handle_t *file_handle(sd_file_t *f)
{
    return (sd_file_handle_t *)f;
}

static int find_free_handle(void)
{
    for (int i = 0; i < SD_MAX_OPEN_FILES; i++) {
        if (!g_files[i].in_use) {
            return i;
        }
    }
    return -1;
}

static uint8_t mode_to_arduino(const char *mode)
{
    if (strstr(mode, "w")) {
        return O_WRITE | O_CREAT | O_TRUNC;
    }
    if (strstr(mode, "a")) {
        return O_WRITE | O_CREAT | O_APPEND;
    }
    if (strstr(mode, "r+") || strstr(mode, "+r")) {
        return O_READ | O_WRITE;
    }
    return O_READ;   /* default: "r" */
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
sd_status_t sd_init(const sd_config_t *cfg)
{
    if (cfg == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    /* Close anything leftover. */
    sd_deinit();

    g_cfg = *cfg;

    /* Inisialisasi bus SPI untuk SD (HSPI terpisah). */
    g_sd_spi.begin(cfg->sck_gpio, cfg->miso_gpio, cfg->mosi_gpio, cfg->cs_gpio);

    /* Inisialisasi kartu SD + FATFS. */
    if (!SD.begin(cfg->cs_gpio, g_sd_spi, cfg->clock_hz)) {
        return SD_ERR_CARD_NOT_FOUND;
    }

    g_initialized = true;
    return SD_OK;
}

sd_status_t sd_deinit(void)
{
    for (int i = 0; i < SD_MAX_OPEN_FILES; i++) {
        if (g_files[i].in_use && g_files[i].file) {
            g_files[i].file.close();
            g_files[i].in_use = false;
        }
    }
    if (g_initialized) {
        SD.end();
        g_initialized = false;
    }
    return SD_OK;
}

bool sd_is_initialized(void)
{
    return g_initialized;
}

/* ------------------------------------------------------------------------
 * Card info
 * ------------------------------------------------------------------------ */
sd_status_t sd_card_info(uint32_t *card_size_mb, uint32_t *free_mb)
{
    if (!g_initialized) {
        return SD_ERR_NOT_INITIALIZED;
    }

    uint64_t total = SD.totalBytes();   /* bytes */
    uint64_t used  = SD.usedBytes();
    uint64_t free  = total - used;

    if (card_size_mb) {
        *card_size_mb = (uint32_t)(total / (1024UL * 1024UL));
    }
    if (free_mb) {
        *free_mb = (uint32_t)(free / (1024UL * 1024UL));
    }
    return SD_OK;
}

/* ------------------------------------------------------------------------
 * File operations
 * ------------------------------------------------------------------------ */
sd_status_t sd_open(const char *path, const char *mode, sd_file_t **out_file)
{
    if (!g_initialized || path == NULL || mode == NULL || out_file == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    int idx = find_free_handle();
    if (idx < 0) {
        return SD_ERR_INVALID_ARG;   /* terlalu banyak file terbuka */
    }

    uint8_t arduino_mode = mode_to_arduino(mode);
    File f = SD.open(path, arduino_mode);
    if (!f) {
        return SD_ERR_OPEN_FAILED;
    }

    g_files[idx].file = f;
    g_files[idx].in_use = true;
    *out_file = (sd_file_t *)&g_files[idx];
    return SD_OK;
}

sd_status_t sd_close(sd_file_t *file)
{
    if (!g_initialized || file == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    sd_file_handle_t *h = file_handle(file);
    if (!h->in_use || !h->file) {
        return SD_ERR_INVALID_ARG;
    }

    h->file.close();
    h->in_use = false;
    return SD_OK;
}

sd_status_t sd_read(sd_file_t *file, uint8_t *buf, size_t len, size_t *out_len)
{
    if (!g_initialized || file == NULL || buf == NULL || out_len == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    sd_file_handle_t *h = file_handle(file);
    if (!h->in_use || !h->file) {
        return SD_ERR_INVALID_ARG;
    }

    int n = h->file.read(buf, len);
    *out_len = (n < 0) ? 0 : (size_t)n;
    return SD_OK;
}

sd_status_t sd_write(sd_file_t *file, const uint8_t *buf, size_t len)
{
    if (!g_initialized || file == NULL || buf == NULL || len == 0) {
        return SD_ERR_INVALID_ARG;
    }

    sd_file_handle_t *h = file_handle(file);
    if (!h->in_use || !h->file) {
        return SD_ERR_INVALID_ARG;
    }

    size_t written = h->file.write(buf, len);
    if (written != len) {
        return SD_ERR_WRITE_FAILED;
    }
    return SD_OK;
}

sd_status_t sd_flush(sd_file_t *file)
{
    if (!g_initialized || file == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    sd_file_handle_t *h = file_handle(file);
    if (!h->in_use || !h->file) {
        return SD_ERR_INVALID_ARG;
    }

    h->file.flush();
    return SD_OK;
}

sd_status_t sd_seek(sd_file_t *file, uint32_t offset)
{
    if (!g_initialized || file == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    sd_file_handle_t *h = file_handle(file);
    if (!h->in_use || !h->file) {
        return SD_ERR_INVALID_ARG;
    }

    if (!h->file.seek(offset)) {
        return SD_ERR_INVALID_ARG;
    }
    return SD_OK;
}

sd_status_t sd_tell(sd_file_t *file, uint32_t *offset)
{
    if (!g_initialized || file == NULL || offset == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    sd_file_handle_t *h = file_handle(file);
    if (!h->in_use || !h->file) {
        return SD_ERR_INVALID_ARG;
    }

    *offset = (uint32_t)h->file.position();
    return SD_OK;
}

sd_status_t sd_size(sd_file_t *file, uint32_t *size)
{
    if (!g_initialized || file == NULL || size == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    sd_file_handle_t *h = file_handle(file);
    if (!h->in_use || !h->file) {
        return SD_ERR_INVALID_ARG;
    }

    *size = (uint32_t)h->file.size();
    return SD_OK;
}

/* ------------------------------------------------------------------------
 * Directory / file management
 * ------------------------------------------------------------------------ */
sd_status_t sd_exists(const char *path, bool *exists)
{
    if (!g_initialized || path == NULL || exists == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    *exists = SD.exists(path);
    return SD_OK;
}

sd_status_t sd_remove(const char *path)
{
    if (!g_initialized || path == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    if (!SD.remove(path)) {
        return SD_ERR_FILE_NOT_FOUND;
    }
    return SD_OK;
}

sd_status_t sd_mkdir(const char *path)
{
    if (!g_initialized || path == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    if (!SD.mkdir(path)) {
        return SD_ERR_DIR_FAILED;
    }
    return SD_OK;
}

sd_status_t sd_readdir(const char *dir_path, char ***out_names, size_t *out_count)
{
    if (!g_initialized || dir_path == NULL || out_names == NULL || out_count == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    File dir = SD.open(dir_path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return SD_ERR_DIR_FAILED;
    }

    /* Hitung entri terlebih dahulu. */
    size_t count = 0;
    File f = dir.openNextFile();
    while (f) {
        count++;
        f.close();
        f = dir.openNextFile();
    }

    char **names = (char **)calloc(count, sizeof(char *));
    if (names == NULL) {
        dir.close();
        return SD_ERR_INVALID_ARG;
    }

    /* Ulang baca dan simpan nama. */
    dir.rewindDirectory();
    size_t i = 0;
    f = dir.openNextFile();
    while (f && i < count) {
        const char *nm = f.name();
        size_t     n   = strlen(nm);
        names[i] = (char *)malloc(n + 1);
        if (names[i] != NULL) {
            memcpy(names[i], nm, n + 1);
            i++;
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();

    *out_names = names;
    *out_count = count;
    return SD_OK;
}

void sd_readdir_free(char **names, size_t count)
{
    if (names == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

/* ------------------------------------------------------------------------
 * ECU backup helpers
 * ------------------------------------------------------------------------ */
sd_status_t sd_ecu_dirs_init(void)
{
    if (!g_initialized) {
        return SD_ERR_NOT_INITIALIZED;
    }

    bool exists = false;
    sd_exists(SD_ECU_DIR, &exists);
    if (!exists) {
        sd_mkdir(SD_ECU_DIR);
    }

    sd_exists(SD_BACKUP_DIR, &exists);
    if (!exists) {
        sd_mkdir(SD_BACKUP_DIR);
    }

    sd_exists(SD_ORIGINAL_DIR, &exists);
    if (!exists) {
        sd_mkdir(SD_ORIGINAL_DIR);
    }

    return SD_OK;
}

void sd_ecu_original_path(const char *ecu_id, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s/%s.bin", SD_ORIGINAL_DIR, ecu_id);
}

void sd_ecu_backup_path(const char *ecu_id, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%s/%s.bin", SD_BACKUP_DIR, ecu_id);
}

sd_status_t sd_save_ecu_backup(const char *ecu_id, const uint8_t *data,
                               size_t len, char *out_path, size_t out_path_len)
{
    if (!g_initialized || ecu_id == NULL || data == NULL || len == 0) {
        return SD_ERR_INVALID_ARG;
    }

    /* Pastikan direktori ada. */
    sd_ecu_dirs_init();

    char path[128];
    sd_ecu_backup_path(ecu_id, path, sizeof(path));

    if (out_path != NULL && out_path_len > 0) {
        snprintf(out_path, out_path_len, "%s", path);
    }

    sd_file_t *file = NULL;
    sd_status_t st = sd_open(path, "w", &file);
    if (st != SD_OK) {
        return st;
    }

    st = sd_write(file, data, len);
    sd_flush(file);
    sd_close(file);
    return st;
}

sd_status_t sd_load_ecu_backup(const char *path, uint8_t *buf, size_t buf_len,
                               size_t *out_len)
{
    if (!g_initialized || path == NULL || buf == NULL || out_len == NULL) {
        return SD_ERR_INVALID_ARG;
    }

    bool exists = false;
    sd_exists(path, &exists);
    if (!exists) {
        return SD_ERR_FILE_NOT_FOUND;
    }

    sd_file_t *file = NULL;
    sd_status_t st = sd_open(path, "r", &file);
    if (st != SD_OK) {
        return st;
    }

    uint32_t fsize = 0;
    sd_size(file, &fsize);
    if (fsize > buf_len) {
        sd_close(file);
        return SD_ERR_NO_FREE_SPACE;
    }

    size_t got = 0;
    st = sd_read(file, buf, fsize, &got);
    sd_close(file);

    *out_len = got;
    return st;
}

sd_status_t sd_list_ecu_backups(char ***out_names, size_t *out_count)
{
    if (!g_initialized || out_names == NULL || out_count == NULL) {
        return SD_ERR_INVALID_ARG;
    }
    return sd_readdir(SD_BACKUP_DIR, out_names, out_count);
}