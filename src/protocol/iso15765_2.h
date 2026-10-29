/*
 * protocol/iso15765_2.h — ISO 15765-2 CAN transport layer (Diagnostic
 * Communication over CAN, DoCAN).
 *
 * Implements Single Frame (SF), First Frame (FF), Consecutive Frame (CF),
 * and Flow Control (FC) per ISO 15765-2:2016. Built on top of
 * drivers/can_mcp2515.c (can_write/can_read/can_available).
 *
 * CAN addressing (ISO 15765-4 / OBD2):
 *   Functional request : 0x7DF (broadcast)
 *   Physical request   : 0x7E0..0x7E7
 *   ECU response       : 0x7E8..0x7EF
 *
 * Layering: APP -> J1979/UDS -> iso15765_2_* -> can_* -> spi_*
 */

#ifndef PROTOCOL_ISO15765_2_H
#define PROTOCOL_ISO15765_2_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "drivers/can_mcp2515.h"   /* can_t, can_frame_t, can_status_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    ISO15765_OK                    = 0,
    ISO15765_ERR_INVALID_ARG       = -1,
    ISO15765_ERR_NOT_INITIALIZED   = -2,
    ISO15765_ERR_TIMEOUT           = -3,
    ISO15765_ERR_BUSY              = -4,
    ISO15765_ERR_PROTOCOL          = -5,   /* unexpected PCI / sequence error */
    ISO15765_ERR_BUFFER_TOO_SMALL  = -6,
    ISO15765_ERR_NO_RESPONSE       = -7,
    ISO15765_ERR_FLOW_CONTROL      = -8,   /* FC overflow / wait timeout */
} iso15765_status_t;

/* =========================================================================
 * PCI (Protocol Control Information) types
 * ========================================================================= */
#define ISO15765_PCI_SF 0x0   /* Single Frame      */
#define ISO15765_PCI_FF 0x1   /* First Frame       */
#define ISO15765_PCI_CF 0x2   /* Consecutive Frame */
#define ISO15765_PCI_FC 0x3   /* Flow Control      */

/* Flow control status values (FC PCI low nibble) */
#define ISO15765_FS_CTS 0x0   /* Continue to Send */
#define ISO15765_FS_WT  0x1   /* Wait             */
#define ISO15765_FS_OVFS 0x2  /* Overflow         */

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct iso15765 iso15765_t;   /* opaque handle */

typedef struct {
    can_t      *can;        /* CAN handle (MCP2515) */
    uint32_t    tx_addr;    /* TX address: 0x7E0..0x7E7 physical, or 0x7DF functional */
    uint32_t    rx_addr;    /* Expected response address: 0x7E8..0x7EF */
    uint32_t    timeout_ms; /* N_As/N_Ar timeout (default 1000 ms) */
    uint8_t     bs;         /* Block size advertised as receiver (0 = continuous) */
    uint8_t     stmin;      /* STmin advertised as receiver (ms, 0..255) */
} iso15765_config_t;

#define ISO15765_CONFIG_DEFAULT(can_handle, tx_addr, rx_addr)              \
    {                                                                      \
        .can        = (can_handle),                                        \
        .tx_addr    = (tx_addr),                                           \
        .rx_addr    = (rx_addr),                                           \
        .timeout_ms = 1000UL,                                              \
        .bs         = 0,                                                   \
        .stmin      = 0,                                                   \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize ISO 15765-2 transport over a CAN handle. */
iso15765_status_t iso15765_init(const iso15765_config_t *cfg,
                                iso15765_t **out_iso);

/* Release the transport handle. */
iso15765_status_t iso15765_deinit(iso15765_t *iso);

/* True if the handle was successfully initialized. */
bool iso15765_is_initialized(const iso15765_t *iso);

/* Set flow control parameters advertised when this node is the receiver. */
iso15765_status_t iso15765_set_flow_control(iso15765_t *iso,
                                            uint8_t bs, uint8_t stmin);

/* =========================================================================
 * Send / receive (blocking request-response)
 * ========================================================================= */

/*
 * Send a diagnostic request and wait for the response.
 *
 * Handles SF (<=7 bytes) and FF+CF (>7 bytes) transmission, plus FC
 * processing when the ECU sends flow control. On receive, handles SF and
 * FF+CF reception, sending FC frames as needed.
 *
 * tx        : request payload (no PCI — added internally)
 * tx_len    : request payload length (1..4095)
 * rx        : caller-allocated response buffer
 * rx_buf_size: size of rx buffer
 * out_len   : set to response payload length on success
 * timeout_ms: overall timeout for the request-response cycle
 *
 * Returns ISO15765_OK on success, ISO15765_ERR_TIMEOUT on timeout,
 * ISO15765_ERR_BUFFER_TOO_SMALL if response doesn't fit in rx buffer.
 */
iso15765_status_t iso15765_send_request(iso15765_t *iso,
                                        const uint8_t *tx, size_t tx_len,
                                        uint8_t *rx, size_t rx_buf_size,
                                        size_t *out_len,
                                        uint32_t timeout_ms);

/* =========================================================================
 * Non-blocking receive poll (for event-driven usage)
 * ========================================================================= */

/*
 * Poll for incoming CAN frames and advance any in-progress reception.
 * Call repeatedly from the application loop. When a complete response is
 * assembled, it is stored internally and available via iso15765_rx_get().
 */
iso15765_status_t iso15765_poll(iso15765_t *iso, uint32_t timeout_ms);

/* Retrieve a received response (blocks up to timeout_ms if none yet). */
iso15765_status_t iso15765_rx_get(iso15765_t *iso,
                                  uint8_t *rx, size_t rx_buf_size,
                                  size_t *out_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_ISO15765_2_H */