/*
 * protocol/obd1/aldl.h — GM ALDL (Assembly Line Diagnostic Link) OBD1.
 *
 * GM's pre-OBD2 serial diagnostic link, used on 1980s–1990s GM vehicles.
 * Runs at 8192 baud (some early implementations at 160 baud) over a single
-wire UART. The ECU typically streams data frames continuously; a scan tool
 * can also request specific data blocks by address.
 *
 * Frame format (typical ALDL stream mode):
 *   [0xFF sync][address][length][data...][checksum]
 *
 * Layering: APP -> aldl_* -> uart_* (8192 baud) -> HardwareSerial
 */

#ifndef PROTOCOL_OBD1_ALDL_H
#define PROTOCOL_OBD1_ALDL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "drivers/uart.h"   /* uart_t, uart_status_t, uart_port_t */
#include "protocol/dtc.h"   /* dtc_list_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    ALDL_OK                    = 0,
    ALDL_ERR_INVALID_ARG       = -1,
    ALDL_ERR_NOT_INITIALIZED   = -2,
    ALDL_ERR_TIMEOUT           = -3,
    ALDL_ERR_PROTOCOL          = -4,   /* bad sync byte / checksum mismatch */
    ALDL_ERR_BUFFER_TOO_SMALL  = -5,
    ALDL_ERR_NO_DATA           = -6,   /* no complete frame available yet */
} aldl_status_t;

/* =========================================================================
 * ALDL baud rates
 * ========================================================================= */
#define ALDL_BAUD_8192   8192UL   /* standard GM ALDL data rate */
#define ALDL_BAUD_160    160UL    /* early GM ALDL (160 baud) */

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
typedef struct aldl aldl_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    uart_port_t uart_port;   /* UART_PORT_1 or UART_PORT_2 (not PORT_0) */
    int         tx_gpio;     /* ALDL TX pin */
    int         rx_gpio;     /* ALDL RX pin */
    uint32_t    baud;        /* ALDL_BAUD_8192 or ALDL_BAUD_160 */
    bool        invert;      /* true if line driver inverts polarity */
    uint32_t    timeout_ms;  /* frame RX/TX timeout */
} aldl_config_t;

/* Default config: 8192 baud on UART_PORT_1 (shared with K-Line hardware,
 * but ALDL and K-Line are mutually exclusive physical links). */
#define ALDL_CONFIG_DEFAULT                                                  \
    {                                                                        \
        .uart_port   = UART_PORT_1,                                          \
        .tx_gpio     = -1,                                                   \
        .rx_gpio     = -1,                                                   \
        .baud        = ALDL_BAUD_8192,                                       \
        .invert      = false,                                                \
        .timeout_ms  = 5000UL,                                               \
    }

/* =========================================================================
 * ALDL frame
 * ========================================================================= */
typedef struct {
    uint8_t  address;    /* ALDL data block address */
    uint8_t  length;     /* data byte count */
    uint8_t  data[16];   /* payload (max 16 bytes typical) */
    uint8_t  checksum;   /* frame checksum */
    bool     valid;      /* true when checksum verified */
} aldl_frame_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the ALDL link at the configured baud rate. */
aldl_status_t aldl_init(const aldl_config_t *cfg, aldl_t **out_aldl);

/* Release the ALDL handle and underlying UART. Safe to call with NULL. */
aldl_status_t aldl_deinit(aldl_t *aldl);

/* True if the handle was successfully initialized. */
bool aldl_is_initialized(const aldl_t *aldl);

/* =========================================================================
 * Frame I/O
 * ========================================================================= */

/*
 * Receive one ALDL frame. Returns ALDL_OK with out_frame filled on success,
 * ALDL_ERR_NO_DATA when no complete frame is available yet (call again),
 * ALDL_ERR_TIMEOUT on deadline, ALDL_ERR_PROTOCOL on bad sync/checksum.
 */
aldl_status_t aldl_receive_frame(aldl_t *aldl, aldl_frame_t *out_frame,
                                 uint32_t timeout_ms);

/*
 * Send a request frame (request/response mode). address selects the data
 * block; data/data_len are optional request parameters.
 */
aldl_status_t aldl_send_request(aldl_t *aldl, uint8_t address,
                                const uint8_t *data, size_t data_len,
                                uint32_t timeout_ms);

/* =========================================================================
 * DTC helpers (ALDL DTCs are typically embedded in stream frames)
 * ========================================================================= */

/* Scan a received frame's data bytes for ALDL DTC codes (2 bytes each,
 * MSB-first, where bit 15 of the first byte = 1 indicates a valid code).
 * Decodes each code via dtc_decode() and appends to the caller-allocated
 * dtc_list_t. */
aldl_status_t aldl_extract_dtcs(const aldl_frame_t *frame,
                                dtc_list_t *list);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_OBD1_ALDL_H */