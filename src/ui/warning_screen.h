/*
 * ui/warning_screen.h — Layar peringatan keselamatan (safety warning).
 *
 * Digunakan sebelum operasi berisiko: tes aktuator, flash ECU,
 * hapus DTC, dll. Menampilkan pesan peringatan dan menunggu
 * konfirmasi pengguna (SELECT = setuju, BACK = batal).
 *
 * Layering: UI -> warning_screen_* -> display_* + buttons_*
 */

#ifndef UI_WARNING_SCREEN_H
#define UI_WARNING_SCREEN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ui/display.h"
#include "ui/buttons.h"

/* =========================================================================
 * Tipe peringatan
 * ========================================================================= */
typedef enum {
    WARN_ACTUATOR_TEST,    /* Tes aktuator — bisa berbahaya */
    WARN_ECU_FLASH,        /* Flash ECU — risiko brick */
    WARN_CLEAR_DTC,        /* Hapus DTC — tidak dapat diurungkan */
    WARN_CUSTOM,           /* Pesan custom */
} warning_type_t;

/* =========================================================================
 * API
 * ========================================================================= */

/* Tampilkan layar peringatan. Mengembalikan true jika pengguna
 * menekan SELECT (setuju), false jika BACK (batal) atau timeout. */
bool warning_show(display_t *disp, warning_type_t type,
                  const char *custom_msg, uint32_t timeout_ms);

/* Tampilkan pesan peringatan sederhana tanpa tombol (info only). */
void warning_info(display_t *disp, const char *title, const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* UI_WARNING_SCREEN_H */
