/*
 * ui/display.c — Driver TFT ILI9341 (SPI) untuk scanner OBD.
 *
 * Komunikasi SPI melalui drivers/spi.h. Kontrol pin CS/DC/RST/BL
 * menggunakan Arduino digitalWrite (diganti dengan driver GPIO kustom
 * pada fase selanjutnya).
 *
 * Layering: UI -> display_* -> spi_* + GPIO (Arduino)
 */

#include "ui/display.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "ui/fonts.h"

/* =========================================================================
 * ILI9341 commands
 * ========================================================================= */
#define ILI9341_SWRESET       0x01
#define ILI9341_SLEEP_OUT     0x11
#define ILI9341_GAMMA_SET     0x26
#define ILI9341_DISPLAY_OFF   0x28
#define ILI9341_DISPLAY_ON    0x29
#define ILI9341_COLUMN_ADDR   0x2A
#define ILI9341_PAGE_ADDR     0x2B
#define ILI9341_MEMORY_WRITE  0x2C
#define ILI9341_MAC           0x36   /* Memory Access Control */
#define ILI9341_PIXEL_FORMAT  0x3A
#define ILI9341_FRAME_CTRL    0xB1
#define ILI9341_DISP_FUNC_CTRL 0xB6
#define ILI9341_POWER_CTRL1   0xC0
#define ILI9341_POWER_CTRL2   0xC1
#define ILI9341_VCOM_CTRL1    0xC5
#define ILI9341_VCOM_CTRL2    0xC7
#define ILI9341_GAMMA_POS     0xE0
#define ILI9341_GAMMA_NEG     0xE1
#define ILI9341_POWER_CTRL_A  0xCB
#define ILI9341_POWER_CTRL_B  0xCF
#define ILI9341_DTIM_CTRL_A   0xE8
#define ILI9341_DTIM_CTRL_B   0xEA
#define ILI9341_POWER_ON_SEQ  0xED
#define ILI9341_PUMP_RATIO    0xF7
#define ILI9341_GAMMA_DIS     0xF2

/* =========================================================================
 * Internal handle
 * ========================================================================= */
struct display {
    scanner_spi_device_t *spi_dev;
    int           cs_gpio;
    int           dc_gpio;
    int           rst_gpio;
    int           bl_gpio;
    uint16_t      width;
    uint16_t      height;
    uint8_t       rotation;
    bool          initialized;
};

/* =========================================================================
 * Low-level SPI helpers (CS/DC pin toggling via Arduino digitalWrite)
 * ========================================================================= */

static void display_cs_low(display_t *d)
{
    digitalWrite(d->cs_gpio, LOW);
}

static void display_cs_high(display_t *d)
{
    digitalWrite(d->cs_gpio, HIGH);
}

static void display_dc_cmd(display_t *d)
{
    digitalWrite(d->dc_gpio, LOW);
}

static void display_dc_data(display_t *d)
{
    digitalWrite(d->dc_gpio, HIGH);
}

/* Kirim satu command byte (DC=LOW, CS dipertahankan oleh pemanggil). */
static display_status_t display_write_cmd(display_t *d, uint8_t cmd)
{
    spi_status_t st = spi_transfer(d->spi_dev, &cmd, NULL, 1, 100);
    if (st != SPI_OK) return DISPLAY_ERR_TRANSFER_FAILED;
    return DISPLAY_OK;
}

/* Kirim data (DC=HIGH, CS dipertahankan oleh pemanggil). */
static display_status_t display_write_data(display_t *d, const uint8_t *buf,
                                            size_t len)
{
    spi_status_t st = spi_transfer(d->spi_dev, buf, NULL, len, 100);
    if (st != SPI_OK) return DISPLAY_ERR_TRANSFER_FAILED;
    return DISPLAY_OK;
}

/* Command + data sekaligus: CS diturunkan, DC diatur, command dikirim,
 * data dikirim, CS dinaikkan. */
static display_status_t display_cmd_data(display_t *d, uint8_t cmd,
                                         const uint8_t *data, size_t data_len)
{
    display_cs_low(d);
    display_dc_cmd(d);
    if (display_write_cmd(d, cmd) != DISPLAY_OK) {
        display_cs_high(d);
        return DISPLAY_ERR_TRANSFER_FAILED;
    }
    if (data_len > 0 && data != NULL) {
        display_dc_data(d);
        if (display_write_data(d, data, data_len) != DISPLAY_OK) {
            display_cs_high(d);
            return DISPLAY_ERR_TRANSFER_FAILED;
        }
    }
    display_cs_high(d);
    return DISPLAY_OK;
}

/* Set window address (kolom + baris) untuk drawing. */
static display_status_t display_set_addr_window(display_t *d,
                                                int16_t x0, int16_t y0,
                                                int16_t x1, int16_t y1)
{
    uint8_t buf[4];

    /* Column address */
    buf[0] = (uint8_t)(x0 >> 8);
    buf[1] = (uint8_t)(x0 & 0xFF);
    buf[2] = (uint8_t)(x1 >> 8);
    buf[3] = (uint8_t)(x1 & 0xFF);
    if (display_cmd_data(d, ILI9341_COLUMN_ADDR, buf, 4) != DISPLAY_OK)
        return DISPLAY_ERR_TRANSFER_FAILED;

    /* Row address */
    buf[0] = (uint8_t)(y0 >> 8);
    buf[1] = (uint8_t)(y0 & 0xFF);
    buf[2] = (uint8_t)(y1 >> 8);
    buf[3] = (uint8_t)(y1 & 0xFF);
    if (display_cmd_data(d, ILI9341_PAGE_ADDR, buf, 4) != DISPLAY_OK)
        return DISPLAY_ERR_TRANSFER_FAILED;

    return DISPLAY_OK;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

display_status_t display_init(display_config_t *cfg, display_t **out_disp)
{
    if (cfg == NULL || out_disp == NULL || cfg->spi_dev == NULL)
        return DISPLAY_ERR_INVALID_ARG;

    /* Konfigurasi pin GPIO via Arduino */
    pinMode(cfg->cs_gpio, OUTPUT);
    pinMode(cfg->dc_gpio, OUTPUT);
    pinMode(cfg->rst_gpio, OUTPUT);
    pinMode(cfg->bl_gpio, OUTPUT);

    /* CS idle high, DC idle high, backlight off initially */
    digitalWrite(cfg->cs_gpio, HIGH);
    digitalWrite(cfg->dc_gpio, HIGH);
    digitalWrite(cfg->bl_gpio, LOW);

    /* Hardware reset */
    digitalWrite(cfg->rst_gpio, LOW);
    delay(10);
    digitalWrite(cfg->rst_gpio, HIGH);
    delay(120);

    display_t *d = (display_t *)calloc(1, sizeof(*d));
    if (d == NULL) return DISPLAY_ERR_INVALID_ARG;

    d->spi_dev   = cfg->spi_dev;
    d->cs_gpio   = cfg->cs_gpio;
    d->dc_gpio   = cfg->dc_gpio;
    d->rst_gpio  = cfg->rst_gpio;
    d->bl_gpio   = cfg->bl_gpio;
    d->width     = cfg->width;
    d->height    = cfg->height;
    d->rotation  = cfg->rotation;
    d->initialized = false;

    /* --- ILI9341 initialization sequence --- */

    display_cmd_data(d, ILI9341_SWRESET, NULL, 0);
    delay(120);

    static const uint8_t pwr_a[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
    display_cmd_data(d, ILI9341_POWER_CTRL_A, pwr_a, sizeof(pwr_a));

    static const uint8_t pwr_b[] = { 0x00, 0xC1, 0x30 };
    display_cmd_data(d, ILI9341_POWER_CTRL_B, pwr_b, sizeof(pwr_b));

    static const uint8_t dtim_a[] = { 0x85, 0x00, 0x78 };
    display_cmd_data(d, ILI9341_DTIM_CTRL_A, dtim_a, sizeof(dtim_a));

    static const uint8_t dtim_b[] = { 0x00, 0x00 };
    display_cmd_data(d, ILI9341_DTIM_CTRL_B, dtim_b, sizeof(dtim_b));

    static const uint8_t pwr_seq[] = { 0x64, 0x03, 0x12, 0x81 };
    display_cmd_data(d, ILI9341_POWER_ON_SEQ, pwr_seq, sizeof(pwr_seq));

    static const uint8_t pump[] = { 0x20 };
    display_cmd_data(d, ILI9341_PUMP_RATIO, pump, sizeof(pump));

    static const uint8_t pwr1[] = { 0x23 };
    display_cmd_data(d, ILI9341_POWER_CTRL1, pwr1, sizeof(pwr1));

    static const uint8_t pwr2[] = { 0x10 };
    display_cmd_data(d, ILI9341_POWER_CTRL2, pwr2, sizeof(pwr2));

    static const uint8_t vcom1[] = { 0x3E, 0x28 };
    display_cmd_data(d, ILI9341_VCOM_CTRL1, vcom1, sizeof(vcom1));

    static const uint8_t vcom2[] = { 0x86 };
    display_cmd_data(d, ILI9341_VCOM_CTRL2, vcom2, sizeof(vcom2));

    /* MAC: rotation 0 = 0x48 (MY=0, MX=0, MV=0, ML=0, BGR=1, MH=0) */
    static const uint8_t mac0[] = { 0x48 };
    display_cmd_data(d, ILI9341_MAC, mac0, sizeof(mac0));

    static const uint8_t pixfmt[] = { 0x55 }; /* 16-bit per pixel */
    display_cmd_data(d, ILI9341_PIXEL_FORMAT, pixfmt, sizeof(pixfmt));

    static const uint8_t frctrl[] = { 0x00, 0x18 };
    display_cmd_data(d, ILI9341_FRAME_CTRL, frctrl, sizeof(frctrl));

    static const uint8_t dfunc[] = { 0x08, 0x82, 0x27 };
    display_cmd_data(d, ILI9341_DISP_FUNC_CTRL, dfunc, sizeof(dfunc));

    static const uint8_t gdis[] = { 0x00 };
    display_cmd_data(d, ILI9341_GAMMA_DIS, gdis, sizeof(gdis));

    static const uint8_t gset[] = { 0x01 };
    display_cmd_data(d, ILI9341_GAMMA_SET, gset, sizeof(gset));

    static const uint8_t gpos[] = {
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
        0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00
    };
    display_cmd_data(d, ILI9341_GAMMA_POS, gpos, sizeof(gpos));

    static const uint8_t gneg[] = {
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
        0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F
    };
    display_cmd_data(d, ILI9341_GAMMA_NEG, gneg, sizeof(gneg));

    /* Exit sleep */
    display_cmd_data(d, ILI9341_SLEEP_OUT, NULL, 0);
    delay(120);

    /* Display on */
    display_cmd_data(d, ILI9341_DISPLAY_ON, NULL, 0);
    delay(50);

    /* Nyalakan backlight */
    digitalWrite(cfg->bl_gpio, HIGH);

    d->initialized = true;
    *out_disp = d;
    return DISPLAY_OK;
}

display_status_t display_deinit(display_t *disp)
{
    if (disp == NULL) return DISPLAY_ERR_INVALID_ARG;
    if (disp->initialized) {
        display_sleep(disp);
        digitalWrite(disp->bl_gpio, LOW);
    }
    free(disp);
    return DISPLAY_OK;
}

bool display_is_initialized(const display_t *disp)
{
    return disp != NULL && disp->initialized;
}

/* =========================================================================
 * Drawing
 * ========================================================================= */

display_status_t display_fill_screen(display_t *disp, uint16_t color)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;

    if (display_set_addr_window(disp, 0, 0,
                                disp->width - 1, disp->height - 1) != DISPLAY_OK)
        return DISPLAY_ERR_TRANSFER_FAILED;

    /* 240 * 320 = 76800 > 65535, jadi harus pakai uint32_t. */
    uint32_t total = (uint32_t)disp->width * (uint32_t)disp->height;
    uint8_t  hi = (uint8_t)(color >> 8);
    uint8_t  lo = (uint8_t)(color & 0xFF);

    /* Kirim pixel warna secara berulang (buffer 512 byte). */
    uint8_t buf[512];
    size_t  chunk = 0;
    for (size_t i = 0; i < 512; i += 2) {
        buf[i] = hi;
        buf[i + 1] = lo;
        chunk += 2;
    }

    display_cs_low(disp);
    display_dc_data(disp);

    uint32_t remaining = total * 2;
    while (remaining > 0) {
        size_t to_send = (remaining < (uint32_t)chunk) ? (size_t)remaining : chunk;
        spi_status_t st = spi_transfer(disp->spi_dev, buf, NULL, to_send, 100);
        if (st != SPI_OK) {
            display_cs_high(disp);
            return DISPLAY_ERR_TRANSFER_FAILED;
        }
        remaining -= to_send;
    }

    display_cs_high(disp);
    return DISPLAY_OK;
}

display_status_t display_draw_pixel(display_t *disp, int16_t x, int16_t y,
                                    uint16_t color)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;
    if (x < 0 || x >= (int16_t)disp->width ||
        y < 0 || y >= (int16_t)disp->height)
        return DISPLAY_ERR_INVALID_ARG;

    if (display_set_addr_window(disp, x, y, x, y) != DISPLAY_OK)
        return DISPLAY_ERR_TRANSFER_FAILED;

    uint8_t buf[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    display_cs_low(disp);
    display_dc_data(disp);
    spi_status_t st = spi_transfer(disp->spi_dev, buf, NULL, 2, 100);
    display_cs_high(disp);
    if (st != SPI_OK) return DISPLAY_ERR_TRANSFER_FAILED;
    return DISPLAY_OK;
}

display_status_t display_draw_rect(display_t *disp, int16_t x, int16_t y,
                                   int16_t w, int16_t h, uint16_t color)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;
    if (w <= 0 || h <= 0) return DISPLAY_ERR_INVALID_ARG;

    /* Clamp x, y dulu, baru hitung x1, y1. */
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    int16_t x1 = x + w - 1;
    int16_t y1 = y + h - 1;
    if (x1 >= (int16_t)disp->width)  x1 = (int16_t)disp->width - 1;
    if (y1 >= (int16_t)disp->height) y1 = (int16_t)disp->height - 1;

    if (display_set_addr_window(disp, x, y, x1, y1) != DISPLAY_OK)
        return DISPLAY_ERR_TRANSFER_FAILED;

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t  lo = (uint8_t)(color & 0xFF);
    uint32_t total = (uint32_t)w * (uint32_t)h;

    uint8_t buf[512];
    size_t  chunk = 0;
    for (size_t i = 0; i < 512; i += 2) {
        buf[i] = hi;
        buf[i + 1] = lo;
        chunk += 2;
    }

    display_cs_low(disp);
    display_dc_data(disp);

    uint32_t remaining = total * 2;
    while (remaining > 0) {
        size_t to_send = (remaining < (uint32_t)chunk) ? (size_t)remaining : chunk;
        spi_status_t st = spi_transfer(disp->spi_dev, buf, NULL, to_send, 100);
        if (st != SPI_OK) {
            display_cs_high(disp);
            return DISPLAY_ERR_TRANSFER_FAILED;
        }
        remaining -= to_send;
    }

    display_cs_high(disp);
    return DISPLAY_OK;
}

display_status_t display_set_rotation(display_t *disp, uint8_t rotation)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;
    if (rotation > 3) return DISPLAY_ERR_INVALID_ARG;

    uint8_t mac;
    switch (rotation) {
        case 0: mac = 0x48; break;   /* Portrait */
        case 1: mac = 0xE8; break;   /* Landscape */
        case 2: mac = 0x88; break;   /* Portrait flipped */
        case 3: mac = 0x28; break;   /* Landscape flipped */
        default: mac = 0x48;
    }
    disp->rotation = rotation;
    return display_cmd_data(disp, ILI9341_MAC, &mac, 1);
}

display_status_t display_sleep(display_t *disp)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;
    return display_cmd_data(disp, ILI9341_SLEEP_OUT, NULL, 0);
}

display_status_t display_wake(display_t *disp)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;
    return display_cmd_data(disp, ILI9341_DISPLAY_ON, NULL, 0);
}

display_status_t display_backlight(display_t *disp, bool on)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;
    digitalWrite(disp->bl_gpio, on ? HIGH : LOW);
    return DISPLAY_OK;
}

/* =========================================================================
 * Teks (font 5x7)
 * ========================================================================= */

display_status_t display_draw_text(display_t *disp, int16_t x, int16_t y,
                                   const char *text, uint16_t color,
                                   uint8_t size)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;
    if (text == NULL || size == 0) return DISPLAY_ERR_INVALID_ARG;

    int16_t cur_x = x;
    int16_t cur_y = y;

    while (*text) {
        char c = *text++;

        /* Line break: pindah ke baris berikutnya, reset X. */
        if (c == '\n' || c == '\r') {
            cur_x = x;
            cur_y += (int16_t)(FONT_HEIGHT * size + 2);
            continue;
        }

        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) {
            cur_x += (FONT_WIDTH + FONT_SPACING) * size;
            continue;
        }

        const uint8_t *glyph = font_5x7[c - FONT_FIRST_CHAR];

        for (int col = 0; col < FONT_WIDTH; col++) {
            uint8_t col_bits = glyph[col];
            for (int row = 0; row < FONT_HEIGHT; row++) {
                if (col_bits & (1 << row)) {
                    for (int sy = 0; sy < size; sy++) {
                        for (int sx = 0; sx < size; sx++) {
                            int16_t px = cur_x + col * size + sx;
                            int16_t py = cur_y + row * size + sy;
                            display_draw_pixel(disp, px, py, color);
                        }
                    }
                }
            }
        }

        cur_x += (FONT_WIDTH + FONT_SPACING) * size;
    }

    return DISPLAY_OK;
}

display_status_t display_show_splash(display_t *disp, const char *title,
                                     const char *msg)
{
    if (!disp->initialized) return DISPLAY_ERR_NOT_INITIALIZED;

    display_fill_screen(disp, DISPLAY_COLOR_BLACK);

    /* Judul di tengah atas */
    if (title != NULL) {
        int16_t tw = (int16_t)font_text_width(title, 2);
        int16_t tx = (disp->width - tw) / 2;
        display_draw_text(disp, tx, 40, title, DISPLAY_COLOR_CYAN, 2);
    }

    /* Pesan di tengah */
    if (msg != NULL) {
        int16_t mw = (int16_t)font_text_width(msg, 1);
        int16_t mx = (disp->width - mw) / 2;
        display_draw_text(disp, mx, 120, msg, DISPLAY_COLOR_WHITE, 1);
    }

    return DISPLAY_OK;
}
