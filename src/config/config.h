/*
 * config/config.h — System constants (ESP32 DevKit V4).
 *
 * K-Line values (KLINE_BAUD_DATA, KLINE_WAKEUP_PULSE_MS,
 * KLINE_INIT_BIT_MS, KLINE_TIMEOUT_MS) are consumed by drivers/kline.c
 * callers via kline_config_t; defaults are also documented in drivers/kline.h.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* K-Line ISO 9141-2 (K-Line on UART1, 10.400 baud data phase, 5-baud init) */
#define KLINE_BAUD_DATA         10400UL
#define KLINE_WAKEUP_PULSE_MS   300UL   /* ECU wake-up pulse LOW, 200..400 ms */
#define KLINE_INIT_BIT_MS       200UL   /* 5-baud start bit, ~5 baud */
#define KLINE_TIMEOUT_MS        5000UL  /* data-phase RX/TX timeout */

/* CAN ISO 15765-4 via MCP2515 (SPI) */
#define CAN_BAUDRATE            500000UL
#define CAN_FUNCTIONAL_ADDR     0x7DFUL   /* functional request (broadcast) */
#define CAN_PHYSICAL_ADDR_BASE  0x7E0UL   /* physical request base 0x7E0..0x7E7 */
#define CAN_RESPONSE_ADDR_BASE  0x7E8UL   /* response base 0x7E8..0x7EF */

/* Timeouts and safety */
#define ACTUATOR_TEST_MAX_MS    30000UL   /* forced actuator test timeout */
#define RETRY_MAX               3         /* caller retries before link failure */

/* ADC (battery / cranking / charging measurement) */
#define ADC_VREF_MV             3300.0f   /* ESP32 ADC reference voltage (mV) */
#define ADC_DIVIDER_RATIO       11.0f     /* voltage divider: V_in / V_adc */
#define ADC_SAMPLE_COUNT        16        /* samples to average per reading */
#define ADC_SAMPLE_INTERVAL_MS  5         /* ms between samples */

#endif /* CONFIG_H */
