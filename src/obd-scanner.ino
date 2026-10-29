/*
 * obd-scanner.ino — Main Arduino sketch for DIY OBD1/OBD2/Euro5 Scanner.
 *
 * Hardware: ESP32 DevKit V4 + MCP2515 CAN (SPI2_HOST) + K-Line PC817 (UART1)
 *           + microSD (SPI3_HOST) + TFT ILI9341 (UI layer).
 *
 * Build: PlatformIO (espressif32 / esp32dev / arduino)
 * Compiled as C++ (-x c++).
 *
 * State machine: POWER_ON -> SELF_TEST -> IDLE -> KONEKSI ->
 *                SELEKSI_PROTOKOL -> BUKA_SESI -> OPERASI ->
 *                TUTUP_SESI -> SLEEP / ERROR
 *
 * Application layer (phase 3): IDLE shows the main menu (menu_t) on the TFT;
 * OPERASI dispatches the user-selected feature to the appropriate application
 * module (DTC, live data, sensor/actuator tests, ECU backup, file manager,
 * motorcycle, UART recovery). Protocol-dependent application modules are
 * bound to the active g_proto handle when a diagnostic session is opened.
 */

#include <Arduino.h>
#include <stdio.h>

#include "config/pinout.h"
#include "config/config.h"

#include "drivers/spi.h"
#include "drivers/uart.h"
#include "drivers/kline.h"
#include "drivers/can_mcp2515.h"
#include "drivers/sd.h"

#include "ui/display.h"
#include "ui/menu.h"
#include "ui/buttons.h"
#include "ui/warning_screen.h"

#include "protocol/dtc.h"
#include "protocol/iso15765_2.h"
#include "protocol/j1979.h"
#include "protocol/kline_proto.h"
#include "protocol/protocol.h"
#include "protocol/ecu_id.h"

#include "app/power_mgmt.h"
#include "app/dtc_service.h"
#include "app/live_data.h"
#include "app/actuator_test.h"
#include "app/sensor_test.h"
#include "app/ecu_file.h"
#include "app/recovery.h"
#include "app/motorcycle.h"
#include "app/file_manager.h"
#include "app/ecu_info.h"
#include "app/freeze_frame.h"
#include "app/dtc_query.h"
#include "app/ecu_program.h"
#include "app/signal_output.h"
#include "app/abs_module.h"
#include "app/battery_test.h"
#include "app/cranking_test.h"
#include "app/charging_test.h"
#include "app/co_adjust.h"
#include "app/firmware_update.h"
#include "app/key_program.h"
#include "app/smartkey_detect.h"

/* -------------------------------------------------------------------------
 * Global handles (one active transport at a time)
 * ----------------------------------------------------------------------- */
static spi_device_t  *g_spi_can     = NULL;
static spi_device_t  *g_spi_tft     = NULL;
static display_t     *g_disp        = NULL;
static can_t         *g_can         = NULL;
static iso15765_t    *g_iso         = NULL;
static j1979_t       *g_j1979       = NULL;
static scanner_uart_t        *g_uart_kline  = NULL;
static kline_t       *g_kline       = NULL;
static kline_proto_t *g_kline_proto = NULL;
static protocol_t    *g_proto       = NULL;

/* -------------------------------------------------------------------------
 * Application layer handles (one instance per application module)
 * ----------------------------------------------------------------------- */
static dtc_service_t    *g_dtc       = NULL;
static live_data_t      *g_live_data = NULL;
static actuator_test_t  *g_actuator  = NULL;
static sensor_test_t    *g_sensor    = NULL;
static ecu_file_t       *g_ecu_file  = NULL;
static motorcycle_t     *g_moto      = NULL;
static file_manager_t   *g_fm        = NULL;
static ecu_info_t       *g_ecu_info  = NULL;
static freeze_frame_t   *g_freeze    = NULL;
static dtc_query_t      *g_dtc_q     = NULL;
static ecu_program_t    *g_ecu_prog  = NULL;
static signal_output_t  *g_signal    = NULL;
static abs_t            *g_abs       = NULL;
static battery_t        *g_battery   = NULL;
static cranking_t       *g_cranking  = NULL;
static charging_t       *g_charging  = NULL;
static co_adjust_t      *g_co_adj    = NULL;
static firmware_update_t* g_fw_upd   = NULL;
static key_prog_t         *g_key_prog  = NULL;
static smartkey_detect_t  *g_smartkey_detect = NULL;

/* -------------------------------------------------------------------------
 * State machine
 * ----------------------------------------------------------------------- */
typedef enum {
    STATE_POWER_ON,
    STATE_SELF_TEST,
    STATE_IDLE,
    STATE_KONEKSI,
    STATE_SELEKSI_PROTOKOL,
    STATE_BUKA_SESI,
    STATE_OPERASI,
    STATE_TUTUP_SESI,
    STATE_SLEEP,
    STATE_ERROR
} scanner_state_t;

static scanner_state_t g_state   = STATE_POWER_ON;
static uint8_t         g_retry   = 0;
static bool            g_ignition = false;

/* -------------------------------------------------------------------------
 * Application features selectable from the main menu (STATE_IDLE).
 * ----------------------------------------------------------------------- */
typedef enum {
    FEATURE_NONE = 0,
    FEATURE_READ_DTC,
    FEATURE_CLEAR_DTC,
    FEATURE_LIVE_DATA,
    FEATURE_SENSOR_TEST,
    FEATURE_ACTUATOR_PUMP,
    FEATURE_ACTUATOR_INJECTOR,
    FEATURE_ACTUATOR_RELAY,
    FEATURE_ECU_BACKUP,
    FEATURE_FILE_MANAGER,
    FEATURE_MOTORCYCLE,
    FEATURE_RECOVERY,
    /* --- 12 fitur tambahan (Fase 2-4) --- */
    FEATURE_ECU_INFO,               /* 1: Info Sistem ECU */
    FEATURE_FREEZE_FRAME,           /* 2: Data Bekukan (Freeze Frame) */
    FEATURE_DTC_QUERY,              /* 3: Cari Kode DTC */
    FEATURE_ECU_PROGRAM,            /* 4: Program ECU */
    FEATURE_SIGNAL_OUTPUT,          /* 5: Keluaran Sinyal */
    FEATURE_ACTUATOR_IGNITION_COIL, /* 6a: Uji Coil Pembuangan */
    FEATURE_ACTUATOR_RADIATOR_FAN,  /* 6b: Uji Kipas Radiator */
    FEATURE_ACTUATOR_FUEL_PUMP_2,   /* 6c: Uji Pompa Bahan Bakar 2 */
    FEATURE_ABS_MODULE,             /* 7: Modul ABS */
    FEATURE_BATTERY_TEST,           /* 8: Tes Baterai */
    FEATURE_CRANKING_TEST,          /* 9: Tes Cranking */
    FEATURE_CHARGING_TEST,          /* 10: Tes Pengisian Daya */
    FEATURE_CO_ADJUST,              /* 11: Penyesuaian CO Idle */
    FEATURE_FIRMWARE_UPDATE,        /* 12: Pembaruan Firmware/DB */
    FEATURE_KEY_PROGRAM,            /* 13: Program Kunci (Smartkey) */
    FEATURE_SMARTKEY_DETECT,        /* 14: Deteksi Smart Key */
} feature_id_t;

static feature_id_t   g_feature      = FEATURE_NONE;
static bool           g_idle_redraw  = true;
static bool           g_connect_menu = false;
static menu_t         g_menu;

/* -------------------------------------------------------------------------
 * Stub functions — implemented in later phases (UI / Application layer)
 * ----------------------------------------------------------------------- */
static bool self_test_all(void)
{
    /* Check display (UI layer, phase 2) */
    if (g_disp == NULL || !display_is_initialized(g_disp)) {
        Serial.println(F("[SELF-TEST] Display belum siap"));
        return false;
    }

    (void)g_can;
    (void)g_kline;
    (void)g_iso;
    (void)g_j1979;
    (void)g_kline_proto;
    (void)g_proto;
    return true;
}

static bool detect_protocol_auto(void)
{
    /* --- Try K-Line first (dominant for motorcycles and early OBD2) --- */
    uart_config_t ucfg = {};
    ucfg.port          = UART_PORT_1;
    ucfg.tx_gpio       = KLINE_TX_GPIO;
    ucfg.rx_gpio       = KLINE_RX_GPIO;
    ucfg.baud          = KLINE_BAUD_DATA;
    ucfg.invert_tx     = KLINE_INVERT_LOGIC;
    ucfg.invert_rx     = KLINE_INVERT_LOGIC;
    ucfg.rx_timeout_ms = KLINE_TIMEOUT_MS;

    if (uart_init(&ucfg, &g_uart_kline) != UART_OK) {
        goto try_can;
    }

    kline_config_t kcfg = KLINE_CONFIG_DEFAULT;
    kcfg.uart_port    = UART_PORT_1;
    kcfg.tx_gpio      = KLINE_TX_GPIO;
    kcfg.rx_gpio      = KLINE_RX_GPIO;
    kcfg.baud_data    = KLINE_BAUD_DATA;
    kcfg.wakeup_pulse_ms = KLINE_WAKEUP_PULSE_MS;
    kcfg.init_bit_ms    = KLINE_INIT_BIT_MS;
    kcfg.timeout_ms     = KLINE_TIMEOUT_MS;
    kcfg.invert_logic   = KLINE_INVERT_LOGIC;

    if (kline_init(&kcfg, &g_kline) != KLINE_OK) {
        goto try_can;
    }

    /* Wake-up + 5-baud init + switch to 10400 baud data phase */
    if (kline_begin(g_kline) != KLINE_OK) {
        goto try_can;
    }

    /* K-Line OK — initialize KWP2000/UDS over K-Line */
    kline_proto_config_t kpcfg = KLINE_PROTO_CONFIG_DEFAULT(g_kline);
    if (kline_proto_init(&kpcfg, &g_kline_proto) != KLINE_PROTO_OK) {
        goto try_can;
    }

    proto_config_t pcfg = {};
    pcfg.transport = PROTO_TRANSPORT_KLINE;
    pcfg.kline     = g_kline_proto;
    if (proto_init(&pcfg, &g_proto) != PROTO_OK) {
        goto try_can;
    }

    Serial.println(F("Protokol terdeteksi: K-Line (KWP2000/UDS)"));
    return true;

try_can:
    /* --- Try CAN ISO 15765-4 (OBD2/Euro5 cars) --- */
    spi_device_config_t scfg = {};
    scfg.host        = SPI_HOST_2;
    scfg.cs_gpio     = CAN_CS_GPIO;
    scfg.clock_hz    = 1000000UL;   /* MCP2515 max 1 MHz SPI */
    scfg.mode        = SPI_MODE_0;
    scfg.bit_order   = SPI_BIT_ORDER_MSB;
    scfg.cs_active_low = true;
    scfg.dummy_bits  = 0;

    if (spi_device_init(&scfg, &g_spi_can) != SPI_OK) {
        return false;
    }

    can_config_t ccfg = CAN_CONFIG_DEFAULT;
    ccfg.spi_dev   = g_spi_can;
    ccfg.int_gpio  = CAN_INT_GPIO;
    ccfg.baudrate  = CAN_BAUDRATE;
    ccfg.loopback  = false;

    if (can_init(&ccfg, &g_can) != CAN_OK) {
        return false;
    }

    iso15765_config_t isocfg = ISO15765_CONFIG_DEFAULT(
        g_can, CAN_ID_PHYS_BASE, CAN_ID_RESP_BASE);
    if (iso15765_init(&isocfg, &g_iso) != ISO15765_OK) {
        return false;
    }

    j1979_config_t jcfg = J1979_CONFIG_DEFAULT(g_iso);
    if (j1979_init(&jcfg, &g_j1979) != J1979_OK) {
        return false;
    }

    proto_config_t pcfg2 = {};
    pcfg2.transport = PROTO_TRANSPORT_CAN;
    pcfg2.can_iso   = g_iso;
    if (proto_init(&pcfg2, &g_proto) != PROTO_OK) {
        return false;
    }

    Serial.println(F("Protokol terdeteksi: CAN (J1979/OBD2)"));
    return true;
}

/* -------------------------------------------------------------------------
 * Application layer: bind application modules to the active protocol.
 *
 * Called after the display is ready (setup) and every time a diagnostic
 * session is opened (STATE_BUKA_SESI / run_selected_feature). Previous
 * handles are released first, so re-calling is safe.
 * ----------------------------------------------------------------------- */
static void app_modules_bind_protocol(protocol_t *proto)
{
    /* Release previous handles first (re-init safe). */
    if (g_dtc)       { dtc_service_deinit(g_dtc);       g_dtc       = NULL; }
    if (g_live_data) { live_data_deinit(g_live_data);   g_live_data = NULL; }
    if (g_actuator)  { actuator_test_deinit(g_actuator); g_actuator  = NULL; }
    if (g_sensor)    { sensor_test_deinit(g_sensor);     g_sensor    = NULL; }
    if (g_moto)      { motorcycle_deinit(g_moto);        g_moto      = NULL; }
    if (g_ecu_info)  { ecu_info_deinit(g_ecu_info);     g_ecu_info  = NULL; }
    if (g_freeze)    { freeze_frame_deinit(g_freeze);    g_freeze   = NULL; }
    if (g_ecu_prog)  { ecu_program_deinit(g_ecu_prog);  g_ecu_prog = NULL; }
    if (g_signal)    { signal_output_deinit(g_signal);   g_signal   = NULL; }
    if (g_abs)       { abs_deinit(g_abs);               g_abs      = NULL; }
    if (g_co_adj)    { co_adjust_deinit(g_co_adj);      g_co_adj   = NULL; }
    if (g_key_prog)  { key_prog_deinit(g_key_prog);     g_key_prog = NULL; }
    if (g_smartkey_detect) { smartkey_detect_deinit(g_smartkey_detect); g_smartkey_detect = NULL; }

    if (proto == NULL) {
        return;
    }

    dtc_service_config_t dtccfg = DTC_SERVICE_CONFIG_DEFAULT(proto, NULL);
    if (dtc_service_init(&dtccfg, &g_dtc) != DTC_SERVICE_OK) {
        g_dtc = NULL;
    }

    live_data_config_t ldcfg = LIVE_DATA_CONFIG_DEFAULT(proto);
    if (live_data_init(&ldcfg, &g_live_data) != LIVE_DATA_OK) {
        g_live_data = NULL;
    }

    actuator_test_config_t atcfg = ACTUATOR_TEST_CONFIG_DEFAULT(proto, g_disp);
    if (actuator_test_init(&atcfg, &g_actuator) != ACTUATOR_TEST_OK) {
        g_actuator = NULL;
    }

    sensor_test_config_t stcfg = SENSOR_TEST_CONFIG_DEFAULT(proto);
    if (sensor_test_init(&stcfg, &g_sensor) != SENSOR_TEST_OK) {
        g_sensor = NULL;
    }

    motorcycle_config_t mcfg = MOTORCYCLE_CONFIG_DEFAULT(proto);
    if (motorcycle_init(&mcfg, &g_moto) != MOTORCYCLE_OK) {
        g_moto = NULL;
    }

    ecu_info_config_t eicfg = ECU_INFO_CONFIG_DEFAULT(proto);
    if (ecu_info_init(&eicfg, &g_ecu_info) != ECU_INFO_OK) {
        g_ecu_info = NULL;
    }

    freeze_frame_config_t ffcfg = FREEZE_FRAME_CONFIG_DEFAULT(proto);
    if (freeze_frame_init(&ffcfg, &g_freeze) != FREEZE_FRAME_OK) {
        g_freeze = NULL;
    }

    ecu_program_config_t epcfg = ECU_PROGRAM_CONFIG_DEFAULT(proto, g_disp);
    if (ecu_program_init(&epcfg, &g_ecu_prog) != ECU_PROGRAM_OK) {
        g_ecu_prog = NULL;
    }

    signal_output_config_t socfg = SIGNAL_OUTPUT_CONFIG_DEFAULT(proto, g_disp);
    if (signal_output_init(&socfg, &g_signal) != SIGNAL_OUTPUT_OK) {
        g_signal = NULL;
    }

    abs_config_t abcfg = ABS_CONFIG_DEFAULT(proto, g_disp);
    if (abs_init(&abcfg, &g_abs) != ABS_OK) {
        g_abs = NULL;
    }

    co_adjust_config_t cacfg = CO_ADJUST_CONFIG_DEFAULT(proto, g_disp);
    if (co_adjust_init(&cacfg, &g_co_adj) != CO_ADJUST_OK) {
        g_co_adj = NULL;
    }

    key_prog_config_t kpcfg = KEY_PROG_CONFIG_DEFAULT(proto, g_disp);
    if (key_prog_init(&kpcfg, &g_key_prog) != KEY_PROG_OK) {
        g_key_prog = NULL;
    }

    smartkey_detect_config_t skcfg = SMARTKEY_DETECT_CONFIG_DEFAULT(proto, g_disp);
    if (smartkey_detect_init(&skcfg, &g_smartkey_detect) != SMARTKEY_DETECT_OK) {
        g_smartkey_detect = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Main menu (rendered in STATE_IDLE)
 * ----------------------------------------------------------------------- */

static void menu_cb_connect(void)
{
    g_connect_menu = true;
    g_state = STATE_KONEKSI;
}

static void menu_cb_read_dtc(void)
{
    g_feature = FEATURE_READ_DTC;
    g_state = STATE_OPERASI;
}

static void menu_cb_clear_dtc(void)
{
    g_feature = FEATURE_CLEAR_DTC;
    g_state = STATE_OPERASI;
}

static void menu_cb_live_data(void)
{
    g_feature = FEATURE_LIVE_DATA;
    g_state = STATE_OPERASI;
}

static void menu_cb_sensor_test(void)
{
    g_feature = FEATURE_SENSOR_TEST;
    g_state = STATE_OPERASI;
}

static void menu_cb_actuator_pump(void)
{
    g_feature = FEATURE_ACTUATOR_PUMP;
    g_state = STATE_OPERASI;
}

static void menu_cb_actuator_injector(void)
{
    g_feature = FEATURE_ACTUATOR_INJECTOR;
    g_state = STATE_OPERASI;
}

static void menu_cb_actuator_relay(void)
{
    g_feature = FEATURE_ACTUATOR_RELAY;
    g_state = STATE_OPERASI;
}

static void menu_cb_ecu_backup(void)
{
    g_feature = FEATURE_ECU_BACKUP;
    g_state = STATE_OPERASI;
}

static void menu_cb_file_manager(void)
{
    g_feature = FEATURE_FILE_MANAGER;
    g_state = STATE_OPERASI;
}

static void menu_cb_motorcycle(void)
{
    g_feature = FEATURE_MOTORCYCLE;
    g_state = STATE_OPERASI;
}

static void menu_cb_recovery(void)
{
    g_feature = FEATURE_RECOVERY;
    g_state = STATE_OPERASI;
}

/* --- Callbacks untuk 12 fitur tambahan --- */
static void menu_cb_ecu_info(void)
{
    g_feature = FEATURE_ECU_INFO;
    g_state = STATE_OPERASI;
}

static void menu_cb_freeze_frame(void)
{
    g_feature = FEATURE_FREEZE_FRAME;
    g_state = STATE_OPERASI;
}

static void menu_cb_dtc_query(void)
{
    g_feature = FEATURE_DTC_QUERY;
    g_state = STATE_OPERASI;
}

static void menu_cb_ecu_program(void)
{
    g_feature = FEATURE_ECU_PROGRAM;
    g_state = STATE_OPERASI;
}

static void menu_cb_signal_output(void)
{
    g_feature = FEATURE_SIGNAL_OUTPUT;
    g_state = STATE_OPERASI;
}

static void menu_cb_actuator_ignition_coil(void)
{
    g_feature = FEATURE_ACTUATOR_IGNITION_COIL;
    g_state = STATE_OPERASI;
}

static void menu_cb_actuator_radiator_fan(void)
{
    g_feature = FEATURE_ACTUATOR_RADIATOR_FAN;
    g_state = STATE_OPERASI;
}

static void menu_cb_actuator_fuel_pump_2(void)
{
    g_feature = FEATURE_ACTUATOR_FUEL_PUMP_2;
    g_state = STATE_OPERASI;
}

static void menu_cb_abs_module(void)
{
    g_feature = FEATURE_ABS_MODULE;
    g_state = STATE_OPERASI;
}

static void menu_cb_battery_test(void)
{
    g_feature = FEATURE_BATTERY_TEST;
    g_state = STATE_OPERASI;
}

static void menu_cb_cranking_test(void)
{
    g_feature = FEATURE_CRANKING_TEST;
    g_state = STATE_OPERASI;
}

static void menu_cb_charging_test(void)
{
    g_feature = FEATURE_CHARGING_TEST;
    g_state = STATE_OPERASI;
}

static void menu_cb_co_adjust(void)
{
    g_feature = FEATURE_CO_ADJUST;
    g_state = STATE_OPERASI;
}

static void menu_cb_firmware_update(void)
{
    g_feature = FEATURE_FIRMWARE_UPDATE;
    g_state = STATE_OPERASI;
}

static void menu_cb_key_program(void)
{
    g_feature = FEATURE_KEY_PROGRAM;
    g_state = STATE_OPERASI;
}

static void menu_cb_smartkey_detect(void)
{
    g_feature = FEATURE_SMARTKEY_DETECT;
    g_state = STATE_OPERASI;
}

static const menu_item_t g_menu_items[] = {
    { "Koneksi / Deteksi Protokol", menu_cb_connect },
    { "Baca DTC",                   menu_cb_read_dtc },
    { "Hapus DTC",                  menu_cb_clear_dtc },
    { "Data Hidup (PID)",           menu_cb_live_data },
    { "Uji Semua Sensor",           menu_cb_sensor_test },
    { "Uji Pompa Bahan Bakar",      menu_cb_actuator_pump },
    { "Uji Injektor",               menu_cb_actuator_injector },
    { "Uji Relai",                  menu_cb_actuator_relay },
    { "Backup ECU ke SD",           menu_cb_ecu_backup },
    { "Manajer File SD",            menu_cb_file_manager },
    { "Motor: Deteksi Merek",       menu_cb_motorcycle },
    { "Pemulihan UART",             menu_cb_recovery },
    /* --- 12 fitur tambahan --- */
    { "Info Sistem ECU",            menu_cb_ecu_info },
    { "Data Bekukan",               menu_cb_freeze_frame },
    { "Cari Kode DTC",              menu_cb_dtc_query },
    { "Program ECU",                menu_cb_ecu_program },
    { "Keluaran Sinyal",            menu_cb_signal_output },
    { "Uji Coil Pembuangan",        menu_cb_actuator_ignition_coil },
    { "Uji Kipas Radiator",         menu_cb_actuator_radiator_fan },
    { "Uji Pompa Bahan Bakar 2",    menu_cb_actuator_fuel_pump_2 },
    { "Modul ABS",                  menu_cb_abs_module },
    { "Tes Baterai",                menu_cb_battery_test },
    { "Tes Cranking",               menu_cb_cranking_test },
    { "Tes Pengisian Daya",         menu_cb_charging_test },
    { "Penyesuaian CO Idle",        menu_cb_co_adjust },
    { "Pembaruan Firmware/DB",      menu_cb_firmware_update },
    { "Program Kunci (Smartkey)",   menu_cb_key_program },
    { "Deteksi Smart Key",          menu_cb_smartkey_detect },
};
#define MENU_ITEM_COUNT (sizeof(g_menu_items) / sizeof(g_menu_items[0]))

/* -------------------------------------------------------------------------
 * Display helpers
 * ----------------------------------------------------------------------- */

/* Show a short info message (no buttons) for ms milliseconds. */
static void app_show_info(const char *title, const char *msg, uint16_t ms)
{
    warning_info(g_disp, title, msg);
    delay(ms);
}

/* Wait until the user presses BACK (or ignition is off) before returning. */
static void app_wait_for_back(void)
{
    display_draw_text(g_disp, 10, DISPLAY_HEIGHT - 20,
                      "BACK = kembali", DISPLAY_COLOR_GRAY, 1);

    for (;;) {
        uint8_t btn = buttons_read();
        if (btn & BTN_BACK) {
            buttons_wait_release();
            break;
        }
        if (!g_ignition || digitalRead(IGNITION_SENSE_GPIO) == LOW) {
            break;
        }
        delay(50);
        yield();
    }
}

/* -------------------------------------------------------------------------
 * Feature runners — each feature calls the appropriate application module.
 * ----------------------------------------------------------------------- */

static void app_feature_read_dtc(void)
{
    if (!dtc_service_is_initialized(g_dtc)) {
        app_show_info("DTC", "Layanan belum siap", 1500);
        return;
    }

    display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
    display_draw_text(g_disp, 10, 20, "DTC Terkonfirmasi", DISPLAY_COLOR_CYAN, 1);

    dtc_entry_t entries[8];
    dtc_list_t  list = {};
    list.entries  = entries;
    list.capacity = sizeof(entries) / sizeof(entries[0]);

    if (dtc_service_read_dtcs(g_dtc, J1979_MODE_DTC_CONFIRMED, &list, 5000)
        != DTC_SERVICE_OK)
    {
        app_show_info("DTC", "Gagal membaca DTC", 2000);
        return;
    }

    uint8_t y = 40;
    if (list.count == 0) {
        display_draw_text(g_disp, 10, y, "Tidak ada DTC",
                          DISPLAY_COLOR_GREEN, 1);
    } else {
        for (size_t i = 0; i < list.count && y < DISPLAY_HEIGHT - 24; i++) {
            char line[96];
            if (dtc_service_format_entry(&list.entries[i], line, sizeof(line))
                == DTC_SERVICE_OK)
            {
                display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
                y += 16;
            }
        }
    }

    app_wait_for_back();
}

static void app_feature_clear_dtc(void)
{
    if (!dtc_service_is_initialized(g_dtc)) {
        app_show_info("DTC", "Layanan belum siap", 1500);
        return;
    }

    if (!warning_show(g_disp, WARN_CLEAR_DTC, NULL, 5000)) {
        app_show_info("DTC", "Dibatalkan", 1000);
        return;
    }

    if (dtc_service_clear_dtcs(g_dtc, 5000) == DTC_SERVICE_OK) {
        app_show_info("DTC", "DTC dihapus", 1500);
    } else {
        app_show_info("DTC", "Gagal menghapus DTC", 1500);
    }
}

static void app_feature_live_data(void)
{
    if (!live_data_is_initialized(g_live_data)) {
        app_show_info("Live Data", "Layanan belum siap", 1500);
        return;
    }

    static const uint8_t k_pids[] = {
        J1979_PID_ENGINE_RPM,
        J1979_PID_VEHICLE_SPEED,
        J1979_PID_COOLANT_TEMP,
    };

    /* Show several PIDs repeatedly until BACK is pressed or ignition off. */
    for (;;) {
        if (!g_ignition || digitalRead(IGNITION_SENSE_GPIO) == LOW) {
            break;
        }

        display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
        display_draw_text(g_disp, 10, 20, "Data Hidup (PID)",
                          DISPLAY_COLOR_CYAN, 1);

        uint8_t y = 50;
        for (size_t j = 0; j < sizeof(k_pids) / sizeof(k_pids[0]); j++) {
            char buf[64];
            live_data_status_t st =
                live_data_read_pid(g_live_data, k_pids[j],
                                   buf, sizeof(buf), 1000);
            if (st == LIVE_DATA_OK) {
                display_draw_text(g_disp, 10, y, buf,
                                  DISPLAY_COLOR_WHITE, 1);
            } else {
                display_draw_text(g_disp, 10, y,
                                  "(tidak tersedia)", DISPLAY_COLOR_GRAY, 1);
            }
            y += 18;
        }

        display_draw_text(g_disp, 10, DISPLAY_HEIGHT - 20,
                          "BACK = kembali", DISPLAY_COLOR_GRAY, 1);

        uint8_t btn = buttons_read();
        if (btn & BTN_BACK) {
            buttons_wait_release();
            break;
        }
        delay(300);
    }
}

static void app_feature_sensor_test(void)
{
    if (!sensor_test_is_initialized(g_sensor)) {
        app_show_info("Sensor", "Layanan belum siap", 1500);
        return;
    }

    static const uint8_t k_tests[] = {
        SENSOR_TEST_O2_BANK1_S1,
        SENSOR_TEST_O2_BANK1_S2,
        SENSOR_TEST_CATALYST_MONITOR,
        SENSOR_TEST_EVAP_MONITOR,
    };
    static const char *k_labels[] = { "O2 B1S1", "O2 B1S2", "Katalis", "EVAP" };

    display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
    display_draw_text(g_disp, 10, 20, "Uji Sensor", DISPLAY_COLOR_CYAN, 1);

    uint8_t y = 40;
    for (size_t i = 0; i < sizeof(k_tests) / sizeof(k_tests[0]); i++) {
        char buf[80];
        sensor_test_status_t st =
            sensor_test_run(g_sensor, k_tests[i], buf, sizeof(buf), 5000);

        char line[128];
        if (st == SENSOR_TEST_OK) {
            snprintf(line, sizeof(line), "%s: %s", k_labels[i], buf);
        } else {
            snprintf(line, sizeof(line), "%s: (error %d)", k_labels[i],
                     (int)st);
        }
        display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
        y += 18;
    }

    app_wait_for_back();
}

static void app_feature_actuator(uint8_t test_id)
{
    if (!actuator_test_is_initialized(g_actuator)) {
        app_show_info("Aktuator", "Layanan belum siap", 1500);
        return;
    }

    /* The actuator_test module itself shows the warning screen
     * (WARN_ACTUATOR_TEST) and polls the STOP button (BACK) during the test. */
    bool stopped_early = false;
    actuator_test_status_t st =
        actuator_test_run(g_actuator, test_id, 10000, &stopped_early);

    const char *msg;
    if (st == ACTUATOR_TEST_OK) {
        msg = stopped_early ? "Dihentikan pengguna" : "Selesai";
    } else {
        msg = "Gagal / ditolak pengguna";
    }
    app_show_info("Aktuator", msg, 1500);
}

static void app_feature_ecu_backup(void)
{
    if (g_ecu_file == NULL || g_proto == NULL) {
        app_show_info("ECU", "Layanan belum siap", 1500);
        return;
    }

    ecu_file_status_t st =
        ecu_file_backup(g_ecu_file, "ECU_MAIN", g_proto, 5000);

    if (st == ECU_FILE_OK) {
        app_show_info("ECU", "Backup tersimpan di SD", 2000);
    } else {
        app_show_info("ECU", "Backup gagal", 2000);
    }
}

static void app_feature_file_manager(void)
{
    if (g_fm == NULL) {
        app_show_info("File", "Layanan belum siap", 1500);
        return;
    }

    char path[128] = "";
    file_manager_status_t st =
        file_manager_browse(g_fm, "/", path, sizeof(path), 0);

    char msg[160];
    if (st == FILE_MANAGER_OK) {
        snprintf(msg, sizeof(msg), "Terpilih: %s", path);
    } else if (st == FILE_MANAGER_ERR_USER_CANCEL) {
        snprintf(msg, sizeof(msg), "Dibatalkan");
    } else {
        snprintf(msg, sizeof(msg), "Gagal (error %d)", (int)st);
    }
    app_show_info("File Manager", msg, 2000);
}

static void app_feature_motorcycle(void)
{
    if (g_moto == NULL || g_proto == NULL) {
        app_show_info("Motor", "Layanan belum siap", 1500);
        return;
    }

    moto_brand_t brand = MOTO_BRAND_GENERIC;
    motorcycle_status_t st =
        motorcycle_detect_brand(g_moto, &brand, 5000);

    const char *nama = "Tidak diketahui";
    if (st == MOTORCYCLE_OK) {
        switch (brand) {
        case MOTO_BRAND_HONDA:    nama = "Honda";    break;
        case MOTO_BRAND_YAMAHA:   nama = "Yamaha";   break;
        case MOTO_BRAND_SUZUKI:   nama = "Suzuki";   break;
        case MOTO_BRAND_KAWASAKI: nama = "Kawasaki"; break;
        default:                  nama = "Generik";  break;
        }
    }

    char msg[80];
    snprintf(msg, sizeof(msg), "Merek: %s", nama);
    app_show_info("Motor", msg, 2000);
}

static void app_feature_recovery(void)
{
    recovery_status_t st = recovery_enter(10000);

    char msg[64];
    snprintf(msg, sizeof(msg), "Kode: %d", (int)st);
    app_show_info("Pemulihan UART", msg, 2000);
}

/* -------------------------------------------------------------------------
 * Feature 1: Info Sistem ECU
 * ----------------------------------------------------------------------- */
static void app_feature_ecu_info(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("ECU Info", "Layanan belum siap", 1500);
        return;
    }

    if (!ecu_info_is_initialized(g_ecu_info)) {
        ecu_info_config_t cfg = ECU_INFO_CONFIG_DEFAULT(g_proto);
        if (ecu_info_init(&cfg, &g_ecu_info) != ECU_INFO_OK) {
            app_show_info("ECU Info", "Gagal inisialisasi", 1500);
            return;
        }
    }

    char buf[64];

    display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
    display_draw_text(g_disp, 10, 20, "Info Sistem ECU", DISPLAY_COLOR_CYAN, 1);

    uint8_t y = 40;

    if (ecu_info_read_vin(g_ecu_info, buf, sizeof(buf), 5000) == ECU_INFO_OK) {
        char line[128];
        snprintf(line, sizeof(line), "VIN: %s", buf);
        display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
        y += 16;
    }

    if (ecu_info_read_ecu_name(g_ecu_info, buf, sizeof(buf), 5000) == ECU_INFO_OK) {
        char line[128];
        snprintf(line, sizeof(line), "ECU: %s", buf);
        display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
        y += 16;
    }

    if (ecu_info_read_sw_version(g_ecu_info, buf, sizeof(buf), 5000) == ECU_INFO_OK) {
        char line[128];
        snprintf(line, sizeof(line), "SW Ver: %s", buf);
        display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
    }

    app_wait_for_back();
}

/* -------------------------------------------------------------------------
 * Feature 2: Freeze Frame
 * ----------------------------------------------------------------------- */
static void app_feature_freeze_frame(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("Freeze", "Layanan belum siap", 1500);
        return;
    }

    if (!freeze_frame_is_initialized(g_freeze)) {
        freeze_frame_config_t cfg = FREEZE_FRAME_CONFIG_DEFAULT(g_proto);
        if (freeze_frame_init(&cfg, &g_freeze) != FREEZE_FRAME_OK) {
            app_show_info("Freeze", "Gagal inisialisasi", 1500);
            return;
        }
    }

    char buf[64];

    display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
    display_draw_text(g_disp, 10, 20, "Data Bekukan", DISPLAY_COLOR_CYAN, 1);

    uint8_t y = 40;

    if (freeze_frame_read_trigger_dtc(g_freeze, buf, sizeof(buf), 5000)
        == FREEZE_FRAME_OK)
    {
        char line[128];
        snprintf(line, sizeof(line), "DTC Pemicu: %s", buf);
        display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
        y += 16;
    } else {
        display_draw_text(g_disp, 10, y, "DTC Pemicu: (tidak ada)",
                          DISPLAY_COLOR_GRAY, 1);
        y += 16;
    }

    /* Read a few common frozen PIDs. */
    static const uint8_t k_ff_pids[] = {
        0x0C, /* RPM */
        0x0D, /* Speed */
        0x05, /* Coolant temp */
        0x04, /* Engine load */
    };

    for (size_t i = 0;
         i < sizeof(k_ff_pids) / sizeof(k_ff_pids[0]) && y < DISPLAY_HEIGHT - 24;
         i++)
    {
        if (freeze_frame_read_pid(g_freeze, k_ff_pids[i],
                                  buf, sizeof(buf), 2000)
            == FREEZE_FRAME_OK)
        {
            display_draw_text(g_disp, 10, y, buf, DISPLAY_COLOR_WHITE, 1);
        } else {
            display_draw_text(g_disp, 10, y, "(tidak tersedia)",
                              DISPLAY_COLOR_GRAY, 1);
        }
        y += 16;
    }

    app_wait_for_back();
}

/* -------------------------------------------------------------------------
 * Feature 3: DTC Query
 * ----------------------------------------------------------------------- */
static void app_feature_dtc_query(void)
{
    if (!dtc_query_is_initialized(g_dtc_q)) {
        dtc_query_config_t cfg = DTC_QUERY_CONFIG_DEFAULT;
        if (dtc_query_init(&cfg, &g_dtc_q) != DTC_QUERY_OK) {
            app_show_info("DTC Query", "Gagal inisialisasi", 1500);
            return;
        }
    }

    display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
    display_draw_text(g_disp, 10, 20, "Cari Kode DTC", DISPLAY_COLOR_CYAN, 1);

    /* Show lookup for a few common DTCs as examples. */
    static const char *k_common_dtcs[] = {
        "P0300", "P0171", "P0420", "P0301", "P0133",
    };
    static const char *k_common_desc[] = {
        "Kegagalan pembakaran acak/beberapa silinder",
        "Sistem terlalu kurus (Bank 1)",
        "Katalis efisiensi di bawah ambang (Bank 1)",
        "Kegagalan pembakaran silinder 1",
        "Respon lambat O2 sensor (Bank 1 Sensor 1)",
    };

    uint8_t y = 40;
    for (size_t i = 0; i < sizeof(k_common_dtcs) / sizeof(k_common_dtcs[0]); i++) {
        char desc[96];
        if (dtc_query_lookup(g_dtc_q, k_common_dtcs[i],
                             desc, sizeof(desc)) == DTC_QUERY_OK)
        {
            char line[128];
            snprintf(line, sizeof(line), "%s: %s", k_common_dtcs[i], desc);
            display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
            y += 16;
        }
    }

    app_wait_for_back();
}

/* -------------------------------------------------------------------------
 * Feature 4: ECU Program
 * ----------------------------------------------------------------------- */
static void app_feature_ecu_program(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("ECU Program", "Layanan belum siap", 1500);
        return;
    }

    if (!ecu_program_is_initialized(g_ecu_prog)) {
        ecu_program_config_t cfg = ECU_PROGRAM_CONFIG_DEFAULT(g_proto, g_disp);
        if (ecu_program_init(&cfg, &g_ecu_prog) != ECU_PROGRAM_OK) {
            app_show_info("ECU Program", "Gagal inisialisasi", 1500);
            return;
        }
    }

    /* Reset ECU (UDS $11) — requires WARN_ECU_FLASH confirmation. */
    if (!warning_show(g_disp, WARN_ECU_FLASH, "Reset ECU?", 10000)) {
        app_show_info("ECU Program", "Dibatalkan", 1000);
        return;
    }

    ecu_program_status_t st =
        ecu_program_reset(g_ecu_prog, 5000);

    if (st == ECU_PROGRAM_OK) {
        app_show_info("ECU Program", "ECU direstart", 2000);
    } else {
        app_show_info("ECU Program", "Gagal mereset ECU", 2000);
    }
}

/* -------------------------------------------------------------------------
 * Feature 5: Signal Output
 * ----------------------------------------------------------------------- */
static void app_feature_signal_output(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("Sinyal", "Layanan belum siap", 1500);
        return;
    }

    if (!signal_output_is_initialized(g_signal)) {
        signal_output_config_t cfg =
            SIGNAL_OUTPUT_CONFIG_DEFAULT(g_proto, g_disp);
        if (signal_output_init(&cfg, &g_signal) != SIGNAL_OUTPUT_OK) {
            app_show_info("Sinyal", "Gagal inisialisasi", 1500);
            return;
        }
    }

    if (!warning_show(g_disp, WARN_ACTUATOR_TEST, "Keluarkan sinyal?", 10000)) {
        app_show_info("Sinyal", "Dibatalkan", 1000);
        return;
    }

    /* Command a test output on DID/PID 0x10 (J1979 Mode $08) or UDS DID 0x0110. */
    signal_output_status_t st =
        signal_output_set(g_signal, 0x10, 0x02, NULL, 0, 5000);

    if (st == SIGNAL_OUTPUT_OK) {
        app_show_info("Sinyal", "Sinyal dikirim", 1500);
    } else {
        app_show_info("Sinyal", "Gagal mengirim sinyal", 1500);
    }

    /* Release control back to ECU. */
    (void)signal_output_release(g_signal, 0x10, 5000);
}

/* -------------------------------------------------------------------------
 * Feature 6 (sub-items): Aktuator Lain — handled via app_feature_actuator()
 * in run_selected_feature() with ACTUATOR_TEST_ID_IGNITION_COIL / RADIATOR_FAN /
 * FUEL_PUMP_2. No separate function needed.
 * ----------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Feature 7: Modul ABS
 * ----------------------------------------------------------------------- */
static void app_feature_abs_module(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("ABS", "Layanan belum siap", 1500);
        return;
    }

    if (!abs_is_initialized(g_abs)) {
        abs_config_t cfg = ABS_CONFIG_DEFAULT(g_proto, g_disp);
        if (abs_init(&cfg, &g_abs) != ABS_OK) {
            app_show_info("ABS", "Gagal inisialisasi", 1500);
            return;
        }
    }

    dtc_entry_t entries[8];
    dtc_list_t  list = {};
    list.entries  = entries;
    list.capacity = sizeof(entries) / sizeof(entries[0]);

    if (abs_read_dtcs(g_abs, &list, 5000) == ABS_OK) {
        display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
        display_draw_text(g_disp, 10, 20, "DTC ABS", DISPLAY_COLOR_CYAN, 1);

        uint8_t y = 40;
        if (list.count == 0) {
            display_draw_text(g_disp, 10, y, "Tidak ada DTC ABS",
                              DISPLAY_COLOR_GREEN, 1);
        } else {
            for (size_t i = 0; i < list.count && y < DISPLAY_HEIGHT - 24; i++) {
                char line[96];
                snprintf(line, sizeof(line), "%s", entries[i].code);
                display_draw_text(g_disp, 10, y, line, DISPLAY_COLOR_WHITE, 1);
                y += 16;
            }
        }
    } else {
        app_show_info("ABS", "Gagal membaca DTC ABS", 2000);
        return;
    }

    app_wait_for_back();
}

/* -------------------------------------------------------------------------
 * Feature 8: Deteksi Baterai
 * ----------------------------------------------------------------------- */
static void app_feature_battery_test(void)
{
    if (!battery_is_initialized(g_battery)) {
        battery_config_t cfg = BATTERY_CONFIG_DEFAULT;
        cfg.disp = g_disp;
        if (battery_init(&cfg, &g_battery) != BATTERY_OK) {
            app_show_info("Baterai", "Gagal inisialisasi", 1500);
            return;
        }
    }

    float voltage = 0.0f;
    battery_status_t st = battery_read_voltage(g_battery, &voltage, 5000);

    char msg[96];
    if (st == BATTERY_OK) {
        snprintf(msg, sizeof(msg), "Tegangan: %.2f V", voltage);
        app_show_info("Baterai", msg, 3000);
    } else {
        app_show_info("Baterai", "Gagal membaca tegangan", 2000);
    }
}

/* -------------------------------------------------------------------------
 * Feature 9: Tes Cranking
 * ----------------------------------------------------------------------- */
static void app_feature_cranking_test(void)
{
    if (!cranking_is_initialized(g_cranking)) {
        cranking_config_t cfg = CRANKING_CONFIG_DEFAULT;
        if (cranking_init(&cfg, &g_cranking) != CRANKING_OK) {
            app_show_info("Cranking", "Gagal inisialisasi", 1500);
            return;
        }
    }

    if (!warning_show(g_disp, WARN_CUSTOM,
                      "Nyalakan starter, tekan SELECT", 15000))
    {
        app_show_info("Cranking", "Dibatalkan", 1000);
        return;
    }

    float min_v = 0.0f, max_v = 0.0f, drop_v = 0.0f;
    cranking_status_t st =
        cranking_run(g_cranking, &min_v, &max_v, &drop_v, 30000);

    char msg[96];
    if (st == CRANKING_OK) {
        snprintf(msg, sizeof(msg), "Min: %.2f V  Drop: %.2f V", min_v, drop_v);
        app_show_info("Cranking", msg, 3000);
    } else {
        app_show_info("Cranking", "Gagal / dibatalkan", 2000);
    }
}

/* -------------------------------------------------------------------------
 * Feature 10: Tes Pengisian Daya
 * ----------------------------------------------------------------------- */
static void app_feature_charging_test(void)
{
    if (!charging_is_initialized(g_charging)) {
        charging_config_t cfg = CHARGING_CONFIG_DEFAULT;
        cfg.disp = g_disp;
        if (charging_init(&cfg, &g_charging) != CHARGING_OK) {
            app_show_info("Charging", "Gagal inisialisasi", 1500);
            return;
        }
    }

    float idle_v = 0.0f, revved_v = 0.0f;
    charging_status_t st =
        charging_run(g_charging, &idle_v, &revved_v, 10000);

    char msg[96];
    if (st == CHARGING_OK) {
        snprintf(msg, sizeof(msg), "Idle: %.2f V  Rev: %.2f V", idle_v, revved_v);
        app_show_info("Charging", msg, 3000);
    } else {
        app_show_info("Charging", "Gagal / dibatalkan", 2000);
    }
}

/* -------------------------------------------------------------------------
 * Feature 11: Penyesuaian CO Idle
 * ----------------------------------------------------------------------- */
static void app_feature_co_adjust(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("CO Idle", "Layanan belum siap", 1500);
        return;
    }

    if (!co_adjust_is_initialized(g_co_adj)) {
        co_adjust_config_t cfg = CO_ADJUST_CONFIG_DEFAULT(g_proto, g_disp);
        if (co_adjust_init(&cfg, &g_co_adj) != CO_ADJUST_OK) {
            app_show_info("CO Idle", "Gagal inisialisasi", 1500);
            return;
        }
    }

    if (!warning_show(g_disp, WARN_CUSTOM,
                      "Ubah campuran idle?", 10000))
    {
        app_show_info("CO Idle", "Dibatalkan", 1000);
        return;
    }

    /* Read current mixture first. */
    int8_t current = 0;
    if (co_adjust_read_mixture(g_co_adj, &current, 5000) == CO_ADJUST_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Saat ini: %d", (int)current);
        app_show_info("CO Idle", msg, 1500);
    }

    /* Set back to stock (0). */
    co_adjust_status_t st = co_adjust_set_mixture(g_co_adj, 0, 5000);
    if (st == CO_ADJUST_OK) {
        app_show_info("CO Idle", "Disetel ke standar", 1500);
    } else {
        app_show_info("CO Idle", "Gagal menyetel", 1500);
    }
}

/* -------------------------------------------------------------------------
 * Feature 12: Pembaruan Firmware / DB
 * ----------------------------------------------------------------------- */
static void app_feature_firmware_update(void)
{
    if (!fw_update_is_initialized(g_fw_upd)) {
        firmware_update_config_t cfg = FW_UPDATE_CONFIG_DEFAULT(g_disp, NULL, NULL);
        if (fw_update_init(&cfg, &g_fw_upd) != FW_UPDATE_OK) {
            app_show_info("Update", "Gagal inisialisasi", 1500);
            return;
        }
    }

    display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
    display_draw_text(g_disp, 10, 20, "Pembaruan", DISPLAY_COLOR_CYAN, 1);
    display_draw_text(g_disp, 10, 40, "1: WiFi OTA", DISPLAY_COLOR_WHITE, 1);
    display_draw_text(g_disp, 10, 56, "2: USB/SD", DISPLAY_COLOR_WHITE, 1);
    display_draw_text(g_disp, 10, 72, "3: DB Motor (SD)", DISPLAY_COLOR_WHITE, 1);
    display_draw_text(g_disp, 10, DISPLAY_HEIGHT - 20,
                      "BACK = kembali", DISPLAY_COLOR_GRAY, 1);

    /* For MVP: just show the menu; actual WiFi/SSID and URL are configured
     * at compile time or via a settings screen not yet implemented. */
    app_wait_for_back();
}

/* -------------------------------------------------------------------------
 * Feature 13: Program Kunci (Smartkey)
 * ----------------------------------------------------------------------- */
static void app_feature_key_program(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("Key Prog", "Layanan belum siap", 1500);
        return;
    }

    if (!key_prog_is_initialized(g_key_prog)) {
        key_prog_config_t cfg = KEY_PROG_CONFIG_DEFAULT(g_proto, g_disp);
        if (key_prog_init(&cfg, &g_key_prog) != KEY_PROG_OK) {
            app_show_info("Key Prog", "Gagal inisialisasi", 1500);
            return;
        }
    }

    /* Submenu pilihan operasi kunci. */
    uint8_t pilih = 0;
    const char *ops[4] = {
        "1: Baca Kunci",
        "2: Tambah Kunci",
        "3: Hapus Kunci",
        "4: Duplikasi Kunci",
    };

    for (;;) {
        display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
        display_draw_text(g_disp, 10, 20, "Program Kunci", DISPLAY_COLOR_CYAN, 1);
        for (int i = 0; i < 4; i++) {
            display_draw_text(g_disp, 10, 40 + i * 16, ops[i],
                              (i == pilih) ? DISPLAY_COLOR_YELLOW : DISPLAY_COLOR_WHITE, 1);
        }
        display_draw_text(g_disp, 10, DISPLAY_HEIGHT - 20,
                          "UP/DOWN=pilih SELECT=ok BACK=kembali", DISPLAY_COLOR_GRAY, 1);

        uint8_t btn = buttons_read();
        if (btn & BTN_UP) {
            buttons_wait_release();
            pilih = (pilih + 3) % 4;
            continue;
        }
        if (btn & BTN_DOWN) {
            buttons_wait_release();
            pilih = (pilih + 1) % 4;
            continue;
        }
        if (btn & BTN_SELECT) {
            buttons_wait_release();
            break;
        }
        if (btn & BTN_BACK) {
            buttons_wait_release();
            return;
        }
        delay(50);
        yield();
    }

    key_prog_status_t st = KEY_PROG_OK;
    char msg[128];

    switch (pilih) {
    case 0: { /* Baca Kunci — read-only, tidak perlu warning screen. */
        key_info_t keys[KEY_PROG_MAX_KEYS];
        size_t count = sizeof(keys) / sizeof(keys[0]);
        st = key_prog_read_keys(g_key_prog, keys, &count, 5000);

        display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
        display_draw_text(g_disp, 10, 20, "Daftar Kunci", DISPLAY_COLOR_CYAN, 1);

        if (st != KEY_PROG_OK) {
            snprintf(msg, sizeof(msg), "Gagal baca (code %d)", (int)st);
            display_draw_text(g_disp, 10, 40, msg, DISPLAY_COLOR_RED, 1);
        } else if (count == 0) {
            display_draw_text(g_disp, 10, 40, "Tidak ada kunci terdaftar",
                              DISPLAY_COLOR_GRAY, 1);
        } else {
            uint8_t y = 40;
            for (size_t i = 0; i < count && y < DISPLAY_HEIGHT - 24; i++) {
                snprintf(msg, sizeof(msg), "Slot %d type %d", keys[i].key_index,
                         (int)keys[i].key_type);
                display_draw_text(g_disp, 10, y, msg, DISPLAY_COLOR_WHITE, 1);
                y += 16;
            }
        }
        break;
    }

    case 1: { /* Tambah Kunci — butuh konfirmasi pengguna terlebih dahulu. */
        if (!warning_show(g_disp, WARN_CUSTOM,
                          "Tambah kunci baru? Tekan SELECT", 15000)) {
            app_show_info("Key Prog", "Dibatalkan", 1000);
            return;
        }
        static const uint8_t contoh_key[8] = { 0xAA, 0xBB, 0xCC, 0xDD,
                                               0xEE, 0xFF, 0x00, 0x11 };
        st = key_prog_add_key(g_key_prog, KEY_TYPE_TRANSPONDER, contoh_key,
                              sizeof(contoh_key), 10000);
        snprintf(msg, sizeof(msg), st == KEY_PROG_OK ? "Kunci ditambahkan"
                         : "Gagal tambah (code %d)", (int)st);
        app_show_info("Key Prog", msg, 2000);
        break;
    }

    case 2: { /* Hapus Kunci — butuh konfirmasi pengguna terlebih dahulu. */
        if (!warning_show(g_disp, WARN_CUSTOM,
                          "Hapus kunci? Tekan SELECT", 15000)) {
            app_show_info("Key Prog", "Dibatalkan", 1000);
            return;
        }
        st = key_prog_delete_key(g_key_prog, 0, 10000);
        snprintf(msg, sizeof(msg), st == KEY_PROG_OK ? "Kunci dihapus"
                         : "Gagal hapus (code %d)", (int)st);
        app_show_info("Key Prog", msg, 2000);
        break;
    }

    case 3: { /* Duplikasi Kunci — butuh konfirmasi pengguna terlebih dahulu. */
        if (!warning_show(g_disp, WARN_CUSTOM,
                          "Duplikasi kunci? Tekan SELECT", 15000)) {
            app_show_info("Key Prog", "Dibatalkan", 1000);
            return;
        }
        st = key_prog_duplicate_key(g_key_prog, 0, 10000);
        snprintf(msg, sizeof(msg), st == KEY_PROG_OK ? "Kunci diduplikasi"
                         : "Gagal duplikasi (code %d)", (int)st);
        app_show_info("Key Prog", msg, 2000);
        break;
    }
    }

    app_wait_for_back();
}

/* -------------------------------------------------------------------------
 * Feature 14: Deteksi Smart Key
 * ----------------------------------------------------------------------- */
static void app_feature_smartkey_detect(void)
{
    if (g_proto == NULL || g_disp == NULL) {
        app_show_info("SmartKey", "Layanan belum siap", 1500);
        return;
    }

    if (!smartkey_detect_is_initialized(g_smartkey_detect)) {
        smartkey_detect_config_t cfg = SMARTKEY_DETECT_CONFIG_DEFAULT(g_proto, g_disp);
        if (smartkey_detect_init(&cfg, &g_smartkey_detect) != SMARTKEY_DETECT_OK) {
            app_show_info("SmartKey", "Gagal inisialisasi", 1500);
            return;
        }
    }

    smartkey_info_t info;
    smartkey_detect_status_t st =
            smartkey_detect_scan(g_smartkey_detect, &info, 6000);

    char msg[128];
    display_fill_screen(g_disp, DISPLAY_COLOR_BLACK);
    display_draw_text(g_disp, 10, 20, "Deteksi Smart Key", DISPLAY_COLOR_CYAN, 1);

    if (st != SMARTKEY_DETECT_OK) {
        snprintf(msg, sizeof(msg), "Status: %d", (int)st);
        display_draw_text(g_disp, 10, 40, msg, DISPLAY_COLOR_RED, 1);
        if (st == SMARTKEY_DETECT_ERR_KEY_NOT_FOUND) {
            display_draw_text(g_disp, 10, 56, "Kunci tidak terdeteksi",
                              DISPLAY_COLOR_GRAY, 1);
        }
    } else {
        snprintf(msg, sizeof(msg), "Terdeteksi: Ya");
        display_draw_text(g_disp, 10, 40, msg, DISPLAY_COLOR_GREEN, 1);
        snprintf(msg, sizeof(msg), "Sinyal: %d dBm", info.rssi_dbm);
        display_draw_text(g_disp, 10, 56, msg, DISPLAY_COLOR_WHITE, 1);
        snprintf(msg, sizeof(msg), "Baterai: %d%%", info.battery_level);
        display_draw_text(g_disp, 10, 72, msg, DISPLAY_COLOR_WHITE, 1);
        snprintf(msg, sizeof(msg), "ID: %02X%02X%02X...", info.key_id[0],
                 info.key_id[1], info.key_id[2]);
        display_draw_text(g_disp, 10, 88, msg, DISPLAY_COLOR_WHITE, 1);
    }

    app_wait_for_back();
}

/* -------------------------------------------------------------------------
 * run_selected_feature — dispatch the selected feature to the matching
 * application module.
 * ----------------------------------------------------------------------- */
static void run_selected_feature(void)
{
    /* Ensure a protocol handle exists; auto-detect if none yet. */
    if (g_proto == NULL) {
        if (!detect_protocol_auto()) {
            app_show_info("Fitur", "Protokol tidak terdeteksi", 2000);
            return;
        }
        app_modules_bind_protocol(g_proto);
    }

    /* Open the diagnostic session before running the feature. */
    if (proto_open_session(g_proto, KLINE_PROTO_SESSION_DEFAULT, 5000)
        != PROTO_OK)
    {
        app_show_info("Fitur", "Gagal buka sesi diagnostik", 1500);
        return;
    }
    app_modules_bind_protocol(g_proto);

    switch (g_feature) {
    case FEATURE_READ_DTC:
        app_feature_read_dtc();
        break;

    case FEATURE_CLEAR_DTC:
        app_feature_clear_dtc();
        break;

    case FEATURE_LIVE_DATA:
        app_feature_live_data();
        break;

    case FEATURE_SENSOR_TEST:
        app_feature_sensor_test();
        break;

    case FEATURE_ACTUATOR_PUMP:
        app_feature_actuator(ACTUATOR_TEST_ID_FUEL_PUMP);
        break;

    case FEATURE_ACTUATOR_INJECTOR:
        app_feature_actuator(ACTUATOR_TEST_ID_INJECTOR);
        break;

    case FEATURE_ACTUATOR_RELAY:
        app_feature_actuator(ACTUATOR_TEST_ID_RELAY);
        break;

    case FEATURE_ECU_BACKUP:
        app_feature_ecu_backup();
        break;

    case FEATURE_FILE_MANAGER:
        app_feature_file_manager();
        break;

    case FEATURE_MOTORCYCLE:
        app_feature_motorcycle();
        break;

    case FEATURE_RECOVERY:
        app_feature_recovery();
        break;

    /* --- 12 fitur tambahan --- */
    case FEATURE_ECU_INFO:
        app_feature_ecu_info();
        break;

    case FEATURE_FREEZE_FRAME:
        app_feature_freeze_frame();
        break;

    case FEATURE_DTC_QUERY:
        app_feature_dtc_query();
        break;

    case FEATURE_ECU_PROGRAM:
        app_feature_ecu_program();
        break;

    case FEATURE_SIGNAL_OUTPUT:
        app_feature_signal_output();
        break;

    case FEATURE_ACTUATOR_IGNITION_COIL:
        app_feature_actuator(ACTUATOR_TEST_ID_IGNITION_COIL);
        break;

    case FEATURE_ACTUATOR_RADIATOR_FAN:
        app_feature_actuator(ACTUATOR_TEST_ID_RADIATOR_FAN);
        break;

    case FEATURE_ACTUATOR_FUEL_PUMP_2:
        app_feature_actuator(ACTUATOR_TEST_ID_FUEL_PUMP_2);
        break;

    case FEATURE_ABS_MODULE:
        app_feature_abs_module();
        break;

    case FEATURE_BATTERY_TEST:
        app_feature_battery_test();
        break;

    case FEATURE_CRANKING_TEST:
        app_feature_cranking_test();
        break;

    case FEATURE_CHARGING_TEST:
        app_feature_charging_test();
        break;

    case FEATURE_CO_ADJUST:
        app_feature_co_adjust();
        break;

    case FEATURE_FIRMWARE_UPDATE:
        app_feature_firmware_update();
        break;

    case FEATURE_KEY_PROGRAM:
        app_feature_key_program();
        break;

    case FEATURE_SMARTKEY_DETECT:
        app_feature_smartkey_detect();
        break;

    default:
        app_show_info("Fitur", "Belum tersedia", 1000);
        break;
    }
}

/* -------------------------------------------------------------------------
 * Arduino entry points
 * ----------------------------------------------------------------------- */
void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {
        /* Wait for Serial to be ready (for USB debugging) */
    }

    Serial.println(F(""));
    Serial.println(F("========================================"));
    Serial.println(F("  OBD Scanner v1.0 — ESP32 DevKit V4"));
    Serial.println(F("========================================"));

    /* Initialize SPI buses (two separate hosts) */
    spi_bus_init(SPI_HOST_2, CAN_MOSI_GPIO, CAN_MISO_GPIO, CAN_SCK_GPIO,
                 1000000UL);   /* MCP2515 <= 1 MHz */
    spi_bus_init(SPI_HOST_3, SD_MOSI_GPIO, SD_MISO_GPIO, SD_SCK_GPIO,
                 20000000UL);  /* microSD 20 MHz */

    /* Initialize microSD + ECU directories */
    sd_config_t sd_cfg = SD_CONFIG_DEFAULT;
    if (sd_init(&sd_cfg) != SD_OK) {
        Serial.println(F("[SD] Gagal inisialisasi"));
    } else {
        Serial.println(F("[SD] OK"));
        sd_ecu_dirs_init();
    }

    /* Initialize TFT ILI9341 (SPI2_HOST, CS GPIO 15, 20 MHz) */
    spi_device_config_t tcfg = {};
    tcfg.host        = SPI_HOST_2;
    tcfg.cs_gpio     = TFT_CS_GPIO;
    tcfg.clock_hz    = 20000000UL;
    tcfg.mode        = SPI_MODE_0;
    tcfg.bit_order   = SPI_BIT_ORDER_MSB;
    tcfg.cs_active_low = true;
    tcfg.dummy_bits  = 0;

    if (spi_device_init(&tcfg, &g_spi_tft) != SPI_OK) {
        Serial.println(F("[TFT] Gagal inisialisasi SPI device"));
    } else {
        display_config_t dcfg = DISPLAY_CONFIG_DEFAULT(g_spi_tft);
        if (display_init(&dcfg, &g_disp) != DISPLAY_OK) {
            Serial.println(F("[TFT] Gagal inisialisasi display"));
        } else {
            Serial.println(F("[TFT] OK"));
            display_show_splash(g_disp, "OBD Scanner", "Siap");
        }
    }

    /* Ignition-sense pin (via PC817) */
    pinMode(IGNITION_SENSE_GPIO, INPUT);

    /* --- UI / physical buttons (after display is ready) --- */
    buttons_init();

    /* --- Power management: ignition sense + backlight ---
     * power_mgmt_init() configures IGNITION_SENSE_GPIO as input and is
     * also called again in STATE_SELF_TEST. */
    power_mgmt_init();

    /* --- Application modules that do not depend on a protocol session --- */
    {
        ecu_file_config_t efcfg = ECU_FILE_CONFIG_DEFAULT;
        ecu_file_init(&efcfg, &g_ecu_file);
    }
    recovery_init();   /* UART recovery console (singleton, no handle) */

    if (g_disp != NULL) {
        file_manager_config_t fmcfg = FILE_MANAGER_CONFIG_DEFAULT(g_disp);
        file_manager_init(&fmcfg, &g_fm);
    }

    /* --- Standalone application modules (no protocol session needed) --- */
    {
        dtc_query_config_t dqcfg = DTC_QUERY_CONFIG_DEFAULT;
        if (dtc_query_init(&dqcfg, &g_dtc_q) != DTC_QUERY_OK) {
            g_dtc_q = NULL;
        }
    }
    {
        battery_config_t bcfg = BATTERY_CONFIG_DEFAULT;
        bcfg.disp = g_disp;
        if (battery_init(&bcfg, &g_battery) != BATTERY_OK) {
            g_battery = NULL;
        }
    }
    {
        cranking_config_t ckcfg = CRANKING_CONFIG_DEFAULT;
        if (cranking_init(&ckcfg, &g_cranking) != CRANKING_OK) {
            g_cranking = NULL;
        }
    }
    {
        charging_config_t chcfg = CHARGING_CONFIG_DEFAULT;
        chcfg.disp = g_disp;
        if (charging_init(&chcfg, &g_charging) != CHARGING_OK) {
            g_charging = NULL;
        }
    }
    {
        firmware_update_config_t fucfg = FW_UPDATE_CONFIG_DEFAULT(g_disp, NULL, NULL);
        if (fw_update_init(&fucfg, &g_fw_upd) != FW_UPDATE_OK) {
            g_fw_upd = NULL;
        }
    }

    /* --- Main menu --- */
    menu_init(&g_menu, "OBD Scanner", g_menu_items, MENU_ITEM_COUNT);

    /* --- Protocol-dependent application modules ---
     * g_proto is still NULL here (protocol not detected yet); handles are
     * (re)bound when a diagnostic session is opened (STATE_BUKA_SESI and
     * run_selected_feature). */
    app_modules_bind_protocol(g_proto);

    g_state = STATE_SELF_TEST;
    Serial.println(F("State machine dimulai"));
}

void loop()
{
    g_ignition = digitalRead(IGNITION_SENSE_GPIO);

    /* Safety: if ignition is off during operation, force SLEEP */
    if (!g_ignition &&
        g_state != STATE_SLEEP &&
        g_state != STATE_POWER_ON)
    {
        Serial.println(F("Kontak mati — membatalkan sesi"));
        if (g_proto) {
            proto_close_session(g_proto, 1000);
        }
        g_state = STATE_SLEEP;
    }

    switch (g_state)
    {
        case STATE_POWER_ON:
            if (g_ignition) {
                g_state = STATE_SELF_TEST;
                Serial.println(F("Ignition ON -> SELF_TEST"));
            }
            delay(100);
            break;

        case STATE_SELF_TEST: {
            /* Make sure ignition-sense is configured before operation. */
            power_mgmt_init();
            bool ok = self_test_all();
            if (ok) {
                g_state = STATE_IDLE;
                g_retry = 0;
                g_idle_redraw = true;
                Serial.println(F("Self-test OK -> IDLE"));
            } else {
                g_state = STATE_ERROR;
                Serial.println(F("Self-test GAGAL -> ERROR"));
            }
            break;
        }

        case STATE_IDLE: {
            if (!g_ignition) {
                g_state = STATE_SLEEP;
                break;
            }

            /* Show the main menu and process button navigation. */
            if (g_idle_redraw) {
                menu_draw(&g_menu, g_disp);
                g_idle_redraw = false;
            }

            uint8_t btn = buttons_read();
            if (menu_navigate(&g_menu, btn)) {
                menu_draw(&g_menu, g_disp);
                buttons_wait_release();
            }

            if ((btn & BTN_SELECT) != 0) {
                buttons_wait_release();
                if (g_menu.item_count > 0) {
                    const menu_item_t *item =
                        &g_menu.items[menu_get_selected(&g_menu)];
                    if (item->on_select != NULL) {
                        item->on_select();
                    }
                }
            }
            break;
        }

        case STATE_KONEKSI: {
            bool ok = detect_protocol_auto();
            if (ok) {
                g_state = STATE_SELEKSI_PROTOKOL;
                g_retry = 0;
                Serial.println(F("Protokol terdeteksi -> SELEKSI_PROTOKOL"));
            } else {
                if (g_retry++ >= RETRY_MAX) {
                    g_state = STATE_ERROR;
                    Serial.println(F("Koneksi gagal -> ERROR"));
                } else {
                    Serial.print(F("Retry KONEKSI ke-"));
                    Serial.println(g_retry);
                    delay(1000);
                }
            }
            break;
        }

        case STATE_SELEKSI_PROTOKOL:
            /* Protocol already selected in detect_protocol_auto() */
            g_state = STATE_BUKA_SESI;
            Serial.println(F("Protokol dipilih -> BUKA_SESI"));
            break;

        case STATE_BUKA_SESI: {
            proto_status_t pst =
                proto_open_session(g_proto, KLINE_PROTO_SESSION_DEFAULT, 5000);
            if (pst == PROTO_OK) {
                app_modules_bind_protocol(g_proto);
                if (g_connect_menu) {
                    /* Connect was chosen explicitly from the menu: return
                     * to IDLE so the user can pick the next feature. */
                    g_connect_menu = false;
                    g_state = STATE_IDLE;
                    g_idle_redraw = true;
                    Serial.println(F("Sesi dibuka -> IDLE (siap)"));
                } else {
                    g_state = STATE_OPERASI;
                    Serial.println(F("Sesi dibuka -> OPERASI"));
                }
            } else {
                Serial.print(F("Buka sesi gagal: "));
                Serial.println(pst);
                g_state = STATE_TUTUP_SESI;
            }
            break;
        }

        case STATE_OPERASI:
            run_selected_feature();
            /* After the feature finishes, close the session and return to IDLE */
            g_state = STATE_TUTUP_SESI;
            break;

        case STATE_TUTUP_SESI:
            if (g_proto) {
                proto_close_session(g_proto, 1000);
            }
            g_state = STATE_IDLE;
            g_idle_redraw = true;
            Serial.println(F("Sesi ditutup -> IDLE"));
            break;

        case STATE_SLEEP:
            /* Wait until ignition is on again */
            while (!digitalRead(IGNITION_SENSE_GPIO)) {
                delay(100);
                yield();
            }
            g_state = STATE_POWER_ON;
            Serial.println(F("Kontak ON lagi -> POWER_ON"));
            break;

        case STATE_ERROR:
            Serial.println(F("STATE_ERROR — tekan tombol reset (BOOT/EN) untuk restart"));
            delay(1000);
            break;
    }

    delay(10);
}
