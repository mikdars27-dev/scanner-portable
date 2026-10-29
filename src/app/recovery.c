/*
 * app/recovery.c — Application-layer UART recovery console.
 *
 * Implements the app/recovery.h API on top of drivers/uart.h (Serial2 /
 * UART_PORT_2, 115200 baud). The console bridges bytes between the recovery
 * UART and the debug Serial, so a terminal on the recovery UART can drive
 * ESP32 bootloader recovery / ECU reflash while traces stream to Serial.
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "recovery.h"

#include <Arduino.h>

/* ------------------------------------------------------------------------
 * Internal state (singleton; no dynamic allocation)
 * ------------------------------------------------------------------------ */
struct recovery {
    uart_t             *uart;     /* UART_PORT_2 handle, or NULL */
    recovery_config_t   cfg;      /* active configuration */
    bool                initialized;
    volatile bool       exit_requested; /* set by recovery_exit(), polled by recovery_enter() */
};

static recovery_t g_recovery;

/* ------------------------------------------------------------------------
 * Console bridge
 * ------------------------------------------------------------------------ */

/* One poll pass between the recovery UART and the debug Serial.
 * Recovery-UART bytes are echoed back and mirrored to Serial; Serial bytes
 * are forwarded to the recovery UART. Reads use a short fixed chunk so the
 * caller keeps checking the deadline and the exit-request flag.
 *
 * Returns RECOVERY_OK, or RECOVERY_ERR_UART on a uart_* failure. A read
 * timeout is just "no data" and is treated as OK. */
static recovery_status_t recovery_poll_bridge(void)
{
    uint8_t buf[64];
    size_t  len;

    /* Recovery UART -> debug Serial (with local echo on the recovery UART). */
    uart_status_t st = uart_read(g_recovery.uart, buf, sizeof(buf), &len, 10UL);
    if (st == UART_OK && len > 0) {
        if (uart_write(g_recovery.uart, buf, len, 100UL) != UART_OK) {
            return RECOVERY_ERR_UART;
        }
        Serial.write(buf, len);
        Serial.flush();
    } else if (st != UART_OK && st != UART_ERR_TIMEOUT) {
        return RECOVERY_ERR_UART;
    }

    /* Debug Serial -> recovery UART. */
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) {
            break;
        }
        uint8_t b = (uint8_t)c;
        if (uart_write(g_recovery.uart, &b, 1, 100UL) != UART_OK) {
            return RECOVERY_ERR_UART;
        }
    }
    return RECOVERY_OK;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

recovery_status_t recovery_init(void)
{
    if (g_recovery.initialized) {
        return RECOVERY_OK;
    }

    /* Assign fields individually (designated initializers in an assignment
     * are not valid C++; the CONFIG_DEFAULT macro is only for declaration-time
     * initialization). g_recovery is static, so zero fields default correctly. */
    g_recovery.cfg.port         = UART_PORT_2;
    g_recovery.cfg.tx_gpio      = RECOVERY_TX_GPIO;
    g_recovery.cfg.rx_gpio      = RECOVERY_RX_GPIO;
    g_recovery.cfg.baud         = 115200UL;
    g_recovery.cfg.invert_tx    = false;
    g_recovery.cfg.invert_rx    = false;
    g_recovery.cfg.rx_timeout_ms = 100UL;

    uart_config_t ucfg = {
        .port          = g_recovery.cfg.port,
        .tx_gpio       = g_recovery.cfg.tx_gpio,
        .rx_gpio       = g_recovery.cfg.rx_gpio,
        .baud          = g_recovery.cfg.baud,
        .invert_tx     = g_recovery.cfg.invert_tx,
        .invert_rx     = g_recovery.cfg.invert_rx,
        .rx_timeout_ms = g_recovery.cfg.rx_timeout_ms,
    };

    uart_status_t st = uart_init(&ucfg, &g_recovery.uart);
    if (st != UART_OK) {
        g_recovery.uart = NULL;
        return RECOVERY_ERR_UART;
    }

    g_recovery.initialized   = true;
    g_recovery.exit_requested = false;
    return RECOVERY_OK;
}

recovery_status_t recovery_deinit(void)
{
    if (!g_recovery.initialized || g_recovery.uart == NULL) {
        return RECOVERY_ERR_NOT_INITIALIZED;
    }

    uart_status_t st = uart_deinit(g_recovery.uart);
    g_recovery.uart         = NULL;
    g_recovery.initialized  = false;
    g_recovery.exit_requested = false;

    return (st == UART_OK) ? RECOVERY_OK : RECOVERY_ERR_UART;
}

bool recovery_is_initialized(void)
{
    return g_recovery.initialized;
}

recovery_status_t recovery_enter(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return RECOVERY_ERR_INVALID_ARG;
    }
    if (!g_recovery.initialized || g_recovery.uart == NULL) {
        return RECOVERY_ERR_NOT_INITIALIZED;
    }

    g_recovery.exit_requested = false;
    uint32_t start = (uint32_t)millis();

    for (;;) {
        if (g_recovery.exit_requested) {
            return RECOVERY_OK;
        }

        /* millis()-safe deadline check (same idiom as drivers/uart.c). */
        uint32_t now = (uint32_t)millis();
        if ((int32_t)((int32_t)now - (int32_t)start) >= (int32_t)timeout_ms) {
            break;
        }

        recovery_status_t st = recovery_poll_bridge();
        if (st != RECOVERY_OK) {
            return st;
        }
    }

    (void)uart_flush(g_recovery.uart, 100UL);
    Serial.flush();

    return g_recovery.exit_requested ? RECOVERY_OK : RECOVERY_ERR_TIMEOUT;
}

recovery_status_t recovery_exit(void)
{
    if (!g_recovery.initialized) {
        return RECOVERY_ERR_NOT_INITIALIZED;
    }
    g_recovery.exit_requested = true;
    return RECOVERY_OK;
}
