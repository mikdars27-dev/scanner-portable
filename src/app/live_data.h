/*
 * app/live_data.h — OBD2 live-data (mode $01 PID) application layer.
 *
 * Reads live-data PIDs over the unified protocol and formats raw response
 * bytes into human-readable strings (engine RPM, vehicle speed, coolant /
 * intake air temperature, calculated engine load, MAF airflow, throttle
 * position) for the UI layer.
 *
 * Layering: APP -> live_data_* -> proto_* -> j1979_* -> iso15765_2_* -> drivers
 */

#ifndef APP_LIVE_DATA_H
#define APP_LIVE_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config/config.h"
#include "config/pinout.h"
#include "protocol/j1979.h"
#include "protocol/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    LIVE_DATA_OK                    = 0,
    LIVE_DATA_ERR_INVALID_ARG       = -1,
    LIVE_DATA_ERR_NOT_INITIALIZED   = -2,
    LIVE_DATA_ERR_PROTOCOL          = -3,
    LIVE_DATA_ERR_BUFFER_TOO_SMALL  = -4,
    LIVE_DATA_ERR_TIMEOUT           = -5,   /* underlying transport timeout */
} live_data_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

/* The real struct is defined only in app/live_data.c. */
typedef struct live_data live_data_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* initialized protocol handle (CAN/J1979) */
} live_data_config_t;

#define LIVE_DATA_CONFIG_DEFAULT(p) { .proto = (p) }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the live-data layer over an initialized protocol handle. */
live_data_status_t live_data_init(const live_data_config_t *cfg,
                                  live_data_t **out);

/* Release the live-data handle. */
live_data_status_t live_data_deinit(live_data_t *ld);

/* True if the handle was successfully initialized. */
bool live_data_is_initialized(const live_data_t *ld);

/* =========================================================================
 * Live data (mode $01 PIDs)
 * ========================================================================= */

/*
 * Read live-data PID `pid` and format it into out_buf as a human-readable
 * string (e.g. "RPM: 850 rpm"). An unknown PID yields
 * "PID 0x?? not supported". out_buf is always NUL-terminated on success.
 */
live_data_status_t live_data_read_pid(live_data_t *ld, uint8_t pid,
                                      char *out_buf, size_t out_buf_len,
                                      uint32_t timeout_ms);

/*
 * Format raw PID response bytes into a human-readable string (same output
 * formats and unknown-PID message as live_data_read_pid). Pure conversion
 * with no protocol access, usable by the UI layer without a live session.
 */
live_data_status_t live_data_format_pid(uint8_t pid, const uint8_t *data,
                                        size_t len, char *out_buf,
                                        size_t out_buf_len);

#ifdef __cplusplus
}
#endif

#endif /* APP_LIVE_DATA_H */
