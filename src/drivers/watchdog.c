/*
 * drivers/watchdog.c — Watchdog abstraction (ESP32 TWDT).
 *
 * Digunakan untuk memastikan bahwa state machine scanner tidak terkurung
 * selamanya. Jika watchdog tidak diberi "kick" dalam waktu yang ditentukan,
 * perangkat akan di-reset.
 *
 * Implementasi untuk ESP32 DevKit V4 (Arduino-ESP32). Menggunakan
 * ESP-IDF Task Watchdog Timer (TWDT) dengan fallback ESP.restart().
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "drivers/watchdog.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <Arduino.h>
#include <esp_task_wdt.h>

/* =========================================================================
 * Internal state.
 * ========================================================================= */

static bool            watchdog_enabled = false;
static watchdog_callback_t watchdog_reset_cb = NULL;

/* =========================================================================
 * Public API.
 * ========================================================================= */

watchdog_status_t watchdog_init(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return WATCHDOG_ERR_INVALID_ARG;
    }

    /* TWDT timeout dalam detik. ESP32 TWDT minimum ~1 detik. */
    uint32_t timeout_s = timeout_ms / 1000UL;
    if (timeout_s < 1) {
        timeout_s = 1;
    }

    esp_err_t err = esp_task_wdt_init(timeout_s, true); /* panic = true */
    if (err != ESP_OK) {
        return WATCHDOG_ERR_INIT_FAIL;
    }

    /* Tambahkan task loop utama ke TWDT. */
    err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        return WATCHDOG_ERR_INIT_FAIL;
    }

    watchdog_enabled = true;
    return WATCHDOG_OK;
}

watchdog_status_t watchdog_kick(void)
{
    if (!watchdog_enabled) {
        return WATCHDOG_OK;
    }

    esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        return WATCHDOG_ERR_KICK_FAIL;
    }
    return WATCHDOG_OK;
}

watchdog_status_t watchdog_disable(void)
{
    if (!watchdog_enabled) {
        return WATCHDOG_OK;
    }

    esp_task_wdt_delete(NULL);
    esp_task_wdt_deinit();
    watchdog_enabled = false;
    return WATCHDOG_OK;
}

watchdog_status_t watchdog_set_reset_callback(watchdog_callback_t cb)
{
    watchdog_reset_cb = cb;
    return WATCHDOG_OK;
}

/* Dipanggil oleh tingkat paling atas (loop utama) sebagai fallback jika
 * TWDT tidak tersedia. Panggil secara berkala. */
void watchdog_poll(void)
{
    if (watchdog_enabled) {
        watchdog_kick();
    }
}
