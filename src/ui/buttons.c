/*
 * ui/buttons.c — Implementasi pembacaan tombol fisik.
 *
 * Semua tombol menggunakan INPUT_PULLUP; ditekan = LOW.
 * Tidak ada debounce hardware; debounce ditangani oleh pemanggil
 * melalui interval polling.
 */

#include "ui/buttons.h"

#include <Arduino.h>

/* =========================================================================
 * Inisialisasi
 * ========================================================================= */

void buttons_init(void)
{
    pinMode(BTN_UP_GPIO, INPUT_PULLUP);
    pinMode(BTN_DOWN_GPIO, INPUT_PULLUP);
    pinMode(BTN_SELECT_GPIO, INPUT_PULLUP);
    pinMode(BTN_BACK_GPIO, INPUT_PULLUP);
}

/* =========================================================================
 * Pembacaan
 * ========================================================================= */

uint8_t buttons_read(void)
{
    uint8_t code = BTN_NONE;

    if (digitalRead(BTN_UP_GPIO) == LOW)     code |= BTN_UP;
    if (digitalRead(BTN_DOWN_GPIO) == LOW)   code |= BTN_DOWN;
    if (digitalRead(BTN_SELECT_GPIO) == LOW) code |= BTN_SELECT;
    if (digitalRead(BTN_BACK_GPIO) == LOW)   code |= BTN_BACK;

    return code;
}

void buttons_wait_release(void)
{
    while (buttons_read() != BTN_NONE) {
        delay(10);
        yield();
    }
}
