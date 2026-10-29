/*
 * drivers/kline.c — K-Line / ISO 9141-2 driver for ESP32 DevKit V4.
 *
 * Physical: UART1 GPIO17 TX / GPIO16 RX via PC817 optocoupler (inverting).
 *
 * Fixes applied vs earlier draft:
 *   - kline_init() rejects UART_PORT_0 (debug console).
 *   - kline_wakeup_detect() accepts ONLY low pulses in the ISO 9141-2
 *     200..400 ms window (was: any pulse >= 300 ms, no upper bound).
 *   - 5-baud init bit-banged on raw GPIO (HardwareSerial can't do 5 baud).
 *   - PC817 inversion handled in tx_set/rx_is_low helpers.
 *
 * Toolchain note: C++ (Arduino digitalWrite/digitalRead). Compile as C++.
 */

#include "kline.h"

#include <Arduino.h>

/* ------------------------------------------------------------------------
 * Internal handle & phase
 * ------------------------------------------------------------------------ */
typedef enum {
    KLINE_PHASE_IDLE,      /* kline_init done, GPIO only */
    KLINE_PHASE_DATA,      /* kline_begin done, UART at 10400 baud */
} kline_phase_t;

struct kline {
    kline_config_t cfg;
    uart_t        *uart;        /* NULL until kline_begin() switches to data phase */
    kline_phase_t  phase;
    bool           initialized;
};

/* ------------------------------------------------------------------------
 * PC817 inversion helpers
 * ------------------------------------------------------------------------ */

/* Drive K-Line TX to desired K-Line level (true = K-Line LOW/active).
 * With PC817 inversion: GPIO HIGH -> K-Line LOW, GPIO LOW -> K-Line HIGH. */
static void kline_tx_set(kline_t *kline, bool kline_low)
{
    uint8_t gpio_level;
    if (kline->cfg.invert_logic) {
        gpio_level = kline_low ? HIGH : LOW;
    } else {
        gpio_level = kline_low ? LOW : HIGH;
    }
    digitalWrite(kline->cfg.tx_gpio, gpio_level);
}

/* Read K-Line RX. Returns true if K-Line is LOW (active).
 * With PC817 inversion: GPIO HIGH -> K-Line LOW. */
static bool kline_rx_is_low(kline_t *kline)
{
    uint8_t gpio_level = digitalRead(kline->cfg.rx_gpio);
    if (kline->cfg.invert_logic) {
        return gpio_level == HIGH;
    }
    return gpio_level == LOW;
}

/* ------------------------------------------------------------------------
 * 5-baud bit-bang init (~200 ms per bit)
 * ------------------------------------------------------------------------ */

/* Send one byte at 5 baud, LSB first, K-Line levels:
 *   start bit = K-Line LOW, data bit 1 = K-Line HIGH, data bit 0 = K-Line LOW,
 *   stop bit = K-Line HIGH. */
static kline_status_t kline_send_5baud(kline_t *kline, uint8_t byte)
{
    uint32_t bit_ms = kline->cfg.init_bit_ms;   /* ~200 ms */

    /* Start bit: K-Line LOW */
    kline_tx_set(kline, true);
    delay(bit_ms);

    /* 8 data bits, LSB first */
    for (int i = 0; i < 8; i++) {
        bool bit = (byte >> i) & 1U;   /* 1 = K-Line HIGH, 0 = K-Line LOW */
        kline_tx_set(kline, !bit);
        delay(bit_ms);
    }

    /* Stop bit: K-Line HIGH */
    kline_tx_set(kline, false);
    delay(bit_ms);

    return KLINE_OK;
}

/* Receive one byte at 5 baud, LSB first. Samples each bit in the middle. */
static kline_status_t kline_recv_5baud(kline_t *kline, uint8_t *byte_out)
{
    uint32_t bit_ms = kline->cfg.init_bit_ms;
    uint32_t start;

    /* Wait for start bit: K-Line goes LOW (RX goes HIGH with inversion). */
    start = (uint32_t)millis();
    while (!kline_rx_is_low(kline)) {
        if ((int32_t)((int32_t)(uint32_t)millis() - (int32_t)start) > 1000) {
            return KLINE_ERR_TIMEOUT;
        }
    }

    /* Middle of start bit — verify still LOW. */
    delay(bit_ms / 2);
    if (!kline_rx_is_low(kline)) {
        return KLINE_ERR_INIT_FAILED;
    }

    /* 8 data bits, LSB first, sample in middle of each. */
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        delay(bit_ms);
        bool kline_low = kline_rx_is_low(kline);
        bool bit = !kline_low;   /* K-Line HIGH = 1, LOW = 0 */
        byte |= (uint8_t)(bit << i);
    }

    /* Stop bit: wait to middle, verify K-Line HIGH (RX LOW with inversion). */
    delay(bit_ms);
    if (kline_rx_is_low(kline)) {
        return KLINE_ERR_INIT_FAILED;
    }

    *byte_out = byte;
    return KLINE_OK;
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
kline_status_t kline_init(const kline_config_t *cfg, kline_t **out_kline)
{
    if (cfg == NULL || out_kline == NULL) {
        return KLINE_ERR_INVALID_ARG;
    }
    if (cfg->uart_port < UART_PORT_0 || cfg->uart_port > UART_PORT_2) {
        return KLINE_ERR_INVALID_ARG;
    }
    if (cfg->tx_gpio < 0 || cfg->tx_gpio > 39 ||
        cfg->rx_gpio < 0 || cfg->rx_gpio > 39) {
        return KLINE_ERR_INVALID_ARG;
    }
    if (cfg->baud_data == 0 || cfg->baud_data > 500000UL) {
        return KLINE_ERR_INVALID_ARG;
    }
    if (cfg->wakeup_pulse_ms < 50 || cfg->wakeup_pulse_ms > 1000) {
        return KLINE_ERR_INVALID_ARG;
    }
    if (cfg->init_bit_ms < 50 || cfg->init_bit_ms > 1000) {
        return KLINE_ERR_INVALID_ARG;
    }
    if (cfg->timeout_ms == 0) {
        return KLINE_ERR_INVALID_ARG;
    }

    /* UART_PORT_0 is the debug console — never hand it to K-Line. */
    if (cfg->uart_port == UART_PORT_0) {
        return KLINE_ERR_PORT_RESERVED;
    }

    kline_t *kline = (kline_t *)calloc(1, sizeof(*kline));
    if (kline == NULL) {
        return KLINE_ERR_INVALID_ARG;
    }

    kline->cfg        = *cfg;
    kline->uart       = NULL;
    kline->phase      = KLINE_PHASE_IDLE;
    kline->initialized = true;

    /* Set up raw GPIO for wake-up + 5-baud init (HardwareSerial not yet used). */
    pinMode(cfg->tx_gpio, OUTPUT);
    pinMode(cfg->rx_gpio, INPUT);
    /* Idle: K-Line HIGH (released). With inversion: GPIO LOW. */
    kline_tx_set(kline, false);   /* K-Line HIGH / idle */

    *out_kline = kline;
    return KLINE_OK;
}

kline_status_t kline_deinit(kline_t *kline)
{
    if (kline == NULL) {
        return KLINE_ERR_INVALID_ARG;
    }
    if (kline->uart != NULL) {
        uart_deinit(kline->uart);
        kline->uart = NULL;
    }
    kline->phase = KLINE_PHASE_IDLE;
    kline->initialized = false;
    free(kline);
    return KLINE_OK;
}

/* ------------------------------------------------------------------------
 * Link bring-up (ISO 9141-2 initialization)
 * ------------------------------------------------------------------------ */
kline_status_t kline_wakeup(kline_t *kline)
{
    if (kline == NULL || !kline->initialized) {
        return KLINE_ERR_NOT_INITIALIZED;
    }

    /* Drive K-Line LOW for wakeup_pulse_ms (ISO 9141-2: 200..400 ms). */
    kline_tx_set(kline, true);    /* K-Line LOW */
    delay(kline->cfg.wakeup_pulse_ms);

    /* Release to HIGH (idle). */
    kline_tx_set(kline, false);   /* K-Line HIGH */

    /* Give the ECU ~400 ms to settle before the 5-baud init. */
    delay(400);

    return KLINE_OK;
}

kline_status_t kline_wakeup_detect(kline_t *kline, uint32_t timeout_ms)
{
    if (kline == NULL || !kline->initialized) {
        return KLINE_ERR_NOT_INITIALIZED;
    }

    uint32_t start = (uint32_t)millis();

    /* Wait for K-Line to go LOW (wake-up pulse from ECU). */
    while (!kline_rx_is_low(kline)) {
        if ((int32_t)((int32_t)(uint32_t)millis() - (int32_t)start) >
            (int32_t)timeout_ms) {
            return KLINE_ERR_TIMEOUT;
        }
    }

    /* Measure the LOW pulse duration. ISO 9141-2 window: 200..400 ms. */
    uint32_t pulse_start = (uint32_t)millis();
    while (kline_rx_is_low(kline)) {
        uint32_t elapsed = (uint32_t)millis() - pulse_start;
        if (elapsed > 600) {   /* beyond ISO window — not a valid wake-up */
            return KLINE_ERR_WAKEUP_FAILED;
        }
    }
    uint32_t pulse_ms = (uint32_t)millis() - pulse_start;

    if (pulse_ms >= 200 && pulse_ms <= 400) {
        return KLINE_OK;
    }
    return KLINE_ERR_WAKEUP_FAILED;
}

kline_status_t kline_begin(kline_t *kline)
{
    if (kline == NULL || !kline->initialized) {
        return KLINE_ERR_NOT_INITIALIZED;
    }
    if (kline->phase == KLINE_PHASE_DATA) {
        return KLINE_OK;   /* already begun */
    }

    kline_status_t st;
    uint8_t        byte;

    /* 1. Wake-up pulse (K-Line LOW 200..400 ms, then idle ~400 ms). */
    st = kline_wakeup(kline);
    if (st != KLINE_OK) {
        return st;
    }

    /* 2. Send 0x33 at 5 baud. */
    st = kline_send_5baud(kline, 0x33);
    if (st != KLINE_OK) {
        return KLINE_ERR_INIT_FAILED;
    }

    /* 3. Read ECU 0x55 reply at 5 baud. */
    st = kline_recv_5baud(kline, &byte);
    if (st != KLINE_OK) {
        return KLINE_ERR_INIT_FAILED;
    }
    if (byte != 0x55) {
        return KLINE_ERR_INIT_FAILED;
    }

    /* 4. Switch to 10400 baud data phase via UART (HardwareSerial). */
    uart_config_t uart_cfg = {
        .port        = kline->cfg.uart_port,
        .tx_gpio     = kline->cfg.tx_gpio,
        .rx_gpio     = kline->cfg.rx_gpio,
        .baud        = kline->cfg.baud_data,   /* 10400 */
        .invert_tx   = kline->cfg.invert_logic,
        .invert_rx   = kline->cfg.invert_logic,
        .rx_timeout_ms = kline->cfg.timeout_ms,
    };
    st = uart_init(&uart_cfg, &kline->uart);
    if (st != UART_OK) {
        return KLINE_ERR_INIT_FAILED;
    }

    kline->phase = KLINE_PHASE_DATA;
    return KLINE_OK;
}

/* ------------------------------------------------------------------------
 * Data phase (10400 baud, 8N1)
 * ------------------------------------------------------------------------ */
kline_status_t kline_send(kline_t *kline, const uint8_t *buf, size_t len,
                          uint32_t timeout_ms)
{
    if (kline == NULL || !kline->initialized) {
        return KLINE_ERR_NOT_INITIALIZED;
    }
    if (kline->phase != KLINE_PHASE_DATA || kline->uart == NULL) {
        return KLINE_ERR_LINK_DOWN;
    }
    if (buf == NULL || len == 0) {
        return KLINE_ERR_INVALID_ARG;
    }
    (void)timeout_ms;
    return uart_write(kline->uart, buf, len, timeout_ms) == UART_OK
               ? KLINE_OK : KLINE_ERR_WRITE_FAILED;
}

kline_status_t kline_receive(kline_t *kline, uint8_t *buf, size_t len,
                             size_t *out_len, uint32_t timeout_ms)
{
    if (kline == NULL || !kline->initialized) {
        return KLINE_ERR_NOT_INITIALIZED;
    }
    if (kline->phase != KLINE_PHASE_DATA || kline->uart == NULL) {
        return KLINE_ERR_LINK_DOWN;
    }
    if (buf == NULL || len == 0 || out_len == NULL) {
        return KLINE_ERR_INVALID_ARG;
    }
    uart_status_t st = uart_read(kline->uart, buf, len, out_len, timeout_ms);
    if (st == UART_ERR_TIMEOUT) return KLINE_ERR_TIMEOUT;
    return (st == UART_OK) ? KLINE_OK : KLINE_ERR_READ_FAILED;
}

kline_status_t kline_transfer(kline_t *kline, const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_len, size_t *out_len,
                              uint32_t timeout_ms)
{
    kline_status_t st = kline_send(kline, tx, tx_len, timeout_ms);
    if (st != KLINE_OK) return st;
    return kline_receive(kline, rx, rx_len, out_len, timeout_ms);
}

/* ------------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------------ */
uint32_t kline_baud_get(const kline_t *kline)
{
    return (kline != NULL) ? kline->cfg.baud_data : 0;
}

bool kline_is_initialized(const kline_t *kline)
{
    return (kline != NULL && kline->initialized);
}

bool kline_port_is_reserved(uart_port_t port)
{
    return port == UART_PORT_0;
}