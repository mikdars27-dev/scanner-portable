/*
 * protocol/protocol.h — Unified diagnostic protocol API.
 *
 * Facade over ISO 15765-2 (CAN), SAE J1979 (OBD2), and KWP2000/UDS (K-Line).
 * The application layer uses this single API regardless of the physical
 * transport (CAN bus or K-Line serial).
 *
 * Layering: APP -> protocol_* -> {j1979_*, kline_proto_*, iso15765_2_*} -> drivers
 */

#ifndef PROTOCOL_PROTOCOL_H
#define PROTOCOL_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/dtc.h"      /* dtc_list_t, dtc_status_t */
#include "protocol/j1979.h"    /* j1979_t, j1979_status_t */
#include "protocol/kline_proto.h" /* kline_proto_t, kline_proto_status_t */
#include "protocol/iso15765_2.h"   /* iso15765_t, iso15765_status_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Transport type
 * ========================================================================= */
typedef enum {
    PROTO_TRANSPORT_CAN,    /* ISO 15765-2 over CAN (MCP2515) */
    PROTO_TRANSPORT_KLINE,  /* KWP2000/UDS over K-Line */
} proto_transport_t;

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    PROTO_OK                    = 0,
    PROTO_ERR_INVALID_ARG       = -1,
    PROTO_ERR_NOT_INITIALIZED   = -2,
    PROTO_ERR_TIMEOUT           = -3,
    PROTO_ERR_PROTOCOL          = -4,
    PROTO_ERR_BUFFER_TOO_SMALL  = -5,
    PROTO_ERR_NEGATIVE_RESPONSE = -6,
} proto_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
typedef struct protocol protocol_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    proto_transport_t transport;   /* CAN or K-Line */
    union {
        iso15765_t   *can_iso;     /* ISO 15765-2 transport (for CAN) */
        kline_proto_t *kline;      /* KWP2000/UDS over K-Line */
    };
} proto_config_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the unified protocol over the given transport. */
proto_status_t proto_init(const proto_config_t *cfg, protocol_t **out_proto);

/* Release the protocol handle. */
proto_status_t proto_deinit(protocol_t *proto);

/* True if the handle was successfully initialized. */
bool proto_is_initialized(const protocol_t *proto);

/* =========================================================================
 * Diagnostic session
 * ========================================================================= */

/* Open a diagnostic session with the ECU. */
proto_status_t proto_open_session(protocol_t *proto,
                                  uint8_t session_type,
                                  uint32_t timeout_ms);

/* Close the diagnostic session. */
proto_status_t proto_close_session(protocol_t *proto, uint32_t timeout_ms);

/* Keep the session alive (tester present). */
proto_status_t proto_tester_present(protocol_t *proto, uint32_t timeout_ms);

/* =========================================================================
 * DTC operations
 * ========================================================================= */

/* Read DTCs (confirmed, pending, or permanent depending on transport/mode).
 * mode: J1979_MODE_DTC_CONFIRMED ($03), DTC_PENDING ($07), DTC_PERMANENT ($0A),
 *       or KWP2000/UDS equivalent. */
proto_status_t proto_read_dtcs(protocol_t *proto, uint8_t mode,
                               dtc_list_t *list, uint32_t timeout_ms);

/* Clear emission-related DTCs. */
proto_status_t proto_clear_dtcs(protocol_t *proto, uint32_t timeout_ms);

/* =========================================================================
 * Live data (PID-based, OBD2/J1979 only)
 * ========================================================================= */

/* Read a live-data PID. Returns payload bytes after SID+PID echo. */
proto_status_t proto_read_pid(protocol_t *proto, uint8_t pid,
                              uint8_t *data, size_t data_buf_size,
                              size_t *out_len, uint32_t timeout_ms);

/* =========================================================================
 * Generic request / response (transport-agnostic)
 * ========================================================================= */

/* Send a raw diagnostic request: [sid][data...], receive [sid+0x40][data...].
 * For CAN/J1979, sid is the J1979 mode ($01–$0A). For K-Line, sid is the
 * KWP2000/UDS service ID. */
proto_status_t proto_send(protocol_t *proto, uint8_t sid,
                          const uint8_t *data, size_t data_len,
                          uint8_t *resp, size_t resp_buf_size,
                          size_t *out_len, uint32_t timeout_ms);

/* =========================================================================
 * Access to underlying handles (for advanced use)
 * ========================================================================= */
proto_transport_t proto_get_transport(protocol_t *proto);
j1979_t     *proto_j1979_handle(protocol_t *proto);
kline_proto_t *proto_kline_handle(protocol_t *proto);
iso15765_t  *proto_iso15765_handle(protocol_t *proto);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_PROTOCOL_H */