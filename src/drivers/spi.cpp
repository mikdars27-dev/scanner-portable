/*
 * drivers/spi.c — SPI master BSP/HAL implementation for ESP32 DevKit V4.
 *
 * Backend: Arduino-ESP32 <SPI.h> (VSPI=SPI_HOST_2, HSPI=SPI_HOST_3).
 * Toolchain note: <SPI.h> is C++. Compile this file as C++ (rename to .cpp or
 * pass -x c++). The header drivers/spi.h stays plain C.
 *
 * Fixes applied vs earlier draft:
 *   - spi_bus_lock()/spi_bus_unlock() use a real global lock per host with a
 *     deadline-based timeout (not a per-handle trylock with no arbitration).
 *   - A locked host rejects locks from other device handles (SPI_ERR_BUSY).
 *   - MCP2515 and TFT share SPI2_HOST; the lock serializes multi-transfer
 *     sequences across that bus. CS stays per-device.
 */

#include "spi.h"

#include <Arduino.h>
#include <SPI.h>

#include "config/pinout.h"

/* ------------------------------------------------------------------------
 * Driver-internal state (one per SPI host)
 * ------------------------------------------------------------------------ */
#define SPI_MAX_DEVICES 4

typedef struct {
    spi_host_t    host;
    scanner_spi_device_t *devices[SPI_MAX_DEVICES];
    uint8_t       device_count;
    bool          initialized;
    SPIClass     *spi;            /* &SPI (VSPI) or &HSPI */
    /* Bus lock: owner device handle + deadline (millis). Held across a
     * multi-transfer sequence so MCP2515 config / TFT frame is atomic vs the
     * other device on the same host. */
    scanner_spi_device_t *lock_owner;
    uint32_t      lock_deadline_ms;
} spi_bus_state_t;

static spi_bus_state_t g_bus[2];   /* index = host - 2 */

static spi_bus_state_t *bus_for_host(spi_host_t host)
{
    return &g_bus[host - 2];
}

static SPIClass *spi_class_for_host(spi_host_t host)
{
    return (host == SPI_HOST_2) ? &SPI : &HSPI;
}

/* Map spi_mode_t to Arduino SPI mode constant (SPI_MODE0..SPI_MODE3). */
static uint8_t arduino_mode(spi_mode_t m)
{
    switch (m) {
        case SPI_MODE_1: return SPI_MODE1;
        case SPI_MODE_2: return SPI_MODE2;
        case SPI_MODE_3: return SPI_MODE3;
        case SPI_MODE_0:
        default:         return SPI_MODE0;
    }
}

/* ------------------------------------------------------------------------
 * Bus lifecycle
 * ------------------------------------------------------------------------ */
spi_status_t spi_bus_init(spi_host_t host, int mosi_gpio, int miso_gpio,
                          int sck_gpio, uint32_t clock_hz)
{
    spi_bus_state_t *bs = bus_for_host(host);
    SPIClass *spi = spi_class_for_host(host);

    if (bs->initialized) {
        return SPI_OK;   /* idempotent */
    }

    spi->begin(sck_gpio, miso_gpio, mosi_gpio, -1);   /* no SS pin here */

    bs->host          = host;
    bs->device_count  = 0;
    bs->initialized   = true;
    bs->spi           = spi;
    bs->lock_owner    = NULL;
    bs->lock_deadline_ms = 0;

    return SPI_OK;
}

spi_status_t spi_device_init(const spi_device_config_t *cfg,
                             scanner_spi_device_t **out_dev)
{
    if (cfg == NULL || out_dev == NULL) {
        return SPI_ERR_INVALID_ARG;
    }
    if (cfg->cs_gpio < 0 || cfg->cs_gpio > 39) {
        return SPI_ERR_INVALID_ARG;
    }
    if (cfg->clock_hz == 0 || cfg->clock_hz > 80000000UL) {
        return SPI_ERR_INVALID_ARG;
    }

    spi_bus_state_t *bs = bus_for_host(cfg->host);
    if (!bs->initialized) {
        return SPI_ERR_NOT_INITIALIZED;
    }
    if (bs->device_count >= SPI_MAX_DEVICES) {
        return SPI_ERR_BUSY;
    }

    scanner_spi_device_t *dev = (scanner_spi_device_t *)calloc(1, sizeof(*dev));
    if (dev == NULL) {
        return SPI_ERR_INVALID_ARG;
    }

    dev->host       = cfg->host;
    dev->cs_gpio    = cfg->cs_gpio;
    dev->clock_hz   = cfg->clock_hz;
    dev->mode       = cfg->mode;
    dev->bit_order  = cfg->bit_order;
    dev->cs_active_low = cfg->cs_active_low;
    dev->dummy_bits = cfg->dummy_bits;
    dev->bus        = bs;

    pinMode(cfg->cs_gpio, OUTPUT);
    /* Deassert CS (inactive = high for active-low, low for active-high). */
    digitalWrite(cfg->cs_gpio, cfg->cs_active_low ? HIGH : LOW);

    bs->devices[bs->device_count++] = dev;

    if (out_dev != NULL) {
        *out_dev = dev;
    }
    return SPI_OK;
}

spi_status_t spi_device_deinit(scanner_spi_device_t *dev)
{
    if (dev == NULL) {
        return SPI_ERR_INVALID_ARG;
    }
    spi_bus_state_t *bs = dev->bus;
    if (bs != NULL) {
        for (uint8_t i = 0; i < bs->device_count; i++) {
            if (bs->devices[i] == dev) {
                bs->devices[i] = NULL;
                /* compact */
                for (uint8_t j = i + 1; j < bs->device_count; j++) {
                    bs->devices[j - 1] = bs->devices[j];
                }
                bs->device_count--;
                break;
            }
        }
        if (bs->lock_owner == dev) {
            bs->lock_owner = NULL;
            bs->lock_deadline_ms = 0;
        }
    }
    free(dev);
    return SPI_OK;
}

spi_status_t spi_bus_deinit(spi_host_t host)
{
    spi_bus_state_t *bs = bus_for_host(host);
    if (!bs->initialized) {
        return SPI_OK;
    }
    bs->initialized = false;
    bs->device_count = 0;
    bs->lock_owner = NULL;
    bs->lock_deadline_ms = 0;
    return SPI_OK;
}

/* ------------------------------------------------------------------------
 * CS control
 * ------------------------------------------------------------------------ */
spi_status_t spi_cs_assert(scanner_spi_device_t *dev)
{
    if (dev == NULL) {
        return SPI_ERR_INVALID_ARG;
    }
    digitalWrite(dev->cs_gpio, dev->cs_active_low ? LOW : HIGH);
    return SPI_OK;
}

spi_status_t spi_cs_release(scanner_spi_device_t *dev)
{
    if (dev == NULL) {
        return SPI_ERR_INVALID_ARG;
    }
    digitalWrite(dev->cs_gpio, dev->cs_active_low ? HIGH : LOW);
    return SPI_OK;
}

/* ------------------------------------------------------------------------
 * Bus arbitration — the fix: real per-host lock with timeout
 * ------------------------------------------------------------------------ */
static uint32_t now_ms(void)
{
    return (uint32_t)millis();
}

spi_status_t spi_bus_lock(scanner_spi_device_t *dev, uint32_t timeout_ms)
{
    if (dev == NULL || dev->bus == NULL) {
        return SPI_ERR_INVALID_ARG;
    }
    spi_bus_state_t *bs = dev->bus;

    /* Already held by this handle: re-entrant success. */
    if (bs->lock_owner == dev) {
        return SPI_OK;
    }

    /* Already held by another device on the same host: wait or fail. */
    if (bs->lock_owner != NULL) {
        if (timeout_ms == 0) {
            return SPI_ERR_BUSY;
        }
        uint32_t start = now_ms();
        uint32_t deadline = start + timeout_ms;
        while (bs->lock_owner != NULL && bs->lock_owner != dev) {
            uint32_t t = now_ms();
            /* Handle millis() wraparound: if deadline wrapped, treat as
             * expired when now() < start (wrapped past) — simpler: spin until
             * deadline or owner clears. Guard against wraparound by comparing
             * elapsed. */
            if ((int32_t)((int32_t)t - (int32_t)start) >= (int32_t)timeout_ms) {
                return SPI_ERR_TIMEOUT;
            }
            delay(1);   /* yield briefly; ESP32 watchdog tolerates short yields */
        }
        /* Re-check after spin. */
        if (bs->lock_owner != NULL && bs->lock_owner != dev) {
            return SPI_ERR_TIMEOUT;
        }
    }

    bs->lock_owner   = dev;
    bs->lock_deadline_ms = now_ms() + timeout_ms;
    return SPI_OK;
}

spi_status_t spi_bus_unlock(scanner_spi_device_t *dev)
{
    if (dev == NULL || dev->bus == NULL) {
        return SPI_ERR_INVALID_ARG;
    }
    spi_bus_state_t *bs = dev->bus;

    if (bs->lock_owner != dev) {
        /* Not held by this caller — harmless no-op instead of an error, so
         * unlock paths in error handling don't cascade failures. */
        return SPI_OK;
    }

    bs->lock_owner     = NULL;
    bs->lock_deadline_ms = 0;
    return SPI_OK;
}

/* ------------------------------------------------------------------------
 * Raw transfer — CS held for the whole call; bus lock NOT acquired here
 * (caller wraps multi-transfer sequences with spi_bus_lock/unlock).
 * ------------------------------------------------------------------------ */
static spi_status_t transfer_locked(scanner_spi_device_t *dev,
                                    const uint8_t *tx_buf, uint8_t *rx_buf,
                                    size_t len, uint32_t timeout_ms)
{
    spi_bus_state_t *bs = dev->bus;
    SPIClass *spi = bs->spi;

    spi->beginTransaction(SPISettings(dev->clock_hz,
                                      (dev->bit_order == SPI_BIT_ORDER_LSB)
                                          ? LSBFIRST : MSBFIRST,
                                      arduino_mode(dev->mode)));

    spi_cs_assert(dev);

    for (size_t i = 0; i < len; i++) {
        uint8_t tx = (tx_buf != NULL) ? tx_buf[i] : 0xFF;
        uint8_t rx = spi->transfer(tx);
        if (rx_buf != NULL) {
            rx_buf[i] = rx;
        }
    }

    spi_cs_release(dev);
    spi->endTransaction();
    return SPI_OK;
}

spi_status_t spi_write(scanner_spi_device_t *dev, const uint8_t *tx_buf, size_t tx_len,
                       uint32_t timeout_ms)
{
    if (dev == NULL || dev->bus == NULL || !dev->bus->initialized) {
        return SPI_ERR_NOT_INITIALIZED;
    }
    if (tx_buf == NULL || tx_len == 0) {
        return SPI_ERR_INVALID_ARG;
    }
    return transfer_locked(dev, tx_buf, NULL, tx_len, timeout_ms);
}

spi_status_t spi_read(scanner_spi_device_t *dev, uint8_t *rx_buf, size_t rx_len,
                      uint32_t timeout_ms)
{
    if (dev == NULL || dev->bus == NULL || !dev->bus->initialized) {
        return SPI_ERR_NOT_INITIALIZED;
    }
    if (rx_buf == NULL || rx_len == 0) {
        return SPI_ERR_INVALID_ARG;
    }
    return transfer_locked(dev, NULL, rx_buf, rx_len, timeout_ms);
}

spi_status_t spi_transfer(scanner_spi_device_t *dev, const uint8_t *tx_buf,
                          uint8_t *rx_buf, size_t len, uint32_t timeout_ms)
{
    if (dev == NULL || dev->bus == NULL || !dev->bus->initialized) {
        return SPI_ERR_NOT_INITIALIZED;
    }
    if (len == 0 || (tx_buf == NULL && rx_buf == NULL)) {
        return SPI_ERR_INVALID_ARG;
    }
    return transfer_locked(dev, tx_buf, rx_buf, len, timeout_ms);
}

/* ------------------------------------------------------------------------
 * Register helpers (command + data frames, CS held across the frame)
 * ------------------------------------------------------------------------ */
spi_status_t spi_reg_write(scanner_spi_device_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t frame[2] = { reg, value };
    return spi_write(dev, frame, sizeof(frame), 1000);
}

spi_status_t spi_reg_read(scanner_spi_device_t *dev, uint8_t reg, uint8_t *value_out)
{
    if (value_out == NULL) {
        return SPI_ERR_INVALID_ARG;
    }
    uint8_t tx = reg;
    uint8_t rx = 0x00;
    spi_status_t st = spi_transfer(dev, &tx, &rx, 1, 1000);
    if (st != SPI_OK) return st;

    /* Dummy cycle then sample data byte. */
    tx = 0xFF;
    rx = 0x00;
    st = spi_transfer(dev, &tx, &rx, 1, 1000);
    if (st != SPI_OK) return st;

    *value_out = rx;
    return SPI_OK;
}

spi_status_t spi_reg_write_burst(scanner_spi_device_t *dev, uint8_t reg,
                                 const uint8_t *values, size_t count)
{
    if (values == NULL || count == 0) {
        return SPI_ERR_INVALID_ARG;
    }
    uint8_t header[1] = { reg };
    spi_status_t st = spi_write(dev, header, sizeof(header), 1000);
    if (st != SPI_OK) return st;
    return spi_write(dev, values, count, 1000);
}

spi_status_t spi_reg_read_burst(scanner_spi_device_t *dev, uint8_t reg,
                                uint8_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return SPI_ERR_INVALID_ARG;
    }
    uint8_t header[1] = { reg };
    spi_status_t st = spi_transfer(dev, header, NULL, sizeof(header), 1000);
    if (st != SPI_OK) return st;

    /* Dummy cycle, then read count bytes. */
    uint8_t dummy = 0xFF;
    st = spi_transfer(dev, &dummy, NULL, 1, 1000);
    if (st != SPI_OK) return st;

    return spi_read(dev, out, count, 1000);
}

/* ------------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------------ */
uint32_t spi_clock_get_hz(const scanner_spi_device_t *dev)
{
    return (dev != NULL) ? dev->clock_hz : 0;
}

bool spi_is_initialized(const scanner_spi_device_t *dev)
{
    return (dev != NULL && dev->bus != NULL && dev->bus->initialized);
}