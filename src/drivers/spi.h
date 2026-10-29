/*
 * drivers/spi.h — Generic SPI master BSP/HAL for ESP32 DevKit V4, multi-device shared bus.
 *
 * Layering: APP -> PROTOCOL (J1979/KWP2000/UDS/ISO 15765-2) -> DATA LINK -> spi_* (BSP/HAL)
 *
 * Supported devices on the scanner board:
 *   MCP2515 CAN controller — SPI2_HOST (VSPI), CS GPIO 5, clock <= 1 MHz
 *   microSD card            — SPI3_HOST (HSPI), CS GPIO 13, clock 20 MHz
 *   TFT ILI9341             — SPI2_HOST (VSPI), CS GPIO 15, clock 20 MHz
 *
 * MCP2515 and TFT share SPI2_HOST; spi_bus_lock()/spi_bus_unlock() guard
 * multi-transfer sequences across that shared bus. CS is managed per device.
 *
 * Toolchain note: the .c implementation uses <SPI.h>/<Arduino.h> (C++), so it
 * must be compiled as C++ (rename to .cpp or -x c++). This header stays plain C.
 */

#ifndef DRIVERS_SPI_H
#define DRIVERS_SPI_H

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
    SPI_OK                     = 0,   /* Success */
    SPI_ERR_INVALID_ARG        = -1,  /* NULL config/handle, bad GPIO/clock */
    SPI_ERR_NOT_INITIALIZED    = -2,  /* spi_bus_init() not yet completed */
    SPI_ERR_BUSY               = -3,  /* Transfer in progress / bus locked */
    SPI_ERR_TIMEOUT            = -4,  /* Lock or transfer timeout */
    SPI_ERR_TRANSFER_FAILED    = -5,  /* SPI transfer returned error */
    SPI_ERR_DEVICE_BUSY        = -6,  /* Device-specific busy flag */
} spi_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef enum {
    SPI_HOST_2 = 2,   /* VSPI: SCK18/MOSI23/MISO19 — MCP2515 + TFT share this host */
    SPI_HOST_3 = 3,   /* HSPI: microSD CS13 */
} spi_host_t;

typedef enum {
    SPI_MODE_0 = 0,   /* CPOL=0, CPHA=0 */
    SPI_MODE_1 = 1,   /* CPOL=0, CPHA=1 */
    SPI_MODE_2 = 2,   /* CPOL=1, CPHA=0 */
    SPI_MODE_3 = 3,   /* CPOL=1, CPHA=1 */
} spi_mode_t;

typedef enum {
    SPI_BIT_ORDER_MSB = 0,
    SPI_BIT_ORDER_LSB = 1,
} spi_bit_order_t;

typedef struct {
    spi_host_t      host;         /* SPI_HOST_2 or SPI_HOST_3 */
    int             cs_gpio;      /* Device chip-select GPIO */
    uint32_t        clock_hz;     /* SPI clock (MCP2515 <=1 MHz, SD/TFT 20 MHz) */
    spi_mode_t      mode;         /* SPI_MODE_0..3 */
    spi_bit_order_t bit_order;    /* MSB-first or LSB-first */
    bool            cs_active_low;/* true: CS asserted at LOW */
    uint8_t         dummy_bits;   /* dummy cycles after register address (reads) */
} spi_device_config_t;

typedef struct spi_device spi_device_t;   /* opaque handle */

/* =========================================================================
 * Bus lifecycle
 * ========================================================================= */

/* Initialize an SPI host bus (SCK/MOSI/MISO). Call once per host before any
 * device on that bus is used. Returns SPI_OK or SPI_ERR_*. */
spi_status_t spi_bus_init(spi_host_t host, int mosi_gpio, int miso_gpio,
                          int sck_gpio, uint32_t clock_hz);

/* Allocate and configure a device on an already-initialized bus. The device
 * handle owns its CS GPIO; the caller must not reuse that GPIO for another
 * purpose. Returns SPI_OK or SPI_ERR_*. */
spi_status_t spi_device_init(const spi_device_config_t *cfg,
                             spi_device_t **out_dev);

/* Tear down a device (release its handle). CS GPIO is left as-is. */
spi_status_t spi_device_deinit(spi_device_t *dev);

/* Tear down an entire host bus (all devices on it must be deinit'd first). */
spi_status_t spi_bus_deinit(spi_host_t host);

/* =========================================================================
 * Raw transfer
 * ========================================================================= */

/* Write bytes to the device. Holds CS for the whole call. */
spi_status_t spi_write(spi_device_t *dev, const uint8_t *tx_buf, size_t tx_len,
                       uint32_t timeout_ms);

/* Read bytes from the device. Sends 0xFF dummy bytes, samples MISO.
 * Holds CS for the whole call. */
spi_status_t spi_read(spi_device_t *dev, uint8_t *rx_buf, size_t rx_len,
                      uint32_t timeout_ms);

/* Full-duplex transfer. Either tx_buf or rx_buf may be NULL.
 * Holds CS for the whole call. */
spi_status_t spi_transfer(spi_device_t *dev, const uint8_t *tx_buf,
                          uint8_t *rx_buf, size_t len, uint32_t timeout_ms);

/* =========================================================================
 * Register helpers (command + data frames, CS held across the frame)
 * ========================================================================= */

/* Write one register: [reg_addr][value]. */
spi_status_t spi_reg_write(spi_device_t *dev, uint8_t reg, uint8_t value);

/* Read one register: [reg_addr][dummy][value_out]. */
spi_status_t spi_reg_read(spi_device_t *dev, uint8_t reg, uint8_t *value_out);

/* Write a burst: [reg_addr][values[0..count-1]]. */
spi_status_t spi_reg_write_burst(spi_device_t *dev, uint8_t reg,
                                 const uint8_t *values, size_t count);

/* Read a burst: [reg_addr][dummy][out[0..count-1]]. */
spi_status_t spi_reg_read_burst(spi_device_t *dev, uint8_t reg,
                                uint8_t *out, size_t count);

/* =========================================================================
 * CS control (low-level; use spi_write/read/transfer for normal I/O)
 * ========================================================================= */

spi_status_t spi_cs_assert(spi_device_t *dev);
spi_status_t spi_cs_release(spi_device_t *dev);

/* =========================================================================
 * Shared-bus arbitration (MCP2515 + TFT on SPI2_HOST)
 * ========================================================================= */

/* Acquire exclusive access to the SPI host bus shared by multiple devices.
 * Blocks up to timeout_ms. Returns SPI_OK when locked, SPI_ERR_TIMEOUT if
 * the lock could not be acquired, SPI_ERR_BUSY if already locked by this
 * device handle. */
spi_status_t spi_bus_lock(spi_device_t *dev, uint32_t timeout_ms);

/* Release the bus lock. */
spi_status_t spi_bus_unlock(spi_device_t *dev);

/* =========================================================================
 * Accessors
 * ========================================================================= */

uint32_t spi_clock_get_hz(const spi_device_t *dev);
bool     spi_is_initialized(const spi_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_SPI_H */