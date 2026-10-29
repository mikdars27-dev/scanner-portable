/*
 * app/actuator_test.c — Application-layer actuator test (output / bi-directional control).
 *
 * Commands an ECU actuator (fuel pump, idle valve, EGR, injector cycle, relay)
 * for a bounded time. J1979 mode $08 is used on CAN (ISO 15765-2) and UDS
 * $2F InputOutputControlByIdentifier on K-Line (KWP2000/UDS).
 *
 * Safety: the WARN_ACTUATOR_TEST warning screen is shown before the test and
 * SELECT is required; a forced 30 s timeout applies (ACTUATOR_TEST_MAX_MS);
 * the STOP button (BACK) is polled every 100 ms during the test.
 *
 * Toolchain note: C++ (Arduino digitalRead/delay). Compile as C++.
 */

#include "actuator_test.h"

#include <Arduino.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct actuator_test {
    actuator_test_config_t cfg;
    bool                   initialized;
    volatile bool          stop_requested; /* set by actuator_test_stop() */
};

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */
#define ACTUATOR_TEST_CMD_TIMEOUT_MS 2000UL  /* per-command protocol timeout */
#define ACTUATOR_TEST_POLL_MS        100UL   /* STOP button poll interval */
#define ACTUATOR_TEST_TICK_MS         20UL   /* main loop tick */

/* J1979 mode $08 control state (first data byte). */
#define ACTUATOR_J1979_CTRL_START 0x01U
#define ACTUATOR_J1979_CTRL_STOP  0x00U

/* UDS $2F InputOutputControl operations (kline_proto_uds_io_control). */
#define ACTUATOR_UDS_OP_RETURN_TO_ECU 0x01U
#define ACTUATOR_UDS_OP_START         0x02U

/* Per-test identifiers. Exact UDS DID / J1979 PID values are ECU specific;
 * these are defaults for the DIY scanner. */
typedef struct {
    uint8_t  id;
    uint16_t uds_did;      /* K-Line UDS $2F data identifier */
    uint8_t  j1979_pid;    /* J1979 mode $08 PID */
    const char *name;
} actuator_test_entry_t;

static const actuator_test_entry_t test_table[] = {
    { ACTUATOR_TEST_ID_FUEL_PUMP,    0x0101U, 0x01U, "fuel pump"    },
    { ACTUATOR_TEST_ID_IDLE_VALVE,   0x0102U, 0x02U, "idle valve"   },
    { ACTUATOR_TEST_ID_EGR,          0x0103U, 0x03U, "EGR"          },
    { ACTUATOR_TEST_ID_INJECTOR,     0x0104U, 0x04U, "injector"     },
    { ACTUATOR_TEST_ID_RELAY,        0x0105U, 0x05U, "relay"        },
    { ACTUATOR_TEST_ID_IGNITION_COIL,0x0106U, 0x06U, "ignition coil"},
    { ACTUATOR_TEST_ID_RADIATOR_FAN, 0x0107U, 0x07U, "radiator fan" },
    { ACTUATOR_TEST_ID_FUEL_PUMP_2,  0x0108U, 0x08U, "fuel pump 2"  },
};

#define TEST_TABLE_SIZE (sizeof(test_table) / sizeof(test_table[0]))

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static const actuator_test_entry_t *actuator_test_find_entry(uint8_t test_id)
{
    for (size_t i = 0; i < TEST_TABLE_SIZE; i++) {
        if (test_table[i].id == test_id) {
            return &test_table[i];
        }
    }
    return NULL;
}

/* Send start/stop control command to the ECU over the active transport. */
static actuator_test_status_t actuator_test_command(actuator_test_t *at,
                                                    const actuator_test_entry_t *entry,
                                                    bool start)
{
    proto_transport_t tr = proto_get_transport(at->cfg.proto);
    uint8_t resp[8];
    size_t resp_len = 0;
    proto_status_t pst;

    if (tr == PROTO_TRANSPORT_CAN) {
        j1979_t *jh = proto_j1979_handle(at->cfg.proto);
        if (jh == NULL) {
            return ACTUATOR_TEST_ERR_PROTOCOL;
        }
        uint8_t arg = start ? ACTUATOR_J1979_CTRL_START : ACTUATOR_J1979_CTRL_STOP;
        pst = j1979_send(jh, J1979_MODE_CONTROL, entry->j1979_pid,
                         &arg, 1, resp, sizeof(resp), &resp_len,
                         ACTUATOR_TEST_CMD_TIMEOUT_MS);
    } else if (tr == PROTO_TRANSPORT_KLINE) {
        kline_proto_t *kh = proto_kline_handle(at->cfg.proto);
        if (kh == NULL) {
            return ACTUATOR_TEST_ERR_PROTOCOL;
        }
        uint8_t op = start ? ACTUATOR_UDS_OP_START : ACTUATOR_UDS_OP_RETURN_TO_ECU;
        pst = kline_proto_uds_io_control(kh, entry->uds_did, op,
                                         NULL, 0, resp, sizeof(resp),
                                         &resp_len, ACTUATOR_TEST_CMD_TIMEOUT_MS);
    } else {
        return ACTUATOR_TEST_ERR_PROTOCOL;
    }

    switch (pst) {
        case PROTO_OK:                    return ACTUATOR_TEST_OK;
        case PROTO_ERR_TIMEOUT:           return ACTUATOR_TEST_ERR_TIMEOUT;
        case PROTO_ERR_INVALID_ARG:       return ACTUATOR_TEST_ERR_INVALID_ARG;
        case PROTO_ERR_NOT_INITIALIZED:   return ACTUATOR_TEST_ERR_NOT_INITIALIZED;
        default:                          return ACTUATOR_TEST_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */
actuator_test_status_t actuator_test_init(const actuator_test_config_t *cfg,
                                          actuator_test_t **out)
{
    if (cfg == NULL || out == NULL || cfg->proto == NULL || cfg->disp == NULL) {
        return ACTUATOR_TEST_ERR_INVALID_ARG;
    }

    actuator_test_t *at = (actuator_test_t *)calloc(1, sizeof(*at));
    if (at == NULL) {
        return ACTUATOR_TEST_ERR_INVALID_ARG;
    }

    at->cfg         = *cfg;      /* shallow copy: proto + disp pointers */
    at->initialized = true;

    *out = at;
    return ACTUATOR_TEST_OK;
}

actuator_test_status_t actuator_test_deinit(actuator_test_t *at)
{
    if (at == NULL) {
        return ACTUATOR_TEST_ERR_INVALID_ARG;
    }
    at->initialized = false;
    free(at);
    return ACTUATOR_TEST_OK;
}

bool actuator_test_is_initialized(const actuator_test_t *at)
{
    return at != NULL && at->initialized;
}

/* ------------------------------------------------------------------------
 * Actuator test
 * ------------------------------------------------------------------------ */
actuator_test_status_t actuator_test_run(actuator_test_t *at,
                                         uint8_t test_id,
                                         uint32_t duration_ms,
                                         bool *stopped_early)
{
    if (at == NULL) return ACTUATOR_TEST_ERR_INVALID_ARG;
    if (!at->initialized) return ACTUATOR_TEST_ERR_NOT_INITIALIZED;
    if (at->cfg.proto == NULL || at->cfg.disp == NULL) {
        return ACTUATOR_TEST_ERR_INVALID_ARG;
    }

    const actuator_test_entry_t *entry = actuator_test_find_entry(test_id);
    if (entry == NULL) return ACTUATOR_TEST_ERR_INVALID_ARG;
    if (duration_ms == 0) return ACTUATOR_TEST_ERR_INVALID_ARG;

    if (stopped_early != NULL) *stopped_early = false;
    at->stop_requested = false;

    /* Safety: show warning screen and require SELECT before touching
     * any actuator. */
    if (!warning_show(at->cfg.disp, WARN_ACTUATOR_TEST, NULL,
                      ACTUATOR_TEST_MAX_MS))
    {
        return ACTUATOR_TEST_ERR_USER_ABORT;
    }

    /* Command the ECU to start controlling the actuator. */
    actuator_test_status_t st = actuator_test_command(at, entry, true);
    if (st != ACTUATOR_TEST_OK) {
        return st;
    }

    /* Run for duration_ms (capped at ACTUATOR_TEST_MAX_MS), polling the STOP
     * button (BACK) every 100 ms. millis() wraparound is safe with unsigned
     * subtraction as long as intervals stay below ~49 days. */
    uint32_t limit = duration_ms < ACTUATOR_TEST_MAX_MS
                     ? duration_ms : ACTUATOR_TEST_MAX_MS;
    uint32_t start_ms = millis();
    uint32_t next_poll = start_ms;
    bool     stopped = false;

    for (;;) {
        uint32_t now = millis();
        if ((uint32_t)(now - start_ms) >= limit) {
            break;
        }

        if ((int32_t)(now - next_poll) >= 0) {
            next_poll = now + ACTUATOR_TEST_POLL_MS;
            if ((buttons_read() & BTN_BACK) != 0 || at->stop_requested) {
                stopped = true;
                break;
            }
        }

        delay(ACTUATOR_TEST_TICK_MS);
        yield();
    }

    /* Command the ECU to return control of the actuator (best effort). */
    (void)actuator_test_command(at, entry, false);
    buttons_wait_release();

    if (stopped) {
        if (stopped_early != NULL) *stopped_early = true;
        return ACTUATOR_TEST_OK;
    }

    /* Forced cap reached before the requested duration. */
    if (duration_ms > ACTUATOR_TEST_MAX_MS &&
        (uint32_t)(millis() - start_ms) >= ACTUATOR_TEST_MAX_MS)
    {
        return ACTUATOR_TEST_ERR_TIMEOUT;
    }

    return ACTUATOR_TEST_OK;
}

actuator_test_status_t actuator_test_stop(actuator_test_t *at)
{
    if (at == NULL) return ACTUATOR_TEST_ERR_INVALID_ARG;
    if (!at->initialized) return ACTUATOR_TEST_ERR_NOT_INITIALIZED;
    at->stop_requested = true;
    return ACTUATOR_TEST_OK;
}
