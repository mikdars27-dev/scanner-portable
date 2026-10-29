/*
 * app/firmware_update.c — Firmware / database online update implementation.
 *
 * Supports firmware updates via WiFi (OTA over HTTP) or USB/SD card, plus
 * motorcycle DTC database updates via SD card. Built for ESP32 DevKit V4
 * using Arduino-ESP32 Update / WiFi / HTTPClient libraries and the project
 * SD driver.
 *
 * Layering: APP -> firmware_update_* -> ESP32 Arduino libs + drivers/sd
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "firmware_update.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

#include "drivers/sd.h"     /* sd_init, sd_open, sd_read, sd_size, sd_close */
#include "protocol/dtc.h"   /* dtc_register, dtc_entry_t */
#include "ui/warning_screen.h"   /* warning_show, WARN_CUSTOM */
#include "utils/crc.h"      /* crc32() for pre-flash integrity verification */

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

#define FW_UPDATE_WIFI_CONNECT_TIMEOUT_MS 15000UL
#define FW_UPDATE_CHUNK_SIZE 1024U
#define FW_UPDATE_LINE_MAX 128U

/* ------------------------------------------------------------------------
 * Opaque handle
 * ------------------------------------------------------------------------ */

struct firmware_update {
    firmware_update_config_t cfg;
    bool                      initialized;
};

/* ------------------------------------------------------------------------
 * Flash helper: write a stream of bytes to the OTA partition
 * ------------------------------------------------------------------------ */

/* Read up to `size` bytes from a WiFiClient stream and flash them via
 * Update. Returns FW_UPDATE_OK on success. */
static firmware_update_status_t fw_update_flash_wifi_stream(
        WiFiClient *stream, size_t size, uint32_t expected_crc32)
{
    if (!Update.begin(size)) {
        return FW_UPDATE_ERR_INVALID_IMAGE;
    }

    uint8_t buf[FW_UPDATE_CHUNK_SIZE];
    size_t written = 0;
    uint32_t crc = 0UL;
    while (written < size) {
        size_t to_read = size - written;
        if (to_read > FW_UPDATE_CHUNK_SIZE) {
            to_read = FW_UPDATE_CHUNK_SIZE;
        }
        int read = stream->readBytes(buf, to_read);
        if (read <= 0) {
            break;
        }
        if (Update.write(buf, (size_t)read) != (size_t)read) {
            return FW_UPDATE_ERR_PROTOCOL;
        }
        crc = crc32_compute(buf, (size_t)read, crc);
        written += (size_t)read;
        yield();
    }

    if (expected_crc32 != 0UL && crc != expected_crc32) {
        return FW_UPDATE_ERR_INVALID_IMAGE;
    }

    if (!Update.end()) {
        return FW_UPDATE_ERR_PROTOCOL;
    }
    if (!Update.isFinished()) {
        return FW_UPDATE_ERR_PROTOCOL;
    }
    return FW_UPDATE_OK;
}

/* Read up to `size` bytes from an SD file and flash them via Update. */
static firmware_update_status_t fw_update_flash_sd_file(sd_file_t *file,
                                                         uint32_t size,
                                                         uint32_t expected_crc32)
{
    if (!Update.begin(size)) {
        return FW_UPDATE_ERR_INVALID_IMAGE;
    }

    uint8_t buf[FW_UPDATE_CHUNK_SIZE];
    uint32_t written = 0;
    uint32_t crc = 0UL;
    while (written < size) {
        uint32_t to_read = size - written;
        if (to_read > FW_UPDATE_CHUNK_SIZE) {
            to_read = FW_UPDATE_CHUNK_SIZE;
        }
        size_t out_len = 0;
        sd_status_t st = sd_read(file, buf, to_read, &out_len);
        if (st != SD_OK || out_len == 0) {
            break;
        }
        if (Update.write(buf, out_len) != out_len) {
            return FW_UPDATE_ERR_PROTOCOL;
        }
        crc = crc32_compute(buf, out_len, crc);
        written += out_len;
        yield();
    }

    if (expected_crc32 != 0UL && crc != expected_crc32) {
        return FW_UPDATE_ERR_INVALID_IMAGE;
    }

    if (!Update.end()) {
        return FW_UPDATE_ERR_PROTOCOL;
    }
    if (!Update.isFinished()) {
        return FW_UPDATE_ERR_PROTOCOL;
    }
    return FW_UPDATE_OK;
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

firmware_update_status_t fw_update_init(const firmware_update_config_t *cfg,
                                         firmware_update_t **out)
{
    if (cfg == NULL || out == NULL || cfg->disp == NULL) {
        return FW_UPDATE_ERR_INVALID_ARG;
    }

    firmware_update_t *fu = (firmware_update_t *)calloc(1, sizeof(*fu));
    if (fu == NULL) {
        return FW_UPDATE_ERR_INVALID_ARG;
    }

    fu->cfg         = *cfg;
    fu->initialized = true;

    *out = fu;
    return FW_UPDATE_OK;
}

firmware_update_status_t fw_update_deinit(firmware_update_t *fu)
{
    if (fu == NULL) {
        return FW_UPDATE_ERR_INVALID_ARG;
    }
    fu->initialized = false;
    free(fu);
    return FW_UPDATE_OK;
}

bool fw_update_is_initialized(const firmware_update_t *fu)
{
    return fu != NULL && fu->initialized;
}

/* ------------------------------------------------------------------------
 * WiFi OTA firmware update
 * ------------------------------------------------------------------------ */

firmware_update_status_t fw_update_wifi(firmware_update_t *fu,
                                         const char *url, uint32_t expected_crc32,
                                         uint32_t timeout_ms)
{
    (void)timeout_ms; /* WiFi/HTTP use their own internal timeouts */

    if (fu == NULL) return FW_UPDATE_ERR_INVALID_ARG;
    if (!fu->initialized) return FW_UPDATE_ERR_NOT_INITIALIZED;
    if (url == NULL) return FW_UPDATE_ERR_INVALID_ARG;
    if (!fu->cfg.wifi_ssid || !fu->cfg.wifi_password) {
        return FW_UPDATE_ERR_INVALID_ARG;
    }

    /* Connect to WiFi. */
    WiFi.begin(fu->cfg.wifi_ssid, fu->cfg.wifi_password);
    uint32_t wifi_start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if ((uint32_t)(millis() - wifi_start) >
            FW_UPDATE_WIFI_CONNECT_TIMEOUT_MS)
        {
            WiFi.disconnect();
            return FW_UPDATE_ERR_NO_NETWORK;
        }
        delay(500);
        yield();
    }

    /* Download firmware image over HTTP. */
    HTTPClient http;
    http.begin(url);
    int http_code = http.GET();
    if (http_code != HTTP_CODE_OK) {
        http.end();
        WiFi.disconnect();
        return FW_UPDATE_ERR_PROTOCOL;
    }

    size_t content_len = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    if (stream == NULL || content_len == 0) {
        http.end();
        WiFi.disconnect();
        return FW_UPDATE_ERR_INVALID_IMAGE;
    }

    firmware_update_status_t st =
            fw_update_flash_wifi_stream(stream, content_len, expected_crc32);
    http.end();
    WiFi.disconnect();

    if (st == FW_UPDATE_OK) {
        ESP.restart();
    }
    return st;
}

/* ------------------------------------------------------------------------
 * USB / SD card firmware update
 * ------------------------------------------------------------------------ */

firmware_update_status_t fw_update_usb(firmware_update_t *fu,
                                        const char *sd_path, uint32_t expected_crc32,
                                        uint32_t timeout_ms)
{
    (void)timeout_ms; /* SD operations are blocking */

    if (fu == NULL) return FW_UPDATE_ERR_INVALID_ARG;
    if (!fu->initialized) return FW_UPDATE_ERR_NOT_INITIALIZED;
    if (sd_path == NULL) return FW_UPDATE_ERR_INVALID_ARG;

    sd_status_t st = sd_init(NULL); /* uses SD_CONFIG_DEFAULT */
    if (st != SD_OK) {
        return FW_UPDATE_ERR_NO_SD;
    }

    sd_file_t *file = NULL;
    st = sd_open(sd_path, "r", &file);
    if (st != SD_OK || file == NULL) {
        sd_deinit();
        return FW_UPDATE_ERR_NO_SD;
    }

    uint32_t file_size = 0;
    st = sd_size(file, &file_size);
    if (st != SD_OK || file_size == 0) {
        sd_close(file);
        sd_deinit();
        return FW_UPDATE_ERR_INVALID_IMAGE;
    }

    firmware_update_status_t result = fw_update_flash_sd_file(file, file_size, expected_crc32);

    sd_close(file);
    sd_deinit();

    if (result == FW_UPDATE_OK) {
        ESP.restart();
    }
    return result;
}

/* ------------------------------------------------------------------------
 * Motorcycle DTC database update
 * ------------------------------------------------------------------------ */

/* Read one line from an SD file (up to newline or EOF). Returns true if a
 * line was read, false at EOF. */
static bool sd_read_line(sd_file_t *file, char *buf, size_t buf_size)
{
    size_t i = 0;
    while (i < buf_size - 1) {
        uint8_t b;
        size_t out_len = 0;
        sd_status_t st = sd_read(file, &b, 1, &out_len);
        if (st != SD_OK || out_len == 0) {
            break;
        }
        if (b == '\n') {
            break;
        }
        if (b == '\r') {
            continue;
        }
        buf[i++] = (char)b;
    }
    buf[i] = '\0';
    return i > 0;
}

/* Parse a "CODE,DESC" CSV line into a dtc_entry_t. Returns true on success. */
static bool parse_dtc_line(const char *line, dtc_entry_t *entry)
{
    const char *comma = strchr(line, ',');
    if (comma == NULL) {
        return false;
    }

    size_t code_len = (size_t)(comma - line);
    if (code_len >= sizeof(entry->code)) {
        code_len = sizeof(entry->code) - 1;
    }
    memcpy(entry->code, line, code_len);
    entry->code[code_len] = '\0';

    const char *desc = comma + 1;
    size_t desc_len = strlen(desc);
    if (desc_len >= sizeof(entry->desc)) {
        desc_len = sizeof(entry->desc) - 1;
    }
    memcpy(entry->desc, desc, desc_len);
    entry->desc[desc_len] = '\0';

    return true;
}

firmware_update_status_t fw_update_motodb(firmware_update_t *fu,
                                           const char *sd_path,
                                           uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (fu == NULL) return FW_UPDATE_ERR_INVALID_ARG;
    if (!fu->initialized) return FW_UPDATE_ERR_NOT_INITIALIZED;
    if (sd_path == NULL) return FW_UPDATE_ERR_INVALID_ARG;

    sd_status_t st = sd_init(NULL); /* uses SD_CONFIG_DEFAULT */
    if (st != SD_OK) {
        return FW_UPDATE_ERR_NO_SD;
    }

    sd_file_t *file = NULL;
    st = sd_open(sd_path, "r", &file);
    if (st != SD_OK || file == NULL) {
        sd_deinit();
        return FW_UPDATE_ERR_NO_SD;
    }

    char line[FW_UPDATE_LINE_MAX];
    int registered = 0;
    while (sd_read_line(file, line, sizeof(line))) {
        dtc_entry_t entry;
        if (parse_dtc_line(line, &entry)) {
            dtc_status_t dst = dtc_register(&entry);
            if (dst == DTC_OK) {
                registered++;
            }
        }
    }

    sd_close(file);
    sd_deinit();

    if (registered == 0) {
        return FW_UPDATE_ERR_INVALID_IMAGE;
    }
    return FW_UPDATE_OK;
}
