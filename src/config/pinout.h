/*
 * config/pinout.h — Pin mapping for the ESP32 DevKit V4 (GPIO numbers).
 *
 * Mirrors the board pinout documented in STRUKTUR_PROYEK_FIRMWARE.md and used
 * by the drivers (drivers/uart.h, drivers/kline.h, drivers/spi.h,
 * drivers/can_mcp2515.h).
 */
#ifndef CONFIG_PINOUT_H
#define CONFIG_PINOUT_H

/* UART allocation (ESP32 has 3 hardware UARTs: UART0, UART1, UART2)
 * UART0 is the debug console (GPIO1 TX / GPIO3 RX) and is reserved. */
#define UART_DEBUG_NUM        0   /* Debug console — never K-Line */
#define UART_KLINE_NUM        1   /* K-Line ISO 9141-2 via PC817 optocoupler */
#define UART_RECOVERY_NUM     2   /* UART recovery, 115200 baud */

/* K-Line (UART1) — GPIO17 TX, GPIO16 RX */
#define KLINE_TX_GPIO         17
#define KLINE_RX_GPIO         16
#define KLINE_INVERT_LOGIC    1   /* PC817 inverts K-Line polarity */

/* CAN via MCP2515 (SPI) — SCK 18, MOSI 23, MISO 19, CS 5, INT 4 */
#define CAN_SCK_GPIO          18
#define CAN_MOSI_GPIO         23
#define CAN_MISO_GPIO         19
#define CAN_CS_GPIO           5
#define CAN_INT_GPIO          4

/* microSD (SPI terpisah) — SCK 14, MOSI 12, MISO 25, CS 13 */
#define SD_SCK_GPIO         14
#define SD_MOSI_GPIO        12
#define SD_MISO_GPIO        25
#define SD_CS_GPIO          13

/* TFT ILI9341 (SPI) — CS 15, DC 2, Reset 22 (moved from 4 to avoid CAN_INT conflict), Backlight 21 */
#define TFT_CS_GPIO           15
#define TFT_DC_GPIO           2
#define TFT_RESET_GPIO        22
#define TFT_BACKLIGHT_GPIO    21

/* Tombol fisik (navigasi UI) — GPIO bebas pada ESP32 DevKit V4 */
#define BTN_UP_GPIO           26
#define BTN_DOWN_GPIO         27
#define BTN_SELECT_GPIO       32
#define BTN_BACK_GPIO         33

/* Power / ignition-sense (via PC817 optocoupler) */
#define IGNITION_SENSE_GPIO   34   /* input-only, via PC817 — ignition detect */

/* BOOT0 — ESP32 GPIO0 selects the system bootloader for recovery via UART */
#define ESP_BOOT0_GPIO        0

/* ADC — free ADC1 pins (input-only) for battery/cranking/charging measurement.
 * GPIO 35-39 are ADC1, input-only, and not used by any other driver. */
#define ADC_DEFAULT_GPIO      35   /* default ADC measurement pin */

#endif /* CONFIG_PINOUT_H */
