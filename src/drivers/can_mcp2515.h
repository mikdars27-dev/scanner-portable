/*
 * drivers/can_mcp2515.h — MCP2515 CAN 2.0B controller driver over SPI.
 *
 * Hardware: MCP2515 via SPI2_HOST (VSPI), CS GPIO 5, INT GPIO 4,
 *           SN65HVD230 transceiver (3.3 V), 500 kbps, 11-bit standard IDs.
 *
 * ISO 15765-4 addressing (OBD2/Euro5 CAN):
 *   Functional request  : 0x7DF (broadcast)
 *   Physical request    : 0x7E0..0x7E7
 *   ECU response        : 0x7E8..0x7EF
 *
 * Fixes applied vs earlier draft:
 *   - Standard-frame ID encoding: SIDH=(id>>3)&0xFF, SIDL=(id&7)<<5,
 *     wire=(SIDH<<3)|(SIDL>>5)  (was: SIDH=id>>5, SIDL=(id&0x1F)<<3).
 *   - Acceptance mask registers: RXM0SIDH=0x20, RXM1SIDH=0x24
 *     (was: wrong/misaligned bases).
 *   - CANINTF cleared with 0xFF (write-1-to-clear), not 0x00.
 *
 * Layering: APP -> J1979/UDS -> ISO 15765-2 transport -> can_* -> spi_*
 */

#ifndef DRIVERS_CAN_MCP2515_H
#define DRIVERS_CAN_MCP2515_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "spi.h"     /* spi_device_t, spi_status_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * MCP2515 register map (selected)
 * ========================================================================= */
/* Bit timing */
#define MCP_REG_CNF3   0x88
#define MCP_REG_CNF2   0x89
#define MCP_REG_CNF1   0x8A
#define MCP_REG_CANINTE 0x2B
#define MCP_REG_CANINTF 0x0C
#define MCP_REG_EFLG    0x2D
#define MCP_REG_CANCTRL 0x0F
#define MCP_REG_CANSTAT 0x0E
#define MCP_REG_TEC     0x1C
#define MCP_REG_REC     0x1D

/* TX buffers */
#define MCP_REG_TXB0CTRL 0x30
#define MCP_REG_TXB1CTRL 0x40
#define MCP_REG_TXB2CTRL 0x50
#define MCP_REG_TXB0SIDH 0x31
#define MCP_REG_TXB1SIDH 0x41
#define MCP_REG_TXB2SIDH 0x51

/* RX buffers */
#define MCP_REG_RXB0CTRL 0x60
#define MCP_REG_RXB1CTRL 0x70
#define MCP_REG_RXB0SIDH 0x61
#define MCP_REG_RXB1SIDH 0x71

/* Acceptance filters */
#define MCP_REG_RXF0SIDH 0x00
#define MCP_REG_RXF1SIDH 0x0D
#define MCP_REG_RXF2SIDH 0x16
#define MCP_REG_RXF3SIDH 0x1A
#define MCP_REG_RXF4SIDH 0x1E
#define MCP_REG_RXF5SIDH 0x22

/* Acceptance masks */
#define MCP_REG_RXM0SIDH 0x20
#define MCP_REG_RXM1SIDH 0x24

/* MCP2515 SPI instructions */
#define MCP_INSTR_WRITE  0x02
#define MCP_INSTR_READ   0x03
#define MCP_INSTR_BITMOD 0x05
#define MCP_INSTR_LOAD_TX0 0x40
#define MCP_INSTR_LOAD_TX1 0x42
#define MCP_INSTR_LOAD_TX2 0x44
#define MCP_INSTR_RTS_TX0  0x81
#define MCP_INSTR_RTS_TX1  0x82
#define MCP_INSTR_RTS_TX2  0x84
#define MCP_INSTR_READ_RX0 0x90
#define MCP_INSTR_READ_RX1 0x94
#define MCP_INSTR_RESET    0xC0

/* CANCTRL modes */
#define MCP_MODE_NORMAL   0x00
#define MCP_MODE_SLEEP    0x20
#define MCP_MODE_LOOPBACK 0x40
#define MCP_MODE_LISTENONLY 0x60
#define MCP_MODE_CONFIG   0x80
#define MCP_MODE_ABORT    0xE0

/* CANSTAT mode mask */
#define MCP_STAT_MODE_MASK 0xE0

/* TXBnCTRL bits */
#define MCP_TXB_EXIDE  0x10   /* extended ID enable (SIDL bit 4) */
#define MCP_TXB_TXREQ  0x08   /* transmit request (same bit) */
#define MCP_TXB_TXIE   0x40   /* transmit interrupt enable */
#define MCP_TXB_TXP0   0x03   /* priority bits */
#define MCP_TXB_TXP1   0x04

/* RXBnCTRL bits */
#define MCP_RXB_RXM_MASK 0x60   /* RXM1:RX0 */
#define MCP_RXB_RXM0     0x20
#define MCP_RXB_RXM1     0x40

/* CANINTF / CANINTE bits */
#define MCP_CANINTF_RX0IF 0x01
#define MCP_CANINTF_RX1IF 0x02
#define MCP_CANINTF_TX0IF 0x04
#define MCP_CANINTF_TX1IF 0x08
#define MCP_CANINTF_TX2IF 0x10
#define MCP_CANINTF_ERRIF 0x20
#define MCP_CANINTF_WAKIF 0x40
#define MCP_CANINTF_MERRF 0x80

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    CAN_OK                    = 0,
    CAN_ERR_INVALID_ARG       = -1,
    CAN_ERR_NOT_INITIALIZED   = -2,
    CAN_ERR_BUSY              = -3,
    CAN_ERR_TIMEOUT           = -4,
    CAN_ERR_INIT_FAILED       = -5,
    CAN_ERR_MODE_FAILED       = -6,
    CAN_ERR_WRITE_FAILED      = -7,
    CAN_ERR_READ_FAILED       = -8,
    CAN_ERR_NO_MESSAGE        = -9,
    CAN_ERR_SPI_FAILED        = -10,
} can_status_t;

/* =========================================================================
 * Types
 * ========================================================================= */
typedef struct {
    uint32_t id;        /* 11-bit standard or 29-bit extended */
    bool     extended;  /* true = 29-bit, false = 11-bit */
    bool     rtr;       /* remote transfer request */
    uint8_t  dlc;       /* 0..8 */
    uint8_t  data[8];
} can_frame_t;

typedef struct {
    spi_device_t *spi_dev;   /* MCP2515 on SPI, CS GPIO 5 */
    int          int_gpio;   /* MCP2515 INT, GPIO 4 */
    uint32_t     baudrate;   /* 500000 */
    bool         loopback;   /* self-test mode */
} can_config_t;

#define CAN_CONFIG_DEFAULT                                             \
    {                                                                  \
        .spi_dev   = NULL,                                             \
        .int_gpio  = CAN_INT_GPIO,                                     \
        .baudrate  = CAN_BAUDRATE,                                     \
        .loopback  = false,                                            \
    }

typedef struct can can_t;   /* opaque handle */

/* Standard-frame OBD addresses */
#define CAN_ID_FUNCTIONAL 0x7DF   /* functional (broadcast) request */
#define CAN_ID_PHYS_BASE  0x7E0   /* physical request base */
#define CAN_ID_RESP_BASE  0x7E8   /* response base */

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize MCP2515: reset, config mode, bit timing, filters, normal mode.
 * Returns CAN_OK or CAN_ERR_*. */
can_status_t can_init(const can_config_t *cfg, can_t **out_can);

/* Release the CAN handle. */
can_status_t can_deinit(can_t *can);

/* =========================================================================
 * Frame I/O
 * ========================================================================= */

/* Transmit a CAN frame. Blocks until the frame is queued to a TX buffer. */
can_status_t can_write(can_t *can, const can_frame_t *frame,
                       uint32_t timeout_ms);

/* Receive a CAN frame. Returns CAN_ERR_NO_MESSAGE if none available. */
can_status_t can_read(can_t *can, can_frame_t *frame, uint32_t timeout_ms);

/* Check if a frame is available without removing it. */
can_status_t can_available(can_t *can, bool *available);

/* =========================================================================
 * Acceptance filter / mask
 * ========================================================================= */

/* Configure acceptance mask 0 or 1 (11-bit ID). Mask bits = 1 => check. */
can_status_t can_set_mask(can_t *can, uint8_t mask_num, uint16_t id,
                          uint16_t id_mask);

/* Configure acceptance filter 0..5 (11-bit ID). */
can_status_t can_set_filter(can_t *can, uint8_t filter_num, uint16_t id);

/* =========================================================================
 * Interrupt
 * ========================================================================= */

/* Read raw CANINTF (interrupt flag) register. */
can_status_t can_intf_get(can_t *can, uint8_t *intf);

/* Clear interrupt flags (write 1 to clear). Mask of MCP_CANINTF_* bits. */
can_status_t can_intf_clear(can_t *can, uint8_t flags);

/* INT GPIO pin level (active LOW from MCP2515). */
bool can_int_pin_active(const can_t *can);

/* =========================================================================
 * Accessors
 * ========================================================================= */
uint32_t can_baudrate_get(const can_t *can);
bool     can_is_initialized(const can_t *can);

/* Encode/decode standard 11-bit ID into MCP2515 SIDH/SIDL register pair. */
void can_sid_encode(uint16_t id, uint8_t *sidh, uint8_t *sidl);
uint16_t can_sid_decode(uint8_t sidh, uint8_t sidl);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_CAN_MCP2515_H */