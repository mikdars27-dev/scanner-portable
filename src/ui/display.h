/*
 * ui/display.h — Driver TFT ILI9341 (SPI) untuk scanner OBD.
 *
 * Menggunakan drivers/spi.h untuk transfer SPI dan Arduino digitalWrite
 * untuk kontrol pin CS / DC / RST / BL.
 *
 * Layering: UI -> display_* -> spi_* + GPIO
 */

#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "drivers/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Warna ILI9341 (RGB565)
 * ========================================================================= */
#define DISPLAY_COLOR_BLACK     0x0000
#define DISPLAY_COLOR_BLUE      0x001F
#define DISPLAY_COLOR_RED       0xF800
#define DISPLAY_COLOR_GREEN     0x07E0
#define DISPLAY_COLOR_CYAN      0x07FF
#define DISPLAY_COLOR_MAGENTA   0xF81F
#define DISPLAY_COLOR_YELLOW    0xFFE0
#define DISPLAY_COLOR_WHITE     0xFFFF
#define DISPLAY_COLOR_GRAY      0x8410
#define DISPLAY_COLOR_DARKGRAY  0x4208

/* =========================================================================
 * Ukuran layar default (ILI9341 240x320 portrait)
 * ========================================================================= */
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320

/* =========================================================================
 * Konfigurasi pin (mirrors config/pinout.h)
 * ========================================================================= */
#ifndef DISPLAY_CS_GPIO
#define DISPLAY_CS_GPIO   TFT_CS_GPIO   /* 15 */
#endif
#ifndef DISPLAY_DC_GPIO
#define DISPLAY_DC_GPIO   TFT_DC_GPIO   /* 2 */
#endif
#ifndef DISPLAY_RST_GPIO
#define DISPLAY_RST_GPIO  TFT_RESET_GPIO /* 22 */
#endif
#ifndef DISPLAY_BL_GPIO
#define DISPLAY_BL_GPIO   TFT_BACKLIGHT_GPIO /* 21 */
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    DISPLAY_OK                    = 0,
    DISPLAY_ERR_INVALID_ARG       = -1,
    DISPLAY_ERR_NOT_INITIALIZED   = -2,
    DISPLAY_ERR_TRANSFER_FAILED   = -3,
} display_status_t;

/* =========================================================================
 * Handle (opaque)
 * ========================================================================= */
typedef struct display display_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    scanner_spi_device_t *spi_dev;   /* SPI device handle untuk TFT (SPI2_HOST) */
    int          cs_gpio;    /* Chip select */
    int          dc_gpio;    /* Data/Command select */
    int          rst_gpio;   /* Reset */
    int          bl_gpio;    /* Backlight */
    uint16_t     width;      /* Lebar layar (default 240) */
    uint16_t     height;     /* Tinggi layar (default 320) */
    uint8_t      rotation;   /* Rotasi 0..3 */
} display_config_t;

#define DISPLAY_CONFIG_DEFAULT(spi_dev_handle)                               \
    {                                                                        \
        .spi_dev   = (spi_dev_handle),                                       \
        .cs_gpio   = DISPLAY_CS_GPIO,                                        \
        .dc_gpio   = DISPLAY_DC_GPIO,                                        \
        .rst_gpio  = DISPLAY_RST_GPIO,                                       \
        .bl_gpio   = DISPLAY_BL_GPIO,                                        \
        .width     = DISPLAY_WIDTH,                                          \
        .height    = DISPLAY_HEIGHT,                                         \
        .rotation  = 0,                                                      \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Inisialisasi ILI9341: reset hardware, konfigurasi register, nyalakan display. */
display_status_t display_init(display_config_t *cfg, display_t **out_disp);

/* Lepaskan handle dan matikan display. */
display_status_t display_deinit(display_t *disp);

/* True jika display telah diinisialisasi. */
bool display_is_initialized(const display_t *disp);

/* =========================================================================
 * Dasar gambar
 * ========================================================================= */

/* Isi seluruh layar dengan satu warna. */
display_status_t display_fill_screen(display_t *disp, uint16_t color);

/* Gambar satu pixel. */
display_status_t display_draw_pixel(display_t *disp, int16_t x, int16_t y,
                                    uint16_t color);

/* Gambar persegi panjang terisi. */
display_status_t display_draw_rect(display_t *disp, int16_t x, int16_t y,
                                   int16_t w, int16_t h, uint16_t color);

/* Set rotasi layar (0..3). */
display_status_t display_set_rotation(display_t *disp, uint8_t rotation);

/* Matikan / nyalakan display (sleep mode). */
display_status_t display_sleep(display_t *disp);
display_status_t display_wake(display_t *disp);

/* Nyalakan / matikan backlight via GPIO. */
display_status_t display_backlight(display_t *disp, bool on);

/* =========================================================================
 * Teks (menggunakan font_5x7 dari ui/fonts.h)
 * ========================================================================= */

/* Gambar string teks pada (x, y) dengan warna dan ukuran (size = pengali). */
display_status_t display_draw_text(display_t *disp, int16_t x, int16_t y,
                                   const char *text, uint16_t color,
                                   uint8_t size);

/* =========================================================================
 * Helper: layar splash / judul sederhana
 * ========================================================================= */

/* Tampilkan layar awal dengan judul dan pesan singkat. */
display_status_t display_show_splash(display_t *disp, const char *title,
                                     const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* UI_DISPLAY_H */
