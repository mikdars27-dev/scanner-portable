/*
 * ui/buttons.h — Input tombol fisik untuk navigasi UI.
 *
 * Tombol dibaca via Arduino digitalRead() dengan INPUT_PULLUP.
 * Tombol ditekan = LOW (aktif- rendah).
 *
 * Layering: UI -> buttons_* -> GPIO (Arduino digitalRead)
 */

#ifndef UI_BUTTONS_H
#define UI_BUTTONS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "config/pinout.h"

/* =========================================================================
 * Kode tombol
 * ========================================================================= */
typedef enum {
    BTN_NONE   = 0,
    BTN_UP     = 1 << 0,
    BTN_DOWN   = 1 << 1,
    BTN_SELECT = 1 << 2,
    BTN_BACK   = 1 << 3,
} btn_code_t;

/* =========================================================================
 * Inisialisasi & pembacaan
 * ========================================================================= */

/* Atur semua pin tombol sebagai INPUT_PULLUP. Panggil sekali di setup(). */
void buttons_init(void);

/* Baca tombol yang saat ini ditekan (mengembalikan kombinasi BTN_*,
 * atau BTN_NONE jika tidak ada). Tidak ada debounce di sini —
 * pemanggil atur interval polling sendiri. */
uint8_t buttons_read(void);

/* Tunggu hingga semua tombol dilepas (untuk mencegah double-trigger). */
void buttons_wait_release(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BUTTONS_H */
