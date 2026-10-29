/*
 * drivers/uart.c — Raw UART driver for ESP32 DevKit V4 (Arduino-ESP32).
 *
 * Backend: <HardwareSerial.h> — Serial (UART0), Serial1 (UART1), Serial2 (UART2).
 * Toolchain note: C++ (Arduino). Compile as C++ (.cpp or -x c++).
 *
 * UART_PORT_0 (Serial) is the debug console and is REJECTED for K-Line use.
 * UART_PORT_1 (Serial1) — K-Line data phase (GPIO17 TX, GPIO16 RX, 10400 baud)
 * UART_PORT_2 (Serial2) — Recovery console (115200 baud)
 *
 * K-Line 5-baud init and PC817 inversion handling live in drivers/kline.c;
 * this layer carries raw bytes at the configured baud rate.
 */

#include "uart.h"

#include <Arduino.h>
#include <HardwareSerial.h>

#include "config/pinout.h"

/* ------------------------------------------------------------------------
 * Handle table (one per UART port)
 * ------------------------------------------------------------------------ */
#define UART_MAX_PORTS 3

typedef struct {
    uart_port_t port;
    HardwareSerial *serial;   /* &Serial, &Serial1, or &Serial2 */
    int         tx_gpio;      /* kept so set_baud() can re-apply pins */
    int         rx_gpio;
    bool         initialized;
    uint32_t     baud;
    bool         invert_tx;
    bool         invert_rx;
    uint32_t     rx_timeout_ms;
} uart_handle_t;

static uart_handle_t g_handles[UART_MAX_PORTS];

static uart_handle_t *handle_for_port(uart_port_t port)
{
    if (port < UART_PORT_0 || port > UART_PORT_2) {
        return NULL;
    }
    return &g_handles[port];
}

static HardwareSerial *serial_for_port(uart_port_t port)
{
    switch (port) {
        case UART_PORT_0: return &Serial;
        case UART_PORT_1: return &Serial1;
        case UART_PORT_2: return &Serial2;
        default:          return NULL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
uart_status_t uart_init(const uart_config_t *cfg, uart_t **out_uart)
{
    if (cfg == NULL || out_uart == NULL) {
        return UART_ERR_INVALID_ARG;
    }
    if (cfg->port < UART_PORT_0 || cfg->port > UART_PORT_2) {
        return UART_ERR_INVALID_ARG;
    }
    if (cfg->tx_gpio < 0 || cfg->tx_gpio > 39 ||
        cfg->rx_gpio < 0 || cfg->rx_gpio > 39) {
        return UART_ERR_INVALID_ARG;
    }
    if (cfg->baud == 0 || cfg->baud > 5000000UL) {
        return UART_ERR_INVALID_ARG;
    }

    /* UART_PORT_0 is the debug console — never hand it to K-Line callers. */
    if (cfg->port == UART_PORT_0) {
        return UART_ERR_PORT_RESERVED;
    }

    uart_handle_t *h = handle_for_port(cfg->port);
    if (h == NULL || h->initialized) {
        return UART_ERR_BUSY;
    }

    HardwareSerial *serial = serial_for_port(cfg->port);
    if (serial == NULL) {
        return UART_ERR_INVALID_ARG;
    }

    /* HardwareSerial::begin(baud, config, rx_pin, tx_pin) on ESP32 Arduino.
     * SERIAL_8N1 is the default config. */
    serial->begin(cfg->baud, SERIAL_8N1, cfg->rx_gpio, cfg->tx_gpio);

    h->port            = cfg->port;
    h->serial          = serial;
    h->tx_gpio         = cfg->tx_gpio;
    h->rx_gpio         = cfg->rx_gpio;
    h->initialized     = true;
    h->baud            = cfg->baud;
    h->invert_tx       = cfg->invert_tx;
    h->invert_rx       = cfg->invert_rx;
    h->rx_timeout_ms   = cfg->rx_timeout_ms;

    if (out_uart != NULL) {
        *out_uart = (uart_t *)h;
    }
    return UART_OK;
}

uart_status_t uart_deinit(uart_t *uart)
{
    uart_handle_t *h = (uart_handle_t *)uart;
    if (h == NULL) {
        return UART_ERR_INVALID_ARG;
    }
    if (h->serial != NULL) {
        h->serial->end();
    }
    h->initialized = false;
    h->serial      = NULL;
    return UART_OK;
}

/* ------------------------------------------------------------------------
 * I/O
 * ------------------------------------------------------------------------ */
uart_status_t uart_write(uart_t *uart, const uint8_t *buf, size_t len,
                         uint32_t timeout_ms)
{
    uart_handle_t *h = (uart_handle_t *)uart;
    if (h == NULL || !h->initialized || h->serial == NULL) {
        return UART_ERR_NOT_INITIALIZED;
    }
    if (buf == NULL || len == 0) {
        return UART_ERR_INVALID_ARG;
    }

    size_t written = h->serial->write(buf, len);
    if (written != len) {
        return UART_ERR_WRITE_FAILED;
    }
    h->serial->flush();   /* wait until TX FIFO drains */
    (void)timeout_ms;     /* blocking write; timeout not enforced at this layer */
    return UART_OK;
}

uart_status_t uart_read(uart_t *uart, uint8_t *buf, size_t len, size_t *out_len,
                        uint32_t timeout_ms)
{
    uart_handle_t *h = (uart_handle_t *)uart;
    if (h == NULL || !h->initialized || h->serial == NULL) {
        return UART_ERR_NOT_INITIALIZED;
    }
    if (buf == NULL || len == 0 || out_len == NULL) {
        return UART_ERR_INVALID_ARG;
    }

    uint32_t start = (uint32_t)millis();
    size_t   got   = 0;

    while (got < len) {
        while (h->serial->available() > 0 && got < len) {
            int c = h->serial->read();
            if (c < 0) {
                break;
            }
            buf[got++] = (uint8_t)c;
        }
        if (got > 0) {
            break;   /* at least one byte — satisfy partial reads */
        }
        if ((int32_t)((int32_t)(uint32_t)millis() - (int32_t)start) >=
            (int32_t)timeout_ms) {
            break;
        }
        delay(1);
    }

    *out_len = got;
    return (got > 0) ? UART_OK : UART_ERR_TIMEOUT;
}

uart_status_t uart_flush(uart_t *uart, uint32_t timeout_ms)
{
    uart_handle_t *h = (uart_handle_t *)uart;
    if (h == NULL || !h->initialized || h->serial == NULL) {
        return UART_ERR_NOT_INITIALIZED;
    }
    h->serial->flush();
    (void)timeout_ms;
    return UART_OK;
}

uart_status_t uart_set_baud(uart_t *uart, uint32_t baud)
{
    uart_handle_t *h = (uart_handle_t *)uart;
    if (h == NULL || !h->initialized || h->serial == NULL) {
        return UART_ERR_NOT_INITIALIZED;
    }
    if (baud == 0 || baud > 5000000UL) {
        return UART_ERR_INVALID_ARG;
    }
    h->serial->begin(baud, SERIAL_8N1, h->rx_gpio, h->tx_gpio);
    h->baud = baud;
    return UART_OK;
}

uart_status_t uart_available(uart_t *uart, size_t *count)
{
    uart_handle_t *h = (uart_handle_t *)uart;
    if (h == NULL || !h->initialized || h->serial == NULL) {
        return UART_ERR_NOT_INITIALIZED;
    }
    if (count == NULL) {
        return UART_ERR_INVALID_ARG;
    }
    *count = (size_t)h->serial->available();
    return UART_OK;
}

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */
bool uart_port_is_reserved(uart_port_t port)
{
    return port == UART_PORT_0;
}

uint32_t uart_baud_get(const uart_t *uart)
{
    const uart_handle_t *h = (const uart_handle_t *)uart;
    return (h != NULL) ? h->baud : 0;
}

bool uart_is_initialized(const uart_t *uart)
{
    const uart_handle_t *h = (const uart_handle_t *)uart;
    return (h != NULL && h->initialized);
}