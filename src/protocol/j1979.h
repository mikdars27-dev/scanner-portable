/*
 * protocol/j1979.h — SAE J1979 OBD2 diagnostic modes $01–$0A.
 *
 * Implements all ten OBD2/EOBD service modes on top of ISO 15765-2 CAN
 * transport. Request format: [mode][PID][args...], response: [mode+0x40][PID][data].
 *
 * Modes:
 *   $01 Powertrain live data (PID based)
 *   $02 Powertrain freeze frame data (PID based)
 *   $03 Emission-related DTCs (confirmed)
 *   $04 Clear/reset emission-related diagnostic information
 *   $05 Oxygen sensor monitoring test results
 *   $06 Non-continuously monitored system test results
 *   $07 Pending emission-related DTCs
 *   $08 Control of on-board system/test/component
 *   $09 Vehicle information
 *   $0A Permanent emission-related DTCs
 *
 * Layering: APP -> j1979_* -> iso15765_2_* -> can_* -> spi_*
 */

#ifndef PROTOCOL_J1979_H
#define PROTOCOL_J1979_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/iso15765_2.h"   /* iso15765_t, iso15765_status_t */
#include "protocol/dtc.h"          /* dtc_list_t, dtc_status_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    J1979_OK                    = 0,
    J1979_ERR_INVALID_ARG       = -1,
    J1979_ERR_NOT_INITIALIZED   = -2,
    J1979_ERR_TIMEOUT           = -3,
    J1979_ERR_PROTOCOL          = -4,   /* unexpected response SID/PID */
    J1979_ERR_BUFFER_TOO_SMALL  = -5,
    J1979_ERR_NEGATIVE_RESPONSE = -6,   /* ECU sent NRC (0x7F response) */
} j1979_status_t;

/* =========================================================================
 * Service mode IDs
 * ========================================================================= */
#define J1979_MODE_LIVE_DATA       0x01
#define J1979_MODE_FREEZE_FRAME    0x02
#define J1979_MODE_DTC_CONFIRMED   0x03
#define J1979_MODE_CLEAR_DTC       0x04
#define J1979_MODE_O2_SENSOR       0x05
#define J1979_MODE_MONITOR_NONCONT 0x06
#define J1979_MODE_DTC_PENDING     0x07
#define J1979_MODE_CONTROL         0x08
#define J1979_MODE_VEHICLE_INFO    0x09
#define J1979_MODE_DTC_PERMANENT   0x0A

/* Response SID = request mode + 0x40 */
#define J1979_RESP_SID(mode)  ((uint8_t)((mode) + 0x40))

/* Negative response codes (NRC) */
#define J1979_NRC_GENERAL_REJECT       0x10
#define J1979_NRC_SERVICE_NOT_SUPPORTED 0x11
#define J1979_NRC_SUBFUNCTION_NOT_SUPPORTED 0x12
#define J1979_NRC_REQUEST_OUT_OF_RANGE  0x31
#define J1979_NRC_SECURITY_ACCESS       0x33

/* =========================================================================
 * Common PID IDs (mode $01 / $02 / $06)
 * ========================================================================= */
#define J1979_PID_SUPPORTED_01_20   0x00
#define J1979_PID_MONITOR_STATUS    0x01
#define J1979_PID_FREEZE_DTC        0x02
#define J1979_PID_FUEL_SYSTEM       0x03
#define J1979_PID_ENGINE_LOAD       0x04
#define J1979_PID_COOLANT_TEMP      0x05
#define J1979_PID_SHORT_FUEL_TRIM_1 0x06
#define J1979_PID_LONG_FUEL_TRIM_1  0x07
#define J1979_PID_SHORT_FUEL_TRIM_2 0x08
#define J1979_PID_LONG_FUEL_TRIM_2  0x09
#define J1979_PID_FUEL_PRESSURE     0x0A
#define J1979_PID_INTAKE_MAP        0x0B
#define J1979_PID_ENGINE_RPM        0x0C
#define J1979_PID_VEHICLE_SPEED     0x0D
#define J1979_PID_IGNITION_ADVANCE  0x0E
#define J1979_PID_INTAKE_TEMP       0x0F
#define J1979_PID_MAF               0x10
#define J1979_PID_THROTTLE_POS      0x11

/* =========================================================================
 * Config / handle
 * ========================================================================= */
typedef struct j1979 j1979_t;   /* opaque handle */

typedef struct {
    iso15765_t *transport;   /* ISO 15765-2 CAN transport */
} j1979_config_t;

#define J1979_CONFIG_DEFAULT(transport)                                    \
    {                                                                      \
        .transport = (transport),                                          \
    }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize J1979 over an ISO 15765-2 transport. */
j1979_status_t j1979_init(const j1979_config_t *cfg, j1979_t **out_j1979);

/* Release the J1979 handle. */
j1979_status_t j1979_deinit(j1979_t *j1979);

/* True if the handle was successfully initialized. */
bool j1979_is_initialized(const j1979_t *j1979);

/* =========================================================================
 * Generic request / response
 * ========================================================================= */

/*
 * Send a J1979 request and receive the raw response.
 *
 * mode    : service mode ($01–$0A)
 * pid     : parameter ID (0x00 if not applicable)
 * args    : optional additional request bytes (may be NULL)
 * args_len: length of args
 * resp    : caller-allocated response buffer (payload only, SID/PID stripped)
 * resp_buf_size: size of resp buffer
 * out_len : set to response payload length on success
 * timeout_ms: overall timeout
 *
 * Returns J1979_OK on success. On NRC (0x7F response), returns
 * J1979_ERR_NEGATIVE_RESPONSE and resp[0] holds the NRC code.
 */
j1979_status_t j1979_send(j1979_t *j1979, uint8_t mode, uint8_t pid,
                          const uint8_t *args, size_t args_len,
                          uint8_t *resp, size_t resp_buf_size,
                          size_t *out_len, uint32_t timeout_ms);

/* =========================================================================
 * Mode-specific helpers
 * ========================================================================= */

/*
 * Mode $01: Read a live-data PID. Returns payload bytes after SID+PID echo.
 */
j1979_status_t j1979_read_pid(j1979_t *j1979, uint8_t pid,
                              uint8_t *data, size_t data_buf_size,
                              size_t *out_len, uint32_t timeout_ms);

/*
 * Mode $03: Read confirmed (emission-related) DTCs into a dtc_list_t.
 */
j1979_status_t j1979_read_dtcs_confirmed(j1979_t *j1979,
                                         dtc_list_t *list,
                                         uint32_t timeout_ms);

/*
 * Mode $07: Read pending DTCs into a dtc_list_t.
 */
j1979_status_t j1979_read_dtcs_pending(j1979_t *j1979,
                                       dtc_list_t *list,
                                       uint32_t timeout_ms);

/*
 * Mode $0A: Read permanent DTCs into a dtc_list_t.
 */
j1979_status_t j1979_read_dtcs_permanent(j1979_t *j1979,
                                         dtc_list_t *list,
                                         uint32_t timeout_ms);

/*
 * Mode $04: Clear emission-related diagnostic information (DTCs).
 * Returns J1979_OK if the ECU acknowledges (response SID $44).
 */
j1979_status_t j1979_clear_dtcs(j1979_t *j1979, uint32_t timeout_ms);

/* =========================================================================
 * PID value conversion helpers (engineering units)
 * ========================================================================= */

/* Engine RPM from 2-byte PID 0x0C response: ((A*256)+B)/4 */
float j1979_pid_rpm(const uint8_t *data, size_t len);

/* Vehicle speed (km/h) from PID 0x0D response: A */
float j1979_pid_speed(const uint8_t *data, size_t len);

/* Coolant temperature (C) from PID 0x05 response: A - 40 */
float j1979_pid_coolant_temp(const uint8_t *data, size_t len);

/* Intake air temperature (C) from PID 0x0F response: A - 40 */
float j1979_pid_intake_temp(const uint8_t *data, size_t len);

/* Calculated engine load (%) from PID 0x04 response: A*100/255 */
float j1979_pid_engine_load(const uint8_t *data, size_t len);

/* MAF airflow (g/s) from PID 0x10 response: ((A*256)+B)/100 */
float j1979_pid_maf(const uint8_t *data, size_t len);

/* Throttle position (%) from PID 0x11 response: A*100/255 */
float j1979_pid_throttle(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_J1979_H */