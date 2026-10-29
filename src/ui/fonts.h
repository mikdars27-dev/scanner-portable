/*
 * ui/fonts.h — Bitmap font untuk layar TFT.
 *
 * Font 5x7 pixel (ASCII 0x20..0x7F). Setiap karakter direpresentasikan
 * oleh 5 kolom, setiap kolom 1 byte (7 bit digunakan, bit 0 = baris paling atas).
 *
 * Layering: UI -> display_* -> fonts_*
 */

#ifndef UI_FONTS_H
#define UI_FONTS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dimensi font 5x7 */
#define FONT_WIDTH  5
#define FONT_HEIGHT 7
#define FONT_SPACING 1   /* spasi antar karakter (pixel) */

/* Rentang karakter yang didukung: 0x20 (spasi) .. 0x7F ('~') */
#define FONT_FIRST_CHAR 0x20
#define FONT_LAST_CHAR  0x7F
#define FONT_CHAR_COUNT (FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)

/*
 * Glyph data: [FONT_CHAR_COUNT][FONT_WIDTH]
 * Setiap entri = 5 byte, satu byte per kolom.
 * Bit 0 = baris teratas, bit 6 = baris terbawah (7 baris total).
 */
extern const uint8_t font_5x7[FONT_CHAR_COUNT][FONT_WIDTH];

/*
 * Ambil lebar teks dalam pixel untuk ukuran font tertentu.
 * size = pengali (1 = 5x7, 2 = 10x14, dst).
 */
size_t font_text_width(const char *text, uint8_t size);

/*
 * Ambil tinggi teks dalam pixel untuk ukuran font tertentu.
 */
size_t font_text_height(uint8_t size);

#ifdef __cplusplus
}
#endif

#endif /* UI_FONTS_H */
