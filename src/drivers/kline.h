/*
 * drivers/kline.h — K-Line / ISO 9141-2 driver for ESP32 DevKit V4.
 *
 * Physical layer: UART1 (GPIO17 TX, GPIO16 RX) through PC817 optocoupler.
 *   K-Line idle  ~12V (HIGH on wire), active ~0V (LOW on wire).
 *   PC817 inverts: UART TX HIGH -> K-Line LOW; UART TX LOW -> K-Line HIGH.
 *                 K-Line HIGH  -> UART RX LOW;  K-Line LOW  -> UART RX HIGH.
 *
 * Protocol: ISO 9141-2
 *   - Wake-up pulse: K-Line LOW 200..400 ms
 *   - 5-baud init:   start bit LOW ~200 ms, then 0x33 @ ~200 ms/bit
 *   - ECU reply:     0x55 @ 5 baud
 *   - Data phase:    10400 baud, 8N1
 *
 * Layering: APP -> J1979/KWP2000/UDS -> kline_* -> uart_* + GPIO bit-bang
 */

#ifndef DRIVERS_KLINE_H
#define DRIVERS_KLINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "uart.h"      /* scanner_uart_t, uart_status_t, scanner_uart_port_t */
#include "config/pinout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    KLINE_OK                    = 0,
    KLINE_ERR_INVALID_ARG       = -1,
    KLINE_ERR_NOT_INITIALIZED   = -2,
    KLINE_ERR_BUSY              = -3,
    KLINE_ERR_TIMEOUT           = -4,
    KLINE_ERR_PORT_RESERVED     = -5,   /* UART_NUM_0 is the debug console */
    KLINE_ERR_WAKEUP_FAILED     = -6,   /* no valid wake-up pulse seen */
    KLINE_ERR_INIT_FAILED       = -7,   /* 5-baud init / 0x55 reply mismatch */
    KLINE_ERR_WRITE_FAILED      = -8,
    KLINE_ERR_READ_FAILED       = -9,
    KLINE_ERR_LINK_DOWN         = -10,
} kline_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef struct {
    scanner_uart_port_t uart_port;        /* UART_PORT_1 for K-Line (UART_PORT_0 rejected) */
    int         tx_gpio;          /* K-Line TX (to PC817) */
    int         rx_gpio;          /* K-Line RX (from PC817) */
    uint32_t    baud_data;        /* 10400 for ISO 9141-2 data phase */
    uint32_t    wakeup_pulse_ms;  /* 200..400 (ISO 9141-2 window) */
    uint32_t    init_bit_ms;      /* ~200 (5-baud bit time) */
    uint32_t    timeout_ms;       /* data-phase RX/TX timeout */
    bool        invert_logic;     /* 1 = PC817 inverts K-Line polarity */
} kline_config_t;

typedef struct kline kline_t;     /* opaque handle */

/* Default config (ISO 9141-2, matches config/config.h). */
#define KLINE_CONFIG_DEFAULT                                             \
    {                                                                    \
        .uart_port       = UART_PORT_1,                                  \
        .tx_gpio         = KLINE_TX_GPIO,                                \
        .rx_gpio         = KLINE_RX_GPIO,                                \
        .baud_data       = KLINE_BAUD_DATA,                              \
        .wakeup_pulse_ms = KLINE_WAKEUP_PULSE_MS,                        \
        .init_bit_ms     = KLINE_INIT_BIT_MS,                            \
        .timeout_ms      = KLINE_TIMEOUT_MS,                             \
        .invert_logic    = KLINE_INVERT_LOGIC,                           \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize K-Line link. Rejects UART_PORT_0 (debug console).
 * Performs bus bring-up only; wake-up + 5-baud init done via kline_begin(). */
kline_status_t kline_init(const kline_config_t *cfg, kline_t **out_kline);

/* Release the K-Line handle and underlying UART. */
kline_status_t kline_deinit(kline_t *kline);

/* =========================================================================
 * Link bring-up (ISO 9141-2 initialization)
 * ========================================================================= */

/* Send wake-up pulse: drive K-Line LOW for wakeup_pulse_ms (200..400 ms),
 * then release HIGH and wait ~400 ms for the ECU to settle. */
kline_status_t kline_wakeup(kline_t *kline);

/* Detect a wake-up pulse from the ECU: measure how long K-Line RX stays LOW.
 * Returns KLINE_OK only if the LOW pulse is within [wakeup_pulse_ms - 50ms,
 * wakeup_pulse_ms + 100ms] (i.e. inside the ISO 9141-2 200..400 ms window).
 * Returns KLINE_ERR_TIMEOUT if no pulse within timeout_ms. */
kline_status_t kline_wakeup_detect(kline_t *kline, uint32_t timeout_ms);

/* Perform full ISO 9141-2 initialization:
 *   1. kline_wakeup()
 *   2. Send 0x33 at 5 baud (~200 ms/bit)
 *   3. Read ECU 0x55 reply at 5 baud
 *   4. Switch UART to 10400 baud data phase
 * Returns KLINE_OK when the data phase is ready. */
kline_status_t kline_begin(kline_t *kline);

/* =========================================================================
 * Data phase (10400 baud, 8N1)
 * ========================================================================= */

/* Send a K-Line frame (data phase). CS/UART owned by this handle. */
kline_status_t kline_send(kline_t *kline, const uint8_t *buf, size_t len,
                          uint32_t timeout_ms);

/* Receive up to len bytes (data phase). *out_len set to bytes read.
 * Returns KLINE_ERR_TIMEOUT if nothing arrives within timeout_ms. */
kline_status_t kline_receive(kline_t *kline, uint8_t *buf, size_t len,
                             size_t *out_len, uint32_t timeout_ms);

/* Send then receive (request/response). */
kline_status_t kline_transfer(kline_t *kline, const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_len, size_t *out_len,
                              uint32_t timeout_ms);

/* =========================================================================
 * Accessors
 * ========================================================================= */
uint32_t kline_baud_get(const kline_t *kline);
bool     kline_is_initialized(const kline_t *kline);
bool     kline_port_is_reserved(scanner_uart_port_t port);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_KLINE_H */