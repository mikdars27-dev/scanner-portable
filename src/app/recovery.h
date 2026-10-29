/*
 * app/recovery.h — Application-layer UART recovery console.
 *
 * Recovery console on UART_PORT_2 (Serial2, 115200 baud) used for ESP32
 * bootloader recovery and ECU reflash over serial. recovery_enter() bridges
 * bytes between the recovery UART and the debug Serial until timeout_ms
 * elapses or recovery_exit() is requested from another context (another
 * core/task or the UI menu).
 *
 * Layering: APP -> recovery_* -> uart_* -> HardwareSerial (Serial2 / UART2)
 *             recovery_* -> Serial          (debug console bridge)
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_RECOVERY_H
#define APP_RECOVERY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "drivers/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Recovery console GPIO pins (ESP32 DevKit V4 UART2 defaults). Override by
 * defining these before including this header if your wiring differs. */
#ifndef RECOVERY_TX_GPIO
#define RECOVERY_TX_GPIO 17
#endif
#ifndef RECOVERY_RX_GPIO
#define RECOVERY_RX_GPIO 16
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    RECOVERY_OK                    = 0,
    RECOVERY_ERR_INVALID_ARG       = -1,
    RECOVERY_ERR_NOT_INITIALIZED   = -2,
    RECOVERY_ERR_UART              = -3,   /* underlying uart_* failure */
    RECOVERY_ERR_TIMEOUT           = -4,   /* console deadline expired */
} recovery_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

/* The real struct is defined only in app/recovery.c. recovery_* is a
 * singleton (no handle is passed across the API), so the handle is internal;
 * the typedef keeps the layering consistent with the other app modules. */
typedef struct recovery recovery_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    scanner_uart_port_t port;      /* UART_PORT_2 */
    int         tx_gpio;   /* recovery UART TX pin */
    int         rx_gpio;   /* recovery UART RX pin */
    uint32_t    baud;      /* 115200 */
    bool        invert_tx; /* false */
    bool        invert_rx; /* false */
    uint32_t    rx_timeout_ms; /* per-read timeout, ms */
} recovery_config_t;

/* Built-in defaults used by recovery_init(). Designated initializers. */
#define RECOVERY_CONFIG_DEFAULT { \
    .port         = UART_PORT_2, \
    .tx_gpio      = RECOVERY_TX_GPIO, \
    .rx_gpio      = RECOVERY_RX_GPIO, \
    .baud         = 115200UL, \
    .invert_tx    = false, \
    .invert_rx    = false, \
    .rx_timeout_ms = 100UL, \
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the recovery console on UART_PORT_2 (Serial2, 115200 baud)
 * using RECOVERY_CONFIG_DEFAULT. Idempotent: returns RECOVERY_OK when the
 * console is already initialized. */
recovery_status_t recovery_init(void);

/* Release the recovery console UART. */
recovery_status_t recovery_deinit(void);

/* True if recovery_init() completed successfully. */
bool recovery_is_initialized(void);

/* =========================================================================
 * Recovery console
 * ========================================================================= */

/* Enter the recovery console loop. Bridges bytes between the recovery UART
 * (UART_PORT_2) and the debug Serial until timeout_ms elapses or
 * recovery_exit() is called from another context.
 *
 * Returns RECOVERY_OK when recovery_exit() was requested,
 * RECOVERY_ERR_TIMEOUT when the deadline expires without an exit request,
 * or RECOVERY_ERR_UART on an underlying uart_* failure. */
recovery_status_t recovery_enter(uint32_t timeout_ms);

/* Request the recovery console loop to end; recovery_enter() returns
 * RECOVERY_OK on its next poll. Safe to call from another context. */
recovery_status_t recovery_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_RECOVERY_H */
