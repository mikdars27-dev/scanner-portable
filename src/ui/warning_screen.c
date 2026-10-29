/*
 * ui/warning_screen.c — Implementasi layar peringatan keselamatan.
 *
 * Menampilkan judul, ikon peringatan, dan pesan. Menunggu
 * tombol SELECT (setuju) atau BACK (batal). Opsional timeout.
 */

#include "ui/warning_screen.h"

#include "ui/fonts.h"

#include <Arduino.h>
#include <string.h>

/* =========================================================================
 * Pesan default per tipe peringatan
 * ========================================================================= */

static const char *warn_titles[] = {
    [WARN_ACTUATOR_TEST] = "PERINGATAN: Tes Aktuator",
    [WARN_ECU_FLASH]     = "PERINGATAN: Flash ECU",
    [WARN_CLEAR_DTC]     = "PERINGATAN: Hapus DTC",
    [WARN_CUSTOM]        = "PERINGATAN",
};

static const char *warn_msgs[] = {
    [WARN_ACTUATOR_TEST] =
        "Tes aktuator dapat menggerakkan\n"
        "komponen kendaraan. Pastikan\n"
        "kendaraan dalam posisi aman\n"
        "dan tidak ada orang di dekat\n"
        "roda / komponen yang bergerak.",
    [WARN_ECU_FLASH] =
        "Flash ECU dapat merusak ECU\n"
        "(brick) jika gagal. Pastikan\n"
        "cadangan (backup) sudah dibuat\n"
        "dan supply listrik stabil.",
    [WARN_CLEAR_DTC] =
        "Menghapus DTC tidak dapat\n"
        "diurungkan. Pastikan masalah\n"
        "telah diperbaiki sebelum\n"
        "menghapus kode kesalahan.",
    [WARN_CUSTOM] = NULL,
};

/* =========================================================================
 * Rendering
 * ========================================================================= */

static void warning_render(display_t *disp, const char *title,
                           const char *msg, bool waiting)
{
    display_fill_screen(disp, DISPLAY_COLOR_BLACK);

    /* Judul (merah) */
    if (title != NULL) {
        int16_t tw = (int16_t)font_text_width(title, 2);
        int16_t tx = (disp->width - tw) / 2;
        display_draw_text(disp, tx, 50, title, DISPLAY_COLOR_RED, 2);
    }

    /* Simbol peringatan (segitiga sederhana) */
    display_draw_rect(disp, disp->width / 2 - 10, 100, 20, 2,
                      DISPLAY_COLOR_YELLOW);
    display_draw_rect(disp, disp->width / 2 - 6, 102, 4, 14,
                      DISPLAY_COLOR_YELLOW);
    display_draw_rect(disp, disp->width / 2 - 2, 118, 4, 4,
                      DISPLAY_COLOR_YELLOW);

    /* Pesan (putih) */
    if (msg != NULL) {
        display_draw_text(disp, 30, 140, msg, DISPLAY_COLOR_WHITE, 1);
    }

    /* Petunjuk tombol */
    if (waiting) {
        display_draw_text(disp, 30, 260,
                          "SELECT=Setuju  BACK=Batal",
                          DISPLAY_COLOR_CYAN, 1);
    }
}

/* =========================================================================
 * API
 * ========================================================================= */

bool warning_show(display_t *disp, warning_type_t type,
                  const char *custom_msg, uint32_t timeout_ms)
{
    if (disp == NULL) return false;

    const char *title = warn_titles[type];
    const char *msg   = (type == WARN_CUSTOM && custom_msg != NULL)
                        ? custom_msg : warn_msgs[type];

    warning_render(disp, title, msg, true);

    uint32_t start = millis();
    bool     confirmed = false;

    for (;;) {
        uint8_t btn = buttons_read();

        if (btn & BTN_SELECT) {
            confirmed = true;
            buttons_wait_release();
            break;
        }
        if (btn & BTN_BACK) {
            confirmed = false;
            buttons_wait_release();
            break;
        }

        if (timeout_ms > 0 &&
            (uint32_t)(millis() - start) >= timeout_ms)
        {
            confirmed = false;
            break;
        }

        delay(20);
        yield();
    }

    /* Tampilkan hasil singkat */
    warning_render(disp, title,
                   confirmed ? "Dikonfirmasi." : "Dibatalkan.",
                   false);
    delay(800);

    return confirmed;
}

void warning_info(display_t *disp, const char *title, const char *msg)
{
    if (disp == NULL) return;
    warning_render(disp, title, msg, false);
    delay(1500);
}
