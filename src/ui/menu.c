/*
 * ui/menu.c — Implementasi sistem menu sederhana.
 *
 * Menggunakan display_draw_text + display_draw_rect untuk
 * rendering. Item yang terpilih diberi kotak highlight.
 */

#include "ui/menu.h"

#include <string.h>

/* =========================================================================
 * Konstanta layout
 * ========================================================================= */
#define MENU_TITLE_Y     20   /* Posisi Y judul */
#define MENU_ITEM_START  50   /* Posisi Y item pertama */
#define MENU_ITEM_H      24   /* Tinggi satu item (termasuk spasi) */
#define MENU_ITEM_X      10   /* Margin kiri teks item */
#define MENU_VISIBLE     6    /* Jumlah item yang tampak sekaligus */

/* =========================================================================
 * Inisialisasi
 * ========================================================================= */

void menu_init(menu_t *m, const char *title,
               const menu_item_t *items, uint8_t item_count)
{
    if (m == NULL) return;

    m->title      = title;
    m->items      = items;
    m->item_count = item_count;
    m->selected   = 0;
    m->top_index  = 0;
}

/* =========================================================================
 * Rendering
 * ========================================================================= */

void menu_draw(const menu_t *m, display_t *disp)
{
    if (m == NULL || disp == NULL) return;

    /* Bersihkan layar */
    display_fill_screen(disp, DISPLAY_COLOR_BLACK);

    /* Judul */
    if (m->title != NULL) {
        display_draw_text(disp, MENU_ITEM_X, MENU_TITLE_Y,
                          m->title, DISPLAY_COLOR_CYAN, 2);
    }

    /* Hitung range item yang akan ditampilkan */
    uint8_t start = m->top_index;
    uint8_t end = start + MENU_VISIBLE;
    if (end > m->item_count) end = m->item_count;

    /* Gambar setiap item */
    for (uint8_t i = start; i < end; i++) {
        int16_t y = MENU_ITEM_START + (int16_t)(i - start) * MENU_ITEM_H;
        bool    is_sel = (i == m->selected);

        /* Kotak highlight pada item terpilih */
        if (is_sel) {
            display_draw_rect(disp, MENU_ITEM_X - 4, y - 2,
                              200, MENU_ITEM_H - 4,
                              DISPLAY_COLOR_BLUE);
        }

        /* Teks item */
        display_draw_text(disp, MENU_ITEM_X, y,
                          m->items[i].label,
                          is_sel ? DISPLAY_COLOR_YELLOW : DISPLAY_COLOR_WHITE,
                          1);
    }

    /* Indikator scroll (panah atas/bawah) */
    if (m->top_index > 0) {
        display_draw_text(disp, 220, MENU_ITEM_START - 8,
                          "^", DISPLAY_COLOR_GRAY, 1);
    }
    if (m->top_index + MENU_VISIBLE < m->item_count) {
        display_draw_text(disp, 220, MENU_ITEM_START +
                          (int16_t)(MENU_VISIBLE - 1) * MENU_ITEM_H + 8,
                          "v", DISPLAY_COLOR_GRAY, 1);
    }
}

/* =========================================================================
 * Navigasi
 * ========================================================================= */

bool menu_navigate(menu_t *m, uint8_t btn_code)
{
    if (m == NULL || m->item_count == 0) return false;

    bool changed = false;

    if (btn_code & BTN_UP) {
        if (m->selected > 0) {
            m->selected--;
            if (m->selected < m->top_index) {
                m->top_index = m->selected;
            }
            changed = true;
        }
    } else if (btn_code & BTN_DOWN) {
        if (m->selected < m->item_count - 1) {
            m->selected++;
            if (m->selected >= m->top_index + MENU_VISIBLE) {
                m->top_index = m->selected - MENU_VISIBLE + 1;
            }
            changed = true;
        }
    }

    /* SELECT dan BACK ditangani oleh pemanggil (via on_select callback) */

    return changed;
}

uint8_t menu_get_selected(const menu_t *m)
{
    if (m == NULL) return 0;
    return m->selected;
}
