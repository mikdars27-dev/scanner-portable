/*
 * app/firmware_update.h — Firmware / database online update (Feature 12: Pembaruan Online).
 *
 * Supports firmware updates via WiFi (OTA over HTTP) or USB/SD card, plus
 * motorcycle DTC database updates via SD card. Built for ESP32 DevKit V4
 * using the Arduino-ESP32 Update, WiFi, HTTPClient, and SD libraries.
 *
 * Layering: APP -> firmware_update_* -> ESP32 Arduino libraries + SD
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_FIRMWARE_UPDATE_H
#define APP_FIRMWARE_UPDATE_H

#include <stdint.h>
#include <stdbool.h>

#include "ui/display.h"     /* display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    FW_UPDATE_OK                    = 0,
    FW_UPDATE_ERR_INVALID_ARG       = -1,
    FW_UPDATE_ERR_NOT_INITIALIZED   = -2,
    FW_UPDATE_ERR_PROTOCOL          = -3,   /* underlying transport failure */
    FW_UPDATE_ERR_TIMEOUT           = -4,   /* operation timed out */
    FW_UPDATE_ERR_USER_ABORT        = -5,   /* user aborted via button */
    FW_UPDATE_ERR_NO_NETWORK        = -6,   /* WiFi not available */
    FW_UPDATE_ERR_NO_SD             = -7,   /* SD card not available */
    FW_UPDATE_ERR_INVALID_IMAGE     = -8,   /* downloaded image invalid */
    FW_UPDATE_ERR_NOT_SUPPORTED     = -9,
} firmware_update_status_t;

/* =========================================================================
 * Update target
 * ========================================================================= */

typedef enum {
    FW_UPDATE_TARGET_FIRMWARE = 0,   /* ESP32 firmware image */
    FW_UPDATE_TARGET_MOTODB,         /* motorcycle DTC database */
} fw_update_target_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct firmware_update firmware_update_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    display_t   *disp;           /* display for progress / status messages */
    const char  *wifi_ssid;      /* WiFi SSID for OTA updates */
    const char  *wifi_password;  /* WiFi password for OTA updates */
} firmware_update_config_t;

/* Default config (designated initializers). */
#define FW_UPDATE_CONFIG_DEFAULT(d, ssid, pwd)                              \
    {                                                                       \
        .disp = (d), .wifi_ssid = (ssid), .wifi_password = (pwd),           \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the firmware update service. Stores WiFi credentials for OTA
 * updates. cfg->disp must be non-NULL. */
firmware_update_status_t fw_update_init(const firmware_update_config_t *cfg,
                                         firmware_update_t **out);

/* Release the firmware update handle. */
firmware_update_status_t fw_update_deinit(firmware_update_t *fu);

/* True if fw_update_init() was called successfully. */
bool fw_update_is_initialized(const firmware_update_t *fu);

/* =========================================================================
 * Firmware / database updates
 * ========================================================================= */

/* Update ESP32 firmware over WiFi via HTTP OTA.
 *
 * url: HTTP URL of the firmware binary (.bin) to download and flash.
 * expected_crc32: expected CRC32 of the image (0 = skip verification).
 * Connects to WiFi using cfg->wifi_ssid / cfg->wifi_password, downloads the
 * image, verifies its CRC32, writes it to the OTA partition, and reboots on
 * success. If the CRC does not match, the update is aborted without flashing.
 *
 * Returns FW_UPDATE_OK on success (device will reboot), or an error status. */
firmware_update_status_t fw_update_wifi(firmware_update_t *fu,
                                         const char *url, uint32_t expected_crc32,
                                         uint32_t timeout_ms);

/* Update ESP32 firmware from a file on the SD card (USB).
 *
 * sd_path: path to the firmware binary (.bin) on the SD card.
 * expected_crc32: expected CRC32 of the image (0 = skip verification).
 * Mounts the SD card, reads the image, verifies its CRC32, writes it to the
 * OTA partition, and reboots on success. If the CRC does not match, the
 * update is aborted without flashing.
 *
 * Returns FW_UPDATE_OK on success (device will reboot), or an error status. */
firmware_update_status_t fw_update_usb(firmware_update_t *fu,
                                        const char *sd_path, uint32_t expected_crc32,
                                        uint32_t timeout_ms);

/* Update the motorcycle DTC database from a file on the SD card.
 *
 * sd_path: path to the DTC database file on the SD card.
 * Reads DTC entries from the file and registers them via dtc_register().
 * Does not reboot.
 *
 * Returns FW_UPDATE_OK on success, or an error status. */
firmware_update_status_t fw_update_motodb(firmware_update_t *fu,
                                           const char *sd_path,
                                           uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_FIRMWARE_UPDATE_H */
