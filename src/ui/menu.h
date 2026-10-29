/*
 * ui/menu.h — Sistem menu sederhana untuk navigasi layar TFT.
 *
 * Menampilkan daftar item dengan highlight pada seleksi saat ini.
 * Navigasi via tombol UP/DOWN/SELECT/BACK.
 *
 * Layering: UI -> menu_* -> display_* + buttons_*
 */

#ifndef UI_MENU_H
#define UI_MENU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ui/display.h"
#include "ui/buttons.h"

/* =========================================================================
 * Item dan menu
 * ========================================================================= */

typedef struct {
    const char *label;       /* Teks yang ditampilkan */
    void (*on_select)(void); /* Callback saat dipilih (boleh NULL) */
} menu_item_t;

typedef struct {
    const char      *title;      /* Judul menu */
    const menu_item_t *items;    /* Array item */
    uint8_t          item_count; /* Jumlah item */
    uint8_t          selected;   /* Index item yang terpilih */
    uint8_t          top_index;  /* Index item teratas yang ditampilkan (scroll) */
} menu_t;

/* =========================================================================
 * API
 * ========================================================================= */

/* Inisialisasi menu. selected = 0, top_index = 0. */
void menu_init(menu_t *m, const char *title,
               const menu_item_t *items, uint8_t item_count);

/* Gambar menu ke layar. Memerlukan display_t yang sudah diinisialisasi. */
void menu_draw(const menu_t *m, display_t *disp);

/* Proses input tombol. Mengembalikan true jika ada perubahan
 * yang perlu digambar ulang. */
bool menu_navigate(menu_t *m, uint8_t btn_code);

/* Index item yang saat ini terpilih. */
uint8_t menu_get_selected(const menu_t *m);

#ifdef __cplusplus
}
#endif

#endif /* UI_MENU_H */
