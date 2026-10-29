/*
 * drivers/can_mcp2515.c — MCP2515 CAN 2.0B driver over SPI (ESP32 Arduino).
 *
 * Fixes applied vs earlier draft:
 *   - Standard-frame ID encoding: SIDH=(id>>3)&0xFF, SIDL=(id&7)<<5,
 *     decode=(sidh<<3)|(sidl>>5)  (was broken: SIDH=id>>5, SIDL=(id&0x1F)<<3).
 *   - Acceptance mask registers: RXM0SIDH=0x20, RXM1SIDH=0x24
 *     (was: wrong/misaligned bases).
 *   - CANINTF cleared by writing 0xFF (write-1-to-clear), not 0x00.
 *
 * Toolchain note: C++ (Arduino SPI). Compile as C++.
 */

#include "can_mcp2515.h"

#include <Arduino.h>

/* MCP2515 crystal is 8 MHz on typical modules. TQ = 125 ns (BRP=0).
 * 500 kbps = 2 us bit time = 16 TQ:
 *   Sync(1) + Prop(6) + Phase1(5) + Phase2(4) = 16 TQ */
#define MCP_XTAL_HZ       8000000UL
#define MCP_CNF1_500K     0x00   /* BRP=0, SJW=1TQ */
#define MCP_CNF2_500K     0x95   /* BTLMODE=1, SAM=0, PHSEG1=5TQ, PRSEG=6TQ */
#define MCP_CNF3_500K     0x03   /* WAKFIL=0, PHSEG2=4TQ */

/* TX buffer control registers */
static const uint8_t MCP_TXB_CTRL[3]  = { MCP_REG_TXB0CTRL, MCP_REG_TXB1CTRL, MCP_REG_TXB2CTRL };
static const uint8_t MCP_TXB_SIDH[3]  = { MCP_REG_TXB0SIDH,  MCP_REG_TXB1SIDH,  MCP_REG_TXB2SIDH };
static const uint8_t MCP_RTS_INSTR[3] = { MCP_INSTR_RTS_TX0,  MCP_INSTR_RTS_TX1,  MCP_INSTR_RTS_TX2  };

/* ------------------------------------------------------------------------
 * Low-level MCP2515 SPI access
 * ------------------------------------------------------------------------ */

/* Write one MCP2515 register: [INSTR_WRITE][addr][data]. */
static can_status_t mcp_write_reg(scanner_spi_device_t *spi, uint8_t reg, uint8_t val)
{
    uint8_t buf[3] = { MCP_INSTR_WRITE, reg, val };
    spi_status_t st = spi_write(spi, buf, sizeof(buf), 100);
    return (st == SPI_OK) ? CAN_OK : CAN_ERR_SPI_FAILED;
}

/* Read one MCP2515 register: [INSTR_READ][addr][0xFF] -> [dummy][data]. */
static can_status_t mcp_read_reg(scanner_spi_device_t *spi, uint8_t reg, uint8_t *val)
{
    uint8_t tx[3] = { MCP_INSTR_READ, reg, 0xFF };
    uint8_t rx[3] = { 0, 0, 0 };
    spi_status_t st = spi_transfer(spi, tx, rx, sizeof(tx), 100);
    if (st != SPI_OK) return CAN_ERR_SPI_FAILED;
    *val = rx[2];
    return CAN_OK;
}

/* Bit-modify: [INSTR_BITMOD][addr][mask][data]. */
static can_status_t mcp_bit_modify(scanner_spi_device_t *spi, uint8_t reg,
                                   uint8_t mask, uint8_t data)
{
    uint8_t buf[4] = { MCP_INSTR_BITMOD, reg, mask, data };
    spi_status_t st = spi_write(spi, buf, sizeof(buf), 100);
    return (st == SPI_OK) ? CAN_OK : CAN_ERR_SPI_FAILED;
}

/* Reset MCP2515. */
static can_status_t mcp_reset(scanner_spi_device_t *spi)
{
    uint8_t instr = MCP_INSTR_RESET;
    spi_status_t st = spi_write(spi, &instr, 1, 100);
    return (st == SPI_OK) ? CAN_OK : CAN_ERR_SPI_FAILED;
}

/* Request-to-send on a TX buffer. */
static can_status_t mcp_rts(scanner_spi_device_t *spi, uint8_t txb_idx)
{
    uint8_t instr = MCP_RTS_INSTR[txb_idx];
    spi_status_t st = spi_write(spi, &instr, 1, 100);
    return (st == SPI_OK) ? CAN_OK : CAN_ERR_SPI_FAILED;
}

/* ------------------------------------------------------------------------
 * CANCTRL mode
 * ------------------------------------------------------------------------ */

/* Set CANCTRL mode and wait for CANSTAT to reflect it. */
static can_status_t can_set_mode(can_t *can, uint8_t mode)
{
    if (mcp_write_reg(can->spi_dev, MCP_REG_CANCTRL, mode) != CAN_OK) {
        return CAN_ERR_SPI_FAILED;
    }
    /* Wait up to 100 ms for mode change. */
    for (int i = 0; i < 100; i++) {
        uint8_t stat = 0;
        if (mcp_read_reg(can->spi_dev, MCP_REG_CANSTAT, &stat) != CAN_OK) {
            return CAN_ERR_SPI_FAILED;
        }
        if ((stat & MCP_STAT_MODE_MASK) == mode) {
            return CAN_OK;
        }
        delay(1);
    }
    return CAN_ERR_MODE_FAILED;
}

/* ------------------------------------------------------------------------
 * Standard-frame ID encoding/decoding (FIXED)
 *   SIDH = (id >> 3) & 0xFF    (identifier bits 10:3)
 *   SIDL = (id & 0x07) << 5    (identifier bits 2:0 in SIDL[7:5])
 *   id   = (sidh << 3) | (sidl >> 5)
 */
void can_sid_encode(uint16_t id, uint8_t *sidh, uint8_t *sidl)
{
    if (sidh) *sidh = (uint8_t)((id >> 3) & 0xFF);
    if (sidl) *sidl = (uint8_t)((id & 0x07) << 5);
}

uint16_t can_sid_decode(uint8_t sidh, uint8_t sidl)
{
    return (uint16_t)(((uint16_t)sidh << 3) | ((uint16_t)sidl >> 5));
}

/* ------------------------------------------------------------------------
 * Frame loading into a TX buffer
 * ------------------------------------------------------------------------ */
static can_status_t mcp_load_tx(can_t *can, uint8_t txb_idx,
                                const can_frame_t *frame)
{
    uint8_t sidh, sidl;
    can_sid_encode(frame->id, &sidh, &sidl);

    uint8_t buf[1 + 5 + 8];   /* instr + SIDH,SIDL,EID8,EID0,DLC + up to 8 data */
    size_t  n = 0;

    buf[n++] = MCP_INSTR_LOAD_TX0 + (uint8_t)(txb_idx * 2);  /* 0x40, 0x42, 0x44 */
    buf[n++] = sidh;
    /* Standard data frame: EXIDE=0, RTR=0. SIDL holds ID bits 2:0 in [7:5]. */
    buf[n++] = sidl;
    buf[n++] = 0x00;   /* EID8 (standard frame) */
    buf[n++] = 0x00;   /* EID0 (standard frame) */
    buf[n++] = frame->dlc & 0x0F;   /* DLC */
    for (uint8_t i = 0; i < frame->dlc && i < 8; i++) {
        buf[n++] = frame->data[i];
    }

    spi_status_t st = spi_write(can->spi_dev, buf, n, 100);
    return (st == SPI_OK) ? CAN_OK : CAN_ERR_SPI_FAILED;
}

/* Find a free TX buffer (TXREQ not set). Returns index 0..2, or -1. */
static int mcp_free_txb(can_t *can)
{
    for (int i = 0; i < 3; i++) {
        uint8_t ctrl = 0;
        if (mcp_read_reg(can->spi_dev, MCP_TXB_CTRL[i], &ctrl) != CAN_OK) {
            return -1;
        }
        if (!(ctrl & 0x08)) {   /* TXREQ clear = buffer empty */
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
can_status_t can_init(const can_config_t *cfg, can_t **out_can)
{
    if (cfg == NULL || out_can == NULL || cfg->spi_dev == NULL) {
        return CAN_ERR_INVALID_ARG;
    }
    if (!spi_is_initialized(cfg->spi_dev)) {
        return CAN_ERR_NOT_INITIALIZED;
    }

    can_t *can = (can_t *)calloc(1, sizeof(*can));
    if (can == NULL) {
        return CAN_ERR_INVALID_ARG;
    }

    can->spi_dev   = cfg->spi_dev;
    can->int_gpio  = cfg->int_gpio;
    can->baudrate  = cfg->baudrate;

    pinMode(cfg->int_gpio, INPUT);

    /* 1. Reset. */
    if (mcp_reset(cfg->spi_dev) != CAN_OK) {
        free(can);
        return CAN_ERR_INIT_FAILED;
    }
    delay(10);

    /* 2. Enter config mode. */
    if (can_set_mode(can, MCP_MODE_CONFIG) != CAN_OK) {
        free(can);
        return CAN_ERR_MODE_FAILED;
    }

    /* 3. Bit timing (500 kbps, 8 MHz crystal). */
    if (mcp_write_reg(can->spi_dev, MCP_REG_CNF1, MCP_CNF1_500K) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_CNF2, MCP_CNF2_500K) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_CNF3, MCP_CNF3_500K) != CAN_OK) {
        free(can);
        return CAN_ERR_SPI_FAILED;
    }

    /* 4. Disable all interrupts initially. */
    if (mcp_write_reg(can->spi_dev, MCP_REG_CANINTE, 0x00) != CAN_OK) {
        free(can);
        return CAN_ERR_SPI_FAILED;
    }

    /* 5. FIX: clear CANINTF by writing 0xFF (write-1-to-clear).
     * Writing 0x00 clears nothing. */
    if (mcp_write_reg(can->spi_dev, MCP_REG_CANINTF, 0xFF) != CAN_OK) {
        free(can);
        return CAN_ERR_SPI_FAILED;
    }

    /* 6. RX buffers: accept any standard ID (RXM = 00 = enable all messages). */
    if (mcp_write_reg(can->spi_dev, MCP_REG_RXB0CTRL, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXB1CTRL, 0x00) != CAN_OK) {
        free(can);
        return CAN_ERR_SPI_FAILED;
    }

    /* 7. Default acceptance masks: accept all (mask = 0 => don't care). */
    if (mcp_write_reg(can->spi_dev, MCP_REG_RXM0SIDH, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXM0SIDL, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXM0EID8, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXM0EID0, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXM1SIDH, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXM1SIDL, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXM1EID8, 0x00) != CAN_OK ||
        mcp_write_reg(can->spi_dev, MCP_REG_RXM1EID0, 0x00) != CAN_OK) {
        free(can);
        return CAN_ERR_SPI_FAILED;
    }

    /* 8. Enter normal mode. */
    if (can_set_mode(can, MCP_MODE_NORMAL) != CAN_OK) {
        free(can);
        return CAN_ERR_MODE_FAILED;
    }

    *out_can = can;
    return CAN_OK;
}

can_status_t can_deinit(can_t *can)
{
    if (can == NULL) {
        return CAN_ERR_INVALID_ARG;
    }
    if (can->spi_dev != NULL) {
        /* Return to config mode, disable interrupts. */
        can_set_mode(can, MCP_MODE_CONFIG);
        mcp_write_reg(can->spi_dev, MCP_REG_CANINTE, 0x00);
    }
    free(can);
    return CAN_OK;
}

/* ------------------------------------------------------------------------
 * Frame I/O
 * ------------------------------------------------------------------------ */
can_status_t can_write(can_t *can, const can_frame_t *frame, uint32_t timeout_ms)
{
    if (can == NULL || frame == NULL) {
        return CAN_ERR_INVALID_ARG;
    }
    if (frame->dlc > 8) {
        return CAN_ERR_INVALID_ARG;
    }

    (void)timeout_ms;

    int txb = mcp_free_txb(can);
    if (txb < 0) {
        return CAN_ERR_BUSY;
    }

    if (mcp_load_tx(can, (uint8_t)txb, frame) != CAN_OK) {
        return CAN_ERR_SPI_FAILED;
    }
    if (mcp_rts(can, (uint8_t)txb) != CAN_OK) {
        return CAN_ERR_SPI_FAILED;
    }
    return CAN_OK;
}

can_status_t can_read(can_t *can, can_frame_t *frame, uint32_t timeout_ms)
{
    if (can == NULL || frame == NULL) {
        return CAN_ERR_INVALID_ARG;
    }

    uint32_t start = (uint32_t)millis();
    for (;;) {
        uint8_t intf = 0;
        if (mcp_read_reg(can->spi_dev, MCP_REG_CANINTF, &intf) != CAN_OK) {
            return CAN_ERR_SPI_FAILED;
        }

        uint8_t rx_instr = 0;
        if (intf & MCP_CANINTF_RX0IF) rx_instr = MCP_INSTR_READ_RX0;
        else if (intf & MCP_CANINTF_RX1IF) rx_instr = MCP_INSTR_READ_RX1;
        else {
            if ((int32_t)((int32_t)(uint32_t)millis() - (int32_t)start) >=
                (int32_t)timeout_ms) {
                return CAN_ERR_NO_MESSAGE;
            }
            delay(1);
            continue;
        }

        /* Read RX frame: [instr][SIDH][SIDL][EID8][EID0][DCL][data...] */
        uint8_t rxbuf[1 + 5 + 8];
        uint8_t txbuf[1 + 5 + 8];
        txbuf[0] = rx_instr;
        for (size_t i = 1; i < sizeof(txbuf); i++) txbuf[i] = 0xFF;

        spi_status_t st = spi_transfer(can->spi_dev, txbuf, rxbuf, sizeof(txbuf), 100);
        if (st != SPI_OK) return CAN_ERR_SPI_FAILED;

        size_t off = 1;   /* skip instruction echo */
        uint8_t sidh = rxbuf[off++];
        uint8_t sidl = rxbuf[off++];
        uint8_t eid8 = rxbuf[off++];
        uint8_t eid0 = rxbuf[off++];
        uint8_t dlc  = rxbuf[off++];

        frame->id      = can_sid_decode(sidh, sidl);
        frame->extended = (sidl & 0x10) != 0;   /* EXIDE at bit 4 */
        frame->rtr     = (sidl & 0x08) != 0;    /* RTR at bit 3 */
        frame->dlc     = dlc & 0x0F;
        for (uint8_t i = 0; i < frame->dlc && i < 8; i++) {
            frame->data[i] = rxbuf[off++];
        }

        /* Clear the RX interrupt flag (write 1 to clear). */
        uint8_t clear_mask = (rx_instr == MCP_INSTR_READ_RX0)
                                 ? MCP_CANINTF_RX0IF
                                 : MCP_CANINTF_RX1IF;
        mcp_write_reg(can->spi_dev, MCP_REG_CANINTF, clear_mask);

        return CAN_OK;
    }
}

can_status_t can_available(can_t *can, bool *available)
{
    if (can == NULL || available == NULL) {
        return CAN_ERR_INVALID_ARG;
    }
    uint8_t intf = 0;
    if (mcp_read_reg(can->spi_dev, MCP_REG_CANINTF, &intf) != CAN_OK) {
        return CAN_ERR_SPI_FAILED;
    }
    *available = (intf & (MCP_CANINTF_RX0IF | MCP_CANINTF_RX1IF)) != 0;
    return CAN_OK;
}

/* ------------------------------------------------------------------------
 * Acceptance filter / mask
 * ------------------------------------------------------------------------ */
can_status_t can_set_mask(can_t *can, uint8_t mask_num, uint16_t id,
                          uint16_t id_mask)
{
    if (can == NULL) return CAN_ERR_INVALID_ARG;
    if (mask_num > 1) return CAN_ERR_INVALID_ARG;

    uint8_t sidh, sidl;
    uint8_t mask_sidh, mask_sidl;
    can_sid_encode(id, &sidh, &sidl);
    can_sid_encode(id_mask, &mask_sidh, &mask_sidl);

    uint8_t base = (mask_num == 0) ? MCP_REG_RXM0SIDH : MCP_REG_RXM1SIDH;

    /* RXM0/RXM1: SIDH, SIDL, EID8, EID0 (4 consecutive registers) */
    uint8_t regs[4] = { base, base + 1, base + 2, base + 3 };
    uint8_t vals[4] = { sidh, sidl, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF) };
    uint8_t mvals[4] = { mask_sidh, mask_sidl,
                         (uint8_t)(id_mask >> 8), (uint8_t)(id_mask & 0xFF) };

    for (int i = 0; i < 4; i++) {
        if (mcp_write_reg(can->spi_dev, regs[i], mvals[i]) != CAN_OK) {
            return CAN_ERR_SPI_FAILED;
        }
    }
    return CAN_OK;
}

can_status_t can_set_filter(can_t *can, uint8_t filter_num, uint16_t id)
{
    if (can == NULL) return CAN_ERR_INVALID_ARG;
    if (filter_num > 5) return CAN_ERR_INVALID_ARG;

    static const uint8_t FILTER_BASE[6] = {
        MCP_REG_RXF0SIDH, MCP_REG_RXF1SIDH, MCP_REG_RXF2SIDH,
        MCP_REG_RXF3SIDH, MCP_REG_RXF4SIDH, MCP_REG_RXF5SIDH,
    };

    uint8_t sidh, sidl;
    can_sid_encode(id, &sidh, &sidl);

    uint8_t base = FILTER_BASE[filter_num];
    uint8_t regs[4] = { base, base + 1, base + 2, base + 3 };
    uint8_t vals[4] = { sidh, sidl, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF) };

    for (int i = 0; i < 4; i++) {
        if (mcp_write_reg(can->spi_dev, regs[i], vals[i]) != CAN_OK) {
            return CAN_ERR_SPI_FAILED;
        }
    }
    return CAN_OK;
}

/* ------------------------------------------------------------------------
 * Interrupt
 * ------------------------------------------------------------------------ */
can_status_t can_intf_get(can_t *can, uint8_t *intf)
{
    if (can == NULL || intf == NULL) return CAN_ERR_INVALID_ARG;
    return mcp_read_reg(can->spi_dev, MCP_REG_CANINTF, intf);
}

can_status_t can_intf_clear(can_t *can, uint8_t flags)
{
    if (can == NULL) return CAN_ERR_INVALID_ARG;
    /* FIX: write-1-to-clear — setting bits to 1 clears those flags. */
    return mcp_write_reg(can->spi_dev, MCP_REG_CANINTF, flags);
}

bool can_int_pin_active(const can_t *can)
{
    if (can == NULL) return false;
    return digitalRead(can->int_gpio) == LOW;   /* MCP2515 INT is active LOW */
}

/* ------------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------------ */
uint32_t can_baudrate_get(const can_t *can)
{
    return (can != NULL) ? can->baudrate : 0;
}

bool can_is_initialized(const can_t *can)
{
    return can != NULL;
}