/*
 * drivers/uart.h — Raw UART driver for ESP32 DevKit V4 (Arduino-ESP32 HardwareSerial).
 *
 * UART allocation (mirrors config/pinout.h):
 *   UART_NUM_0 (Serial)  — debug console, RESERVED, rejected for K-Line
 *   UART_NUM_1 (Serial1) — K-Line ISO 9141-2 via PC817 optocoupler (GPIO17 TX, GPIO16 RX)
 *   UART_NUM_2 (Serial2) — UART recovery, 115200 baud
 *
 * Layering: APP -> PROTOCOL (kline) -> uart_* (raw UART) -> HardwareSerial
 *
 * The 5-baud K-Line init sequence (~200 ms start bit) is bit-banged in
 * drivers/kline.c; this driver only carries the 10400 baud data phase and the
 * 115200 recovery console.
 */

#ifndef DRIVERS_UART_H
#define DRIVERS_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    UART_OK                    = 0,
    UART_ERR_INVALID_ARG       = -1,
    UART_ERR_NOT_INITIALIZED   = -2,
    UART_ERR_BUSY              = -3,
    UART_ERR_TIMEOUT           = -4,
    UART_ERR_PORT_RESERVED     = -5,   /* UART_NUM_0 is the debug console */
    UART_ERR_WRITE_FAILED      = -6,
    UART_ERR_READ_FAILED       = -7,
} uart_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef enum {
    UART_PORT_0 = 0,   /* Serial   — debug console (reserved) */
    UART_PORT_1 = 1,   /* Serial1  — K-Line */
    UART_PORT_2 = 2,   /* Serial2  — recovery console */
} uart_port_t;

typedef struct {
    uart_port_t port;          /* UART_NUM_0/1/2 */
    int         tx_gpio;       /* TX pin */
    int         rx_gpio;       /* RX pin */
    uint32_t    baud;          /* 10400 for K-Line data phase, 115200 for recovery */
    bool        invert_tx;     /* true: invert TX line (PC817) */
    bool        invert_rx;     /* true: invert RX line (PC817) */
    uint32_t    rx_timeout_ms; /* default read timeout */
} uart_config_t;

typedef struct uart uart_t;    /* opaque handle */

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize a UART port. Rejects UART_PORT_0 (debug console) for K-Line
 * use — callers must pass UART_PORT_1 or UART_PORT_2. Returns UART_OK or
 * UART_ERR_*. */
uart_status_t uart_init(const uart_config_t *cfg, uart_t **out_uart);

/* Release a UART handle. */
uart_status_t uart_deinit(uart_t *uart);

/* =========================================================================
 * I/O
 * ========================================================================= */

/* Write bytes. Returns UART_OK on success. tx_len bytes are written; the
 * call blocks until all bytes are queued to the hardware TX FIFO. */
uart_status_t uart_write(uart_t *uart, const uint8_t *buf, size_t len,
                         uint32_t timeout_ms);

/* Read up to len bytes. Returns UART_OK with at least one byte available
 * before rx_timeout_ms elapses; *out_len set to bytes actually read.
 * Returns UART_ERR_TIMEOUT if nothing arrives within the timeout. */
uart_status_t uart_read(uart_t *uart, uint8_t *buf, size_t len, size_t *out_len,
                        uint32_t timeout_ms);

/* Block until the TX FIFO drains. */
uart_status_t uart_flush(uart_t *uart, uint32_t timeout_ms);

/* Change baud rate at runtime (K-Line data phase uses 10400). */
uart_status_t uart_set_baud(uart_t *uart, uint32_t baud);

/* Number of bytes currently available in the RX buffer. */
uart_status_t uart_available(uart_t *uart, size_t *count);

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* True if port is UART_PORT_0 (reserved debug console). */
bool uart_port_is_reserved(uart_port_t port);

/* The configured baud rate of a handle. */
uint32_t uart_baud_get(const uart_t *uart);

/* True if the handle was successfully initialized. */
bool uart_is_initialized(const uart_t *uart);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_UART_H */