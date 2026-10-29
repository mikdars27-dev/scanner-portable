/*
 * protocol/kline_proto.h — KWP2000 / UDS diagnostic services on K-Line.
 *
 * Implements KWP2000 (ISO 14230-4) and UDS (ISO 14229) service IDs over
 * K-Line ISO 9141-2. Built on top of drivers/kline.c (kline_send/kline_receive/
 * kline_transfer).
 *
 * K-Line requests are typically single-frame: [SID][data...], responses:
 * [SID+0x40][data...]. KWP2000 5-byte addressing header is not used here
 * (most motorcycle/scooter ECUs use simple SID-based diagnostics).
 *
 * Layering: APP -> kline_proto_* -> kline_* -> uart_* + GPIO bit-bang
 */

#ifndef PROTOCOL_KLINE_PROTO_H
#define PROTOCOL_KLINE_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "drivers/kline.h"         /* kline_t, kline_status_t */
#include "protocol/dtc.h"          /* dtc_list_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    KLINE_PROTO_OK                    = 0,
    KLINE_PROTO_ERR_INVALID_ARG       = -1,
    KLINE_PROTO_ERR_NOT_INITIALIZED   = -2,
    KLINE_PROTO_ERR_TIMEOUT           = -3,
    KLINE_PROTO_ERR_PROTOCOL          = -4,   /* unexpected response */
    KLINE_PROTO_ERR_BUFFER_TOO_SMALL  = -5,
    KLINE_PROTO_ERR_NEGATIVE_RESPONSE = -6,   /* NRC (0x7F response) */
} kline_proto_status_t;

/* =========================================================================
 * UDS / KWP2000 Service IDs (SIDs)
 * ========================================================================= */
/* UDS (ISO 14229) */
#define KLINE_PROTO_SID_DIAG_SESSION_CONTROL   0x10
#define KLINE_PROTO_SID_ECU_RESET              0x11
#define KLINE_PROTO_SID_CLEAR_DIAG_INFO        0x12
#define KLINE_PROTO_SID_READ_DTC_INFO          0x13
#define KLINE_PROTO_SID_READ_DATA_BY_ID        0x22
#define KLINE_PROTO_SID_SECURITY_ACCESS        0x27
#define KLINE_PROTO_SID_COMM_CONTROL           0x28
#define KLINE_PROTO_SID_INPUT_OUTPUT_CONTROL   0x2F
#define KLINE_PROTO_SID_TESTER_PRESENT         0x3E
#define KLINE_PROTO_SID_REQUEST_DOWNLOAD       0x34
#define KLINE_PROTO_SID_REQUEST_UPLOAD         0x35
#define KLINE_PROTO_SID_TRANSFER_DATA          0x36
#define KLINE_PROTO_SID_REQUEST_TRANSFER_EXIT  0x37

/* KWP2000 (ISO 14230-4) */
#define KLINE_PROTO_SID_KWP_ECU_RESET          0x85
#define KLINE_PROTO_SID_KWP_READ_DATA_LOCAL    0x86
#define KLINE_PROTO_SID_KWP_READ_DATA_COMMON   0x87

/* Response SID = request SID + 0x40 */
#define KLINE_PROTO_RESP_SID(sid)  ((uint8_t)((sid) + 0x40))

/* Negative response codes (NRC) - UDS */
#define KLINE_PROTO_NRC_GENERAL_REJECT              0x10
#define KLINE_PROTO_NRC_SERVICE_NOT_SUPPORTED       0x11
#define KLINE_PROTO_NRC_SUBFUNCTION_NOT_SUPPORTED   0x12
#define KLINE_PROTO_NRC_REQUEST_OUT_OF_RANGE        0x31
#define KLINE_PROTO_NRC_SECURITY_ACCESS_DENIED      0x33

/* Diagnostic session types (UDS $10 subfunction) */
#define KLINE_PROTO_SESSION_DEFAULT         0x01
#define KLINE_PROTO_SESSION_PROGRAMMING     0x02
#define KLINE_PROTO_SESSION_EXTENDED        0x03

/* SecurityAccess levels (UDS $27) */
#define KLINE_PROTO_SECURITY_LEVEL_1        0x01
#define KLINE_PROTO_SECURITY_LEVEL_2        0x02

/* =========================================================================
 * Config / handle
 * ========================================================================= */
typedef struct kline_proto kline_proto_t;   /* opaque handle */

typedef struct {
    kline_t *kline;   /* K-Line handle (must have kline_begin() called first) */
} kline_proto_config_t;

#define KLINE_PROTO_CONFIG_DEFAULT(kline_handle)                           \
    {                                                                      \
        .kline = (kline_handle),                                           \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize KWP2000/UDS protocol over a K-Line handle.
 * The caller must have called kline_begin() on the kline handle first
 * (wake-up + 5-baud init + switch to 10400 baud). */
kline_proto_status_t kline_proto_init(const kline_proto_config_t *cfg,
                                      kline_proto_t **out_proto);

/* Release the KWP2000/UDS handle. */
kline_proto_status_t kline_proto_deinit(kline_proto_t *proto);

/* True if the handle was successfully initialized. */
bool kline_proto_is_initialized(const kline_proto_t *proto);

/* =========================================================================
 * Generic request / response
 * ========================================================================= */

/*
 * Send a KWP2000/UDS request and receive the response.
 *
 * sid        : service ID (0x10–0x3F, 0x85–0x87)
 * data       : request payload bytes after SID (may be NULL if data_len==0)
 * data_len   : length of data
 * resp       : caller-allocated response buffer (payload after SID+0x40 echo)
 * resp_buf_size: size of resp buffer
 * out_len    : set to response payload length on success
 * timeout_ms : overall timeout
 *
 * Returns KLINE_PROTO_OK on success. On NRC (0x7F response), returns
 * KLINE_PROTO_ERR_NEGATIVE_RESPONSE and resp[0] holds the NRC code.
 */
kline_proto_status_t kline_proto_send(kline_proto_t *proto,
                                      uint8_t sid, const uint8_t *data,
                                      size_t data_len,
                                      uint8_t *resp, size_t resp_buf_size,
                                      size_t *out_len, uint32_t timeout_ms);

/* =========================================================================
 * UDS mode-specific helpers
 * ========================================================================= */

/*
 * UDS $10 DiagnosticSessionControl.
 * session_type: one of KLINE_PROTO_SESSION_*.
 */
kline_proto_status_t kline_proto_uds_session(kline_proto_t *proto,
                                             uint8_t session_type,
                                             uint32_t timeout_ms);

/*
 * UDS $27 SecurityAccess — request seed (odd level: 0x01, 0x03, ...).
 * seed buffer is filled by the ECU; caller then computes key and calls
 * kline_proto_uds_key().
 */
kline_proto_status_t kline_proto_uds_seed(kline_proto_t *proto,
                                          uint8_t level,
                                          uint8_t *seed, size_t seed_buf_size,
                                          size_t *out_len,
                                          uint32_t timeout_ms);

/*
 * UDS $27 SecurityAccess — send key (even level: 0x02, 0x04, ...).
 */
kline_proto_status_t kline_proto_uds_key(kline_proto_t *proto,
                                         uint8_t level,
                                         const uint8_t *key, size_t key_len,
                                         uint32_t timeout_ms);

/*
 * UDS $19 ReadDTCInformation.
 * subfunc: DTC subfunction (e.g. 0x01 reportNumberOfDTCByStatusMask).
 * args: optional argument bytes after subfunction.
 */
kline_proto_status_t kline_proto_uds_read_dtc(kline_proto_t *proto,
                                              uint8_t subfunc,
                                              const uint8_t *args, size_t args_len,
                                              uint8_t *resp, size_t resp_buf_size,
                                              size_t *out_len, uint32_t timeout_ms);

/*
 * UDS $14 ClearDiagnosticInformation.
 * dtc_mask: group of DTCs to clear (0xFFFFFF = all).
 */
kline_proto_status_t kline_proto_uds_clear_dtc(kline_proto_t *proto,
                                               uint32_t dtc_mask,
                                               uint32_t timeout_ms);

/*
 * UDS $22 ReadDataByIdentifier.
 * did: 16-bit data identifier.
 */
kline_proto_status_t kline_proto_uds_read_did(kline_proto_t *proto,
                                              uint16_t did,
                                              uint8_t *resp, size_t resp_buf_size,
                                              size_t *out_len, uint32_t timeout_ms);

/*
 * UDS $2F InputOutputControlByIdentifier.
 * did: 16-bit data identifier.
 * control_op: 0x01=returnToEcu, 0x02=shortTermAdjustment, 0x03=start, etc.
 * params: optional control parameters.
 */
kline_proto_status_t kline_proto_uds_io_control(kline_proto_t *proto,
                                                uint16_t did, uint8_t control_op,
                                                const uint8_t *params, size_t params_len,
                                                uint8_t *resp, size_t resp_buf_size,
                                                size_t *out_len, uint32_t timeout_ms);

/*
 * UDS $3E TesterPresent — keep session alive.
 */
kline_proto_status_t kline_proto_uds_tester_present(kline_proto_t *proto,
                                                     uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_KLINE_PROTO_H */