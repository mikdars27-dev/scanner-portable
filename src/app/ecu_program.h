/*
 * app/ecu_program.h — ECU program / reset implementation (Feature 4: Program ECU).
 *
 * Resets the ECU, enters programming session, performs UDS security access
 * (seed/key), and downloads data to ECU memory over K-Line/UDS via kline_proto_*.
 *
 * Safety: warning_show(WARN_ECU_FLASH) is shown before any operation that writes
 * to the ECU; SELECT is required. Operations that only read session state do not
 * require the warning screen.
 *
 * Layering: APP -> ecu_program_* -> protocol_* -> kline_proto_* -> drivers/kline
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#ifndef APP_ECU_PROGRAM_H
#define APP_ECU_PROGRAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "protocol/protocol.h"     /* protocol_t, proto_kline_handle, proto_is_initialized */
#include "protocol/kline_proto.h"  /* kline_proto_*, KLINE_PROTO_SID_*, KLINE_PROTO_SESSION_* */
#include "ui/warning_screen.h"     /* warning_show, WARN_ECU_FLASH, display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */

typedef enum {
    ECU_PROGRAM_OK                    = 0,
    ECU_PROGRAM_ERR_INVALID_ARG       = -1,
    ECU_PROGRAM_ERR_NOT_INITIALIZED   = -2,
    ECU_PROGRAM_ERR_PROTOCOL          = -3,   /* underlying protocol failure */
    ECU_PROGRAM_ERR_TIMEOUT           = -4,   /* command timeout */
    ECU_PROGRAM_ERR_USER_ABORT        = -5,   /* user declined via warning screen */
    ECU_PROGRAM_ERR_SAFETY            = -6,   /* safety condition not met */
    ECU_PROGRAM_ERR_NOT_SUPPORTED     = -7,   /* transport not K-Line/UDS */
} ecu_program_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */

typedef struct ecu_program ecu_program_t;

/* =========================================================================
 * Config
 * ========================================================================= */

typedef struct {
    protocol_t *proto;   /* already-initialized unified protocol handle */
    display_t  *disp;    /* display for the pre-operation warning screen */
} ecu_program_config_t;

/* Default config (designated initializers). */
#define ECU_PROGRAM_CONFIG_DEFAULT(p, d) { .proto = (p), .disp = (d) }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the ECU program service over an already-initialized protocol
 * handle. cfg->proto and cfg->disp must be non-NULL. */
ecu_program_status_t ecu_program_init(const ecu_program_config_t *cfg,
                                      ecu_program_t **out);

/* Release the ECU program handle. */
ecu_program_status_t ecu_program_deinit(ecu_program_t *ep);

/* True if the handle was successfully initialized. */
bool ecu_program_is_initialized(const ecu_program_t *ep);

/* =========================================================================
 * ECU programming operations
 * ========================================================================= */

/* Reset the ECU (UDS $11 ECUReset, hard reset subfunction 0x01).
 *
 * warning_show(WARN_ECU_FLASH) is shown and SELECT is required before reset;
 * otherwise ECU_PROGRAM_ERR_USER_ABORT is returned and nothing is commanded. */
ecu_program_status_t ecu_program_reset(ecu_program_t *ep, uint32_t timeout_ms);

/* Enter programming session (UDS $10 DiagnosticSessionControl,
 * session = KLINE_PROTO_SESSION_PROGRAMMING 0x02).
 *
 * warning_show(WARN_ECU_FLASH) is shown and SELECT is required before entering
 * programming mode; otherwise ECU_PROGRAM_ERR_USER_ABORT is returned. */
ecu_program_status_t ecu_program_enter_programming(ecu_program_t *ep,
                                                    uint32_t timeout_ms);

/* Request seed for security access (UDS $27 SecurityAccess, odd level).
 * Seed bytes are written to seed buffer; *out_len is set to the seed length.
 * The caller then computes the key (ECU-specific algorithm) and calls
 * ecu_program_send_key(). */
ecu_program_status_t ecu_program_request_seed(ecu_program_t *ep, uint8_t level,
                                              uint8_t *seed, size_t *out_len,
                                              uint32_t timeout_ms);

/* Send key for security access (UDS $27 SecurityAccess, even level). */
ecu_program_status_t ecu_program_send_key(ecu_program_t *ep, uint8_t level,
                                          const uint8_t *key, size_t key_len,
                                          uint32_t timeout_ms);

/* Download data to ECU memory (UDS $36 TransferData, sent in chunks).
 * The caller is responsible for having sent UDS $34 RequestDownload before
 * calling this, and UDS $37 RequestTransferExit after all chunks are sent.
 * Data is sent in ECU_PROGRAM_DOWNLOAD_CHUNK_MAX-sized chunks with an
 * incrementing block sequence counter.
 *
 * warning_show(WARN_ECU_FLASH) is shown and SELECT is required before download;
 * otherwise ECU_PROGRAM_ERR_USER_ABORT is returned. */
ecu_program_status_t ecu_program_download(ecu_program_t *ep, const uint8_t *data,
                                          size_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_ECU_PROGRAM_H */
