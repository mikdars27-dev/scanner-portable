/*
 * app/ecu_info.c — ECU system information implementation.
 *
 * Reads VIN, ECU name, and software version via J1979 Mode $09 (CAN) or
 * UDS $22 ReadDID (K-Line).
 *
 * Toolchain note: C++ (Arduino). Compile as C++ (-x c++).
 */

#include "ecu_info.h"

#include <Arduino.h>
#include <string.h>

#include "protocol/j1979.h"
#include "protocol/kline_proto.h"

/* UDS Data Identifiers (common, manufacturer-specific values may vary) */
#define ECU_INFO_DID_VIN         0xF190U   /* VIN */
#define ECU_INFO_DID_SW_VERSION  0xF18CU   /* software/calibration version */

/* J1979 Mode $09 PIDs (not all defined in j1979.h subset) */
#define ECU_INFO_PID_VIN         0x01U
#define ECU_INFO_PID_ECU_NAME    0x02U

struct ecu_info {
    protocol_t *proto;
    bool        initialized;
};

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */
static ecu_info_status_t ecu_info_from_proto(proto_status_t st)
{
    switch (st) {
        case PROTO_OK:                    return ECU_INFO_OK;
        case PROTO_ERR_INVALID_ARG:       return ECU_INFO_ERR_INVALID_ARG;
        case PROTO_ERR_NOT_INITIALIZED:   return ECU_INFO_ERR_NOT_INITIALIZED;
        case PROTO_ERR_TIMEOUT:           return ECU_INFO_ERR_TIMEOUT;
        case PROTO_ERR_BUFFER_TOO_SMALL:  return ECU_INFO_ERR_BUFFER_TOO_SMALL;
        case PROTO_ERR_NEGATIVE_RESPONSE: return ECU_INFO_ERR_PROTOCOL;
        default:                          return ECU_INFO_ERR_PROTOCOL;
    }
}

static ecu_info_status_t ecu_info_from_j1979(j1979_status_t st)
{
    switch (st) {
        case J1979_OK:                    return ECU_INFO_OK;
        case J1979_ERR_INVALID_ARG:       return ECU_INFO_ERR_INVALID_ARG;
        case J1979_ERR_NOT_INITIALIZED:   return ECU_INFO_ERR_NOT_INITIALIZED;
        case J1979_ERR_TIMEOUT:           return ECU_INFO_ERR_TIMEOUT;
        case J1979_ERR_BUFFER_TOO_SMALL:  return ECU_INFO_ERR_BUFFER_TOO_SMALL;
        case J1979_ERR_NEGATIVE_RESPONSE: return ECU_INFO_ERR_PROTOCOL;
        default:                          return ECU_INFO_ERR_PROTOCOL;
    }
}

static ecu_info_status_t ecu_info_from_kline(kline_proto_status_t st)
{
    switch (st) {
        case KLINE_PROTO_OK:                    return ECU_INFO_OK;
        case KLINE_PROTO_ERR_INVALID_ARG:       return ECU_INFO_ERR_INVALID_ARG;
        case KLINE_PROTO_ERR_NOT_INITIALIZED:   return ECU_INFO_ERR_NOT_INITIALIZED;
        case KLINE_PROTO_ERR_TIMEOUT:           return ECU_INFO_ERR_TIMEOUT;
        case KLINE_PROTO_ERR_BUFFER_TOO_SMALL:  return ECU_INFO_ERR_BUFFER_TOO_SMALL;
        case KLINE_PROTO_ERR_NEGATIVE_RESPONSE: return ECU_INFO_ERR_PROTOCOL;
        default:                          return ECU_INFO_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
ecu_info_status_t ecu_info_init(const ecu_info_config_t *cfg,
                                ecu_info_t **out)
{
    if (cfg == NULL || out == NULL) {
        return ECU_INFO_ERR_INVALID_ARG;
    }
    if (cfg->proto == NULL || !proto_is_initialized(cfg->proto)) {
        return ECU_INFO_ERR_INVALID_ARG;
    }

    ecu_info_t *info = (ecu_info_t *)calloc(1, sizeof(*info));
    if (info == NULL) {
        return ECU_INFO_ERR_INVALID_ARG;
    }

    info->proto       = cfg->proto;
    info->initialized = true;

    *out = info;
    return ECU_INFO_OK;
}

ecu_info_status_t ecu_info_deinit(ecu_info_t *info)
{
    if (info == NULL) {
        return ECU_INFO_ERR_INVALID_ARG;
    }
    info->initialized = false;
    free(info);
    return ECU_INFO_OK;
}

bool ecu_info_is_initialized(const ecu_info_t *info)
{
    return info != NULL && info->initialized;
}

/* ------------------------------------------------------------------------
 * Transport helpers
 * ------------------------------------------------------------------------ */

/* Read via J1979 Mode $09 (CAN). Response: [0x49][PID][ASCII data...]. */
static ecu_info_status_t ecu_info_j1979_read(ecu_info_t *info, uint8_t pid,
                                              char *buf, size_t len,
                                              uint32_t timeout_ms)
{
    j1979_t *j1979 = proto_j1979_handle(info->proto);
    if (j1979 == NULL) {
        return ECU_INFO_ERR_NOT_SUPPORTED;
    }

    uint8_t  resp[256];
    size_t   resp_len = 0;
    j1979_status_t st = j1979_send(j1979, J1979_MODE_VEHICLE_INFO, pid,
                                   NULL, 0,
                                   resp, sizeof(resp), &resp_len, timeout_ms);
    if (st != J1979_OK) {
        return ecu_info_from_j1979(st);
    }

    /* Strip response SID (0x49) and PID echo (2 bytes) */
    if (resp_len < 2) {
        return ECU_INFO_ERR_PROTOCOL;
    }

    size_t data_len = resp_len - 2;
    if (data_len >= len) {
        data_len = len - 1;
    }
    memcpy(buf, resp + 2, data_len);
    buf[data_len] = '\0';
    return ECU_INFO_OK;
}

/* Read via UDS $22 ReadDID (K-Line). Response: [0x62][DID_hi][DID_lo][data...]. */
static ecu_info_status_t ecu_info_uds_read_did(ecu_info_t *info, uint16_t did,
                                                char *buf, size_t len,
                                                uint32_t timeout_ms)
{
    kline_proto_t *kline = proto_kline_handle(info->proto);
    if (kline == NULL) {
        return ECU_INFO_ERR_NOT_SUPPORTED;
    }

    uint8_t  resp[256];
    size_t   resp_len = 0;
    kline_proto_status_t st = kline_proto_uds_read_did(kline, did,
                                                       resp, sizeof(resp),
                                                       &resp_len, timeout_ms);
    if (st != KLINE_PROTO_OK) {
        return ecu_info_from_kline(st);
    }

    /* Strip response SID (0x62) and DID echo (2 bytes) */
    if (resp_len < 3) {
        return ECU_INFO_ERR_PROTOCOL;
    }

    size_t data_len = resp_len - 3;
    if (data_len >= len) {
        data_len = len - 1;
    }
    memcpy(buf, resp + 3, data_len);
    buf[data_len] = '\0';
    return ECU_INFO_OK;
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */
ecu_info_status_t ecu_info_read_vin(ecu_info_t *info, char *buf, size_t len,
                                    uint32_t timeout_ms)
{
    if (info == NULL || !info->initialized) {
        return ECU_INFO_ERR_NOT_INITIALIZED;
    }
    if (buf == NULL || len == 0) {
        return ECU_INFO_ERR_INVALID_ARG;
    }

    proto_transport_t transport = proto_get_transport(info->proto);
    if (transport == PROTO_TRANSPORT_CAN) {
        return ecu_info_j1979_read(info, ECU_INFO_PID_VIN, buf, len, timeout_ms);
    }
    if (transport == PROTO_TRANSPORT_KLINE) {
        return ecu_info_uds_read_did(info, ECU_INFO_DID_VIN, buf, len, timeout_ms);
    }
    return ECU_INFO_ERR_NOT_SUPPORTED;
}

ecu_info_status_t ecu_info_read_ecu_name(ecu_info_t *info, char *buf, size_t len,
                                         uint32_t timeout_ms)
{
    if (info == NULL || !info->initialized) {
        return ECU_INFO_ERR_NOT_INITIALIZED;
    }
    if (buf == NULL || len == 0) {
        return ECU_INFO_ERR_INVALID_ARG;
    }

    proto_transport_t transport = proto_get_transport(info->proto);
    if (transport == PROTO_TRANSPORT_CAN) {
        return ecu_info_j1979_read(info, ECU_INFO_PID_ECU_NAME, buf, len, timeout_ms);
    }
    /* K-Line: ECU name DID is manufacturer-specific; skip for now. */
    return ECU_INFO_ERR_NOT_SUPPORTED;
}

ecu_info_status_t ecu_info_read_sw_version(ecu_info_t *info, char *buf, size_t len,
                                           uint32_t timeout_ms)
{
    if (info == NULL || !info->initialized) {
        return ECU_INFO_ERR_NOT_INITIALIZED;
    }
    if (buf == NULL || len == 0) {
        return ECU_INFO_ERR_INVALID_ARG;
    }

    proto_transport_t transport = proto_get_transport(info->proto);
    if (transport == PROTO_TRANSPORT_KLINE) {
        return ecu_info_uds_read_did(info, ECU_INFO_DID_SW_VERSION, buf, len, timeout_ms);
    }
    /* CAN: software version not directly available via J1979 $09. */
    return ECU_INFO_ERR_NOT_SUPPORTED;
}
