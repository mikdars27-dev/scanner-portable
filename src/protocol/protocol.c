/*
 * protocol/protocol.c — Unified diagnostic protocol API implementation.
 *
 * Facade over ISO 15765-2 (CAN), SAE J1979 (OBD2), and KWP2000/UDS (K-Line).
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "protocol/protocol.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------ */
typedef struct {
    proto_transport_t transport;
    bool              use_j1979;     /* true = CAN/J1979, false = K-Line/KWP2000 */
    union {
        j1979_t        *j1979;
        kline_proto_t  *kline;
        iso15765_t     *iso15765;
    };
    bool initialized;
} protocol_t;

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

proto_status_t proto_init(const proto_config_t *cfg, protocol_t **out_proto)
{
    if (cfg == NULL || out_proto == NULL) {
        return PROTO_ERR_INVALID_ARG;
    }

    protocol_t *p = (protocol_t *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return PROTO_ERR_INVALID_ARG;
    }

    p->transport = cfg->transport;
    p->initialized = true;

    switch (cfg->transport) {
        case PROTO_TRANSPORT_CAN:
            p->use_j1979 = true;
            p->iso15765 = cfg->can_iso;
            p->j1979 = NULL;
            if (cfg->can_iso != NULL) {
                /* Create a J1979 handle over the ISO 15765 transport. */
                j1979_config_t jcfg = J1979_CONFIG_DEFAULT(cfg->can_iso);
                j1979_status_t jst = j1979_init(&jcfg, &p->j1979);
                if (jst != J1979_OK) {
                    free(p);
                    return PROTO_ERR_PROTOCOL;
                }
            }
            break;

        case PROTO_TRANSPORT_KLINE:
            p->use_j1979 = false;
            p->kline = cfg->kline;
            p->iso15765 = NULL;
            p->j1979 = NULL;
            break;
    }

    *out_proto = p;
    return PROTO_OK;
}

proto_status_t proto_deinit(protocol_t *proto)
{
    if (proto == NULL) return PROTO_ERR_INVALID_ARG;
    if (proto->use_j1979 && proto->j1979 != NULL) {
        j1979_deinit(proto->j1979);
    }
    free(proto);
    return PROTO_OK;
}

bool proto_is_initialized(const protocol_t *proto)
{
    return proto != NULL && proto->initialized;
}

proto_status_t proto_open_session(protocol_t *proto, uint8_t session_type,
                                  uint32_t timeout_ms)
{
    if (proto == NULL) return PROTO_ERR_INVALID_ARG;
    if (!proto->initialized) return PROTO_ERR_NOT_INITIALIZED;

    if (proto->use_j1979) {
        /* J1979 doesn't have explicit session control; Mode $08 can be used
         * for ECU control, but session management is implicit. Return OK. */
        (void)session_type;
        return PROTO_OK;
    }

    return (proto_status_t)kline_proto_uds_session(proto->kline, session_type,
                                                    timeout_ms);
}

proto_status_t proto_close_session(protocol_t *proto, uint32_t timeout_ms)
{
    (void)proto;
    (void)timeout_ms;
    /* No explicit close needed for J1979; K-Line sessions end on link down. */
    return PROTO_OK;
}

proto_status_t proto_tester_present(protocol_t *proto, uint32_t timeout_ms)
{
    if (proto == NULL) return PROTO_ERR_INVALID_ARG;
    if (!proto->initialized) return PROTO_ERR_NOT_INITIALIZED;

    if (proto->use_j1979) {
        /* J1979 Mode $01 PID 0x00 can act as a keepalive poll. */
        uint8_t resp[8];
        size_t resp_len = 0;
        return (proto_status_t)j1979_read_pid(proto->j1979, J1979_PID_SUPPORTED_01_20,
                                              resp, sizeof(resp), &resp_len, timeout_ms);
    }

    return (proto_status_t)kline_proto_uds_tester_present(proto->kline, timeout_ms);
}

proto_status_t proto_read_dtcs(protocol_t *proto, uint8_t mode,
                               dtc_list_t *list, uint32_t timeout_ms)
{
    if (proto == NULL || list == NULL) return PROTO_ERR_INVALID_ARG;
    if (!proto->initialized) return PROTO_ERR_NOT_INITIALIZED;

    if (proto->use_j1979) {
        j1979_status_t st;
        switch (mode) {
            case J1979_MODE_DTC_CONFIRMED:
                st = j1979_read_dtcs_confirmed(proto->j1979, list, timeout_ms);
                break;
            case J1979_MODE_DTC_PENDING:
                st = j1979_read_dtcs_pending(proto->j1979, list, timeout_ms);
                break;
            case J1979_MODE_DTC_PERMANENT:
                st = j1979_read_dtcs_permanent(proto->j1979, list, timeout_ms);
                break;
            default:
                return PROTO_ERR_INVALID_ARG;
        }
        return (proto_status_t)st;
    }

    /* K-Line: use UDS ReadDTCInformation ($19). mode maps to subfunction. */
    {
        uint8_t resp[256];
        size_t resp_len = 0;
        kline_proto_status_t st = kline_proto_uds_read_dtc(proto->kline, mode,
                                                           NULL, 0,
                                                           resp, sizeof(resp), &resp_len,
                                                           timeout_ms);
        if (st != KLINE_PROTO_OK) {
            return (proto_status_t)st;
        }
        /* UDS $19 response: [0x59][subfunc_echo][DTC records...].
         * Try decoding 2-byte DTCs from the payload after the echo byte. */
        size_t dtc_start = 0;
        if (resp_len >= 2) dtc_start = 2;   /* skip SID echo + subfunc echo */
        return (proto_status_t)dtc_decode_list(&resp[dtc_start],
                                               resp_len > dtc_start ? resp_len - dtc_start : 0,
                                               list);
    }
}

proto_status_t proto_clear_dtcs(protocol_t *proto, uint32_t timeout_ms)
{
    if (proto == NULL) return PROTO_ERR_INVALID_ARG;
    if (!proto->initialized) return PROTO_ERR_NOT_INITIALIZED;

    if (proto->use_j1979) {
        return (proto_status_t)j1979_clear_dtcs(proto->j1979, timeout_ms);
    }

    /* K-Line: UDS ClearDiagnosticInformation ($14), mask = all DTCs. */
    return (proto_status_t)kline_proto_uds_clear_dtc(proto->kline,
                                                     0xFFFFFF, timeout_ms);
}

proto_status_t proto_read_pid(protocol_t *proto, uint8_t pid,
                              uint8_t *data, size_t data_buf_size,
                              size_t *out_len, uint32_t timeout_ms)
{
    if (proto == NULL || data == NULL || out_len == NULL) {
        return PROTO_ERR_INVALID_ARG;
    }
    if (!proto->initialized) return PROTO_ERR_NOT_INITIALIZED;

    if (!proto->use_j1979) {
        return PROTO_ERR_INVALID_ARG;   /* PID reads are J1979/CAN only */
    }

    return (proto_status_t)j1979_read_pid(proto->j1979, pid,
                                          data, data_buf_size, out_len,
                                          timeout_ms);
}

proto_status_t proto_send(protocol_t *proto, uint8_t sid,
                          const uint8_t *data, size_t data_len,
                          uint8_t *resp, size_t resp_buf_size,
                          size_t *out_len, uint32_t timeout_ms)
{
    if (proto == NULL || resp == NULL || out_len == NULL) {
        return PROTO_ERR_INVALID_ARG;
    }
    if (!proto->initialized) return PROTO_ERR_NOT_INITIALIZED;

    if (proto->use_j1979) {
        /* For J1979, sid is the mode ($01–$0A), PID is assumed 0x00 unless
         * data_len >= 1 and data[0] is used as PID. */
        uint8_t pid = 0x00;
        const uint8_t *args = NULL;
        size_t args_len = 0;

        /* If caller provided data, first byte is the PID for mode $01/$02 */
        if (sid == J1979_MODE_LIVE_DATA || sid == J1979_MODE_FREEZE_FRAME) {
            if (data_len > 0) {
                pid = data[0];
                args = &data[1];
                args_len = data_len - 1;
            }
        } else {
            args = data;
            args_len = data_len;
        }

        return (proto_status_t)j1979_send(proto->j1979, sid, pid,
                                          args, args_len,
                                          resp, resp_buf_size, out_len,
                                          timeout_ms);
    }

    /* K-Line/KWP2000/UDS */
    return (proto_status_t)kline_proto_send(proto->kline, sid,
                                            data, data_len,
                                            resp, resp_buf_size, out_len,
                                            timeout_ms);
}

j1979_t *proto_j1979_handle(protocol_t *proto)
{
    if (proto == NULL || !proto->use_j1979) return NULL;
    return proto->j1979;
}

proto_transport_t proto_get_transport(protocol_t *proto)
{
    if (proto == NULL) return PROTO_TRANSPORT_CAN;
    return proto->transport;
}

kline_proto_t *proto_kline_handle(protocol_t *proto)
{
    if (proto == NULL || proto->use_j1979) return NULL;
    return proto->kline;
}

iso15765_t *proto_iso15765_handle(protocol_t *proto)
{
    if (proto == NULL || !proto->use_j1979) return NULL;
    return proto->iso15765;
}