# STRUKTUR FOLDER & KODE FIRMWARE PROYEK — Scanner OBD1/OBD2/Euro5 & Motor

Dokumen ini menyusun struktur folder proyek embedded, arsitektur firmware menurut lapisan, serta contoh kode kunci (K-Line 5-baud init, baca DTC, state machine, dll.) untuk scanner portabel berbasis **ESP32 DevKit V4 + MCP2515 CAN via SPI** (alternatif: STM32F407/RP2040).

---

## 1. Struktur Folder Proyek

Disarankan menggunakan struktur terpisah menurut lapisan agar mudah dipelihara dan dikembangkan (motor vs mobil, K-Line vs CAN).

```
obd-scanner/
├── platformio.ini           # PlatformIO project configuration
├── src/
│   ├── obd-scanner.ino      # Main sketch — inisialisasi, loop utama, state machine
│   ├── app/                 # Application layer (21 fitur scanner)
│   │   ├── abs_module.c / abs_module.h        # ABS modul diagnostik
│   │   ├── actuator_test.c / actuator_test.h  # Tes aktuator (interlock 30s)
│   │   ├── battery_test.c / battery_test.h    # Tes baterai (ADC)
│   │   ├── charging_test.c / charging_test.h  # Tes sistem charging
│   │   ├── co_adjust.c / co_adjust.h          # Pengaturan CO
│   │   ├── cranking_test.c / cranking_test.h  # Tes cranking
│   │   ├── dtc_query.c / dtc_query.h          # Query DTC
│   │   ├── dtc_service.c / dtc_service.h      # Baca/hapus DTC
│   │   ├── ecu_file.c / ecu_file.h            # Backup / restore file ECU
│   │   ├── ecu_info.c / ecu_info.h            # Informasi ECU
│   │   ├── ecu_program.c / ecu_program.h      # Pemrograman ECU (UDS)
│   │   ├── firmware_update.c / firmware_update.h  # Update firmware OTA/USB
│   │   ├── freeze_frame.c / freeze_frame.h    # Freeze frame data
│   │   ├── key_program.c / key_program.h      # Pemrograman kunci immobilizer
│   │   ├── live_data.c / live_data.h          # Streaming PID / data hidup
│   │   ├── motorcycle.c / motorcycle.h        # Fitur khusus motor (OBD1, blink)
│   │   ├── power_mgmt.c / power_mgmt.h        # Manajemen daya, ignition-sense, sleep
│   │   ├── recovery.c / recovery.h            # Pemulihan via UART / bootloader
│   │   ├── signal_output.c / signal_output.h  # Output sinyal / simulasi sensor
│   │   ├── smartkey_detect.c / smartkey_detect.h  # Detektor smart key
│   │   └── sensor_test.c / sensor_test.h      # Tes semua sensor
│   ├── config/                    # Konstanta kompilasi
│   │   ├── config.h    # Pin mapping, baud rate, timeout, PID daftar
│   │   └── pinout.h    # Mapping pin ESP32 per periferal
│   ├── drivers/                   # BSP/HAL — driver periferal
│   │   ├── adc.h / adc.c            # ADC (12-bit, averaging, divider ratio)
│   │   ├── flash.h                  # Dual-slot flash abstraction (stub)
│   │   ├── sd.h                     # microSD SPI3_HOST FAT32
│   │   └── watchdog.h               # Watchdog abstraction (stub)
│   ├── protocol/                  # Lapisan protokol diagnostik
│   │   ├── aldl.h / aldl.c          # GM ALDL 8192 baud (OBD1)
│   │   ├── blink_parser.h / blink_parser.c  # Parser kode blink OBD1 motor
│   │   ├── dtc.h / dtc.c            # Tabel DTC & decode
│   │   ├── eec_iv.h / eec_iv.c      # Ford EEC-IV VPW/PWM (OBD1)
│   │   ├── iso15765_2.h / iso15765_2.c  # Transport layer CAN (SF/FF/CF/FC)
│   │   ├── j1979.h / j1979.c        # SAE J1979 mode $01–$0A (OBD2)
│   │   ├── kline_proto.h / kline_proto.c  # K-Line protokol (init 5-baud)
│   │   ├── kwp2000.h / kwp2000.c    # KWP2000 ISO 14230-4
│   │   ├── protocol.h / protocol.c  # Unified protocol handle (opaque)
│   │   └── uds.h / uds.c            # UDS ISO 14229 (Euro5/modern)
│   ├── ui/                        # Antarmuka pengguna
│   │   ├── buttons.h / buttons.c    # Tombol fisik
│   │   ├── display.h / display.c    # Driver TFT ILI9341
│   │   ├── font_data.h / font_data.c # Font layar
│   │   ├── menu.h / menu.c          # Menu navigasi
│   │   └── warning_screen.h / warning_screen.c  # Layar peringatan keamanan
│   └── utils/                     # Utilitas umum
│       ├── crc.h / crc.c            # CRC32 untuk verifikasi integritas
│       └── hmac.h / hmac.c          # HMAC-SHA256 untuk tanda tangan citra
├── docs/
│   ├── MASTER_PLAN.md       # Master plan teknis
│   ├── DTC_TABEL_MOTOR.md   # Tabel DTC per merek motor
│   ├── SKEMA_PCB.pdf        # Skema rangkaian PCB
│   └── PANDUAN_KONEKTOR_MOTOR.md  # Panduan adaptor konnector motor
├── .gitignore
└── README.md              # Panduan build dan flash
```

> **Catatan implementasi (2026-10-29):** Dokumen asli ditulis dengan target STM32 HAL
> (main.c, Makefile, linker script, GPIO_WriteBit/GPIOD, system_clock_init, dll.).
> Implementasi nyata di project ini menggunakan **PlatformIO + Arduino-ESP32**
> dengan titik masuk `src/obd-scanner.ino` (bukan `main.c`), dan build sistem
> `platformio.ini` (bukan Makefile/CMake). Contoh kode dalam dokumen yang menggunakan
> API STM32 HAL (mis. `gpio_write`, `Bit_SET`, `system_reset`, `GPIOD`) bersifat
> ilustratif dan perlu diadaptasi ke Arduino-ESP32 sebelum digunakan.
>
> File utilitas yang sebelumnya tercantum sebagai hilang kini sudah tersedia sebagai
> antarmuka/stub: `src/utils/crc.h` + `src/utils/crc.c` (CRC32 terimplementasi),
> `src/utils/hmac.h` (HMAC-SHA256 stub), `src/drivers/flash.h` (dual-slot flash stub),
> `src/drivers/watchdog.h` (watchdog stub). Implementasi body HMAC-SHA256, flash
> dual-slot, dan watchdog masih perlu diisi sebelum produksi.

---

## 2. Arsitektur Firmware (Lapisan)

```
APLIKASI (dtc_service, live_data, actuator_test, ecu_file, recovery, motorcycle)
      ↓
LAYER PROTOKOL (J1979, KWP2000, UDS, ALDL, EEC-IV, blink_parser, iso15765_2)
      ↓
LAYER DATA LINK (driver K-Line, driver CAN, transport ISO 15765-2, driver UART)
      ↓
BSP/HAL (GPIO, UART, CAN, SPI, ADC, watchdog, flash, SD)
      ↓
HARDWARE (MCU ESP32 DevKit V4, transceiver CAN SN65HVD230 + kontroler MCP2515 SPI, modul K-Line optocoupler PC817, microSD SPI, TFT ILI9341 SPI)
```

---

## 3. Contoh Kode Kunci

### 3.1 Konfigurasi Pin & Konstanta (`config/pinout.h`, `config/config.h`)

```c
/* config/pinout.h — Pin mapping ESP32 DevKit V4 (GPIO numbering) */
/* ESP32: UART0 = debug console (USB-Serial DevKit), UART1 = K-Line, UART2 = recovery */
#ifndef PINOUT_H
#define PINOUT_H

/* UART allocation (ESP32 has 3 hardware UARTs: UART0, UART1, UART2) */
#define UART_DEBUG_NUM        0   /* Debug console — sengaja dipisahkan dari flashing */
#define UART_KLINE_NUM        1   /* K-Line ISO 9141-2 via modul optocoupler PC817 */
#define UART_RECOVERY_NUM     2   /* UART recovery (BOOT0 + sistem bootloader) */

/* K-Line GPIO (UART1) — sesuaikan dengan pemasangan fisik pada DevKit V4 */
#define KLINE_TX_GPIO         GPIO_NUM_17
#define KLINE_RX_GPIO         GPIO_NUM_16
#define KLINE_INVERT_LOGIC    1   /* Optocoupler PC817 biasanya logika terbalik */

/* CAN via MCP2515 (SPI) — ESP32 VSPI/HSPI, CS terpisah per device */
#define CAN_SPI_HOST          SPI2_HOST   /* atau VSPI, tergantung board variant */
#define CAN_SPI_CS_GPIO       GPIO_NUM_5   /* CS MCP2515 — terpisah dari microSD dan TFT */
#define CAN_INT_GPIO          GPIO_NUM_4   /* MCP2515 INT ke ESP32 (interrupt CAN) */
#define CAN_SCK_GPIO          GPIO_NUM_18
#define CAN_MOSI_GPIO         GPIO_NUM_23
#define CAN_MISO_GPIO         GPIO_NUM_19

/* microSD (SPI) — CS terpisah dari MCP2515 dan TFT */
#define SD_SPI_HOST           SPI3_HOST   /* atau HSPI, tergantung board variant */
#define SD_CS_GPIO            GPIO_NUM_13

/* Display TFT ILI9341 (SPI) — CS terpisah dari MCP2515 dan microSD */
#define TFT_SPI_HOST          SPI2_HOST   /* boleh condivide host dengan MCP2515 kalau CS terpisah */
#define TFT_CS_GPIO           GPIO_NUM_15
#define TFT_DC_GPIO           GPIO_NUM_2
#define TFT_RESET_GPIO        GPIO_NUM_4  /* sesuaikan; beberapa modul TFT pakai reset bersama */
#define TFT_BACKLIGHT_GPIO    GPIO_NUM_21

/* Power & Ignition-sense (via optokopel PC817) */
#define IGNITION_SENSE_GPIO   GPIO_NUM_34   /* input-only, via PC817 — deteksi kontak */
#define BUCK_ENABLE_GPIO      GPIO_NUM_25   /* enable buck converter 12→5 V */

/* BOOT0 untuk recovery via UART (ESP32: GPIO0 = boot mode pin) */
#define ESP_BOOT0_GPIO        GPIO_NUM_0
#define RECOVERY_BAUD         115200

#endif /* PINOUT_H */
```

```c
/* config/config.h — Konstanta sistem (ESP32 DevKit V4) */
#ifndef CONFIG_H
#define CONFIG_H

/* ESP32: tidak ada SYSTEM_CORE_CLOCK tetap; clock diatur oleh ESP-IDF/Arduino core */
/* CPU ~240 MHz dual-core, clock dapat dikonfigurasi via menuconfig / board setting */

/* K-Line ISO 9141-2 */
#define KLINE_BAUD_DATA         10400UL
#define KLINE_WAKEUP_PULSE_MS   300     /* 200–400 ms */
#define KLINE_INIT_BIT_MS       200     /* ≈5 baud */
#define KLINE_TIMEOUT_MS        5000

/* CAN ISO 15765-4 (via MCP2515 SPI) */
#define CAN_BAUDRATE            500000UL
#define CAN_FUNCTIONAL_ADDR     0x7DF
#define CAN_PHYSICAL_ADDR_BASE  0x7E0
#define CAN_RESPONSE_ADDR_BASE  0x7E8

/* Timeout & safety */
#define ACTUATOR_TEST_MAX_MS    30000   /* 30 detik paksa */
#define ECU_FLASH_TIMEOUT_MS    60000
#define RETRY_MAX               3

/* PID OBD2 mode $01 */
#define PID_SUPPORTED_01_20     0x00
#define PID_ENGINE_LOAD         0x04
#define PID_COOLANT_TEMP        0x05
#define PID_SHORT_FUEL_TRIM_1   0x06
#define PID_LONG_FUEL_TRIM_1    0x07
#define PID_INTAKE_PRESSURE     0x0B
#define PID_RPM                 0x0C
#define PID_VEHICLE_SPEED       0x0D
#define PID_MAF                 0x0A
#define PID_O2_SENSORS          0x13

#endif /* CONFIG_H */
```

### 3.2 Driver K-Line — Inisialisasi 5-baud (`drivers/kline.c`)

```c
/* drivers/kline.c — Driver K-Line ISO 9141-2 dengan modul optocoupler */
#include "kline.h"
#include "config/pinout.h"
#include "config/config.h"
#include "utils/time.h"

/* Optocoupler membalik logika: aktif rendah pada sisi MCU = K-Line ditarik rendah */
#if KLINE_INVERT_LOGIC
#define KLINE_ASSERT()    GPIO_WriteBit(KLINE_TX_PORT, KLINE_TX_PIN, Bit_RESET)
#define KLINE_RELEASE()   GPIO_WriteBit(KLINE_TX_PORT, KLINE_TX_PIN, Bit_SET)
#else
#define KLINE_ASSERT()    GPIO_WriteBit(KLINE_TX_PORT, KLINE_TX_PIN, Bit_SET)
#define KLINE_RELEASE()   GPIO_WriteBit(KLINE_TX_PORT, KLINE_TX_PIN, Bit_RESET)
#endif

static bool kline_initialized = false;

/* Delay mikro detik non-blokir atau blokir pendek (untuk timing 5-baud) */
static void kline_delay_us(uint32_t us) {
    /* Implementasi menggunakan timer atau SysTick */
    delay_us(us);
}

/* Wake-up pulse: K-Line ditarik rendah 200–400 ms oleh ECU, deteksi di sisi tester */
bool kline_wakeup_detect(void) {
    /* Tunggu K-Line turun rendah (ECU memberikan wake-up pulse) */
    uint32_t start = millis();
    while (GPIO_ReadInputDataBit(KLINE_RX_PORT, KLINE_RX_PIN) == Bit_SET) {
        if (millis() - start > 1000) return false; /* Timeout */
    }
    /* Verifikasi pulse cukup lama (200–400 ms) */
    uint32_t pulse_start = millis();
    while (GPIO_ReadInputDataBit(KLINE_RX_PORT, KLINE_RX_PIN) == Bit_RESET) {
        if (millis() - pulse_start > 500) return false;
    }
    uint32_t pulse_width = millis() - pulse_start;
    return (pulse_width >= 200 && pulse_width <= 500);
}

/* Kirim start bit 5-baud (≈200 ms per bit) */
bool kline_send_init_bit(void) {
    KLINE_ASSERT();           /* Tarik rendah */
    kline_delay_us(KLINE_INIT_BIT_MS * 1000UL); /* ≈200 ms */
    KLINE_RELEASE();          /* Lepas, biarkan pull-up ke Vbatt naikkan */
    kline_delay_us(50000);    /* Tunggu stabilisasi */
    return true;
}

/* Terima byte inisialisasi dari ECU (0x80 biasanya, atau spesifik pabrikan) */
bool kline_read_init_byte(uint8_t *byte_out) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        /* Setiap bit data 5-baud ≈200 ms, LSB dulu (sesuai protokol pabrikan) */
        kline_delay_us(KLINE_INIT_BIT_MS * 1000UL / 2);
        uint8_t bit = GPIO_ReadInputDataBit(KLINE_RX_PORT, KLINE_RX_PIN);
        data |= (bit << i);
        kline_delay_us(KLINE_INIT_BIT_MS * 1000UL / 2);
    }
    if (byte_out) *byte_out = data;
    return true;
}

/* Inisialisasi lengkap K-Line (5-baud init) */
kline_status_t kline_init(void) {
    /* Konfigurasi UART pada 10.400 baud, 8N1 (digunakan setelah init byte diterima) */
    uart_init(KLINE_UART, KLINE_BAUD_DATA, UART_PARITY_NONE, UART_STOPBITS_1);

    /* 1. Deteksi wake-up pulse dari ECU */
    if (!kline_wakeup_detect()) {
        return KLINE_ERR_NO_WAKEUP;
    }

    /* 2. Kirim start bit 5-baud */
    if (!kline_send_init_bit()) {
        return KLINE_ERR_INIT_BIT_FAIL;
    }

    /* 3. Terima byte inisialisasi dari ECU */
    uint8_t init_byte = 0;
    if (!kline_read_init_byte(&init_byte)) {
        return KLINE_ERR_INIT_BYTE_FAIL;
    }

    /* 4. Validasi byte inisialisasi (0x80 umum, bervisialisasi pabrikan) */
    if (init_byte == 0x00) {
        return KLINE_ERR_INVALID_INIT;
    }

    kline_initialized = true;
    return KLINE_OK;
}

/* Kirim byte data via UART (setelah inisialisasi selesai) */
bool kline_write_byte(uint8_t data) {
    if (!kline_initialized) return false;
    return uart_write_byte(KLINE_UART, data);
}

/* Terima byte data via UART */
bool kline_read_byte(uint8_t *data, uint32_t timeout_ms) {
    if (!kline_initialized) return false;
    return uart_read_byte(KLINE_UART, data, timeout_ms);
}
```

### 3.3 Lapisan Protokol — Baca DTC SAE J1979 Mode $03 (`protocol/j1979.c`)

```c
/* protocol/j1979.c — SAE J1979 OBD2/EOBD mode $01–$0A */
#include "j1979.h"
#include "protocol/iso15765_2.h"
#include "utils/crc.h"

#define SID_READ_DTC          0x03    /* DTC terkonfirmasi terkait emisi */
#define SID_PENDING_DTC       0x07    /* DTC pending */
#define SID_PERMANENT_DTC     0x0A    /* Permanent DTC */
#define SID_CLEAR_DTC         0x04    /* Hapus DTC */

/* Format respons DTC: SID + 3 byte per DTC (2 byte kode + 1 byte status) */
typedef struct {
    uint8_t sid;
    uint8_t num_dtcs;
    uint8_t dtc_data[48]; /* Maksimum beberapa DTC dalam satu respons */
} j1979_dtc_response_t;

/* Baca DTC mode $03 */
bool j1979_read_dtcs(uint8_t mode, dtc_list_t *list_out) {
    uint8_t request[2] = {mode, 0x00}; /* Mode + PID 0x00 (untuk $03 tidak pakai PID, padding) */
    uint8_t response[64];
    uint16_t resp_len = 0;

    /* Kirim request via transport ISO 15765-2 (Single Frame biasanya cukup) */
    if (!iso15765_send_request(CAN_FUNCTIONAL_ADDR, request, 2,
                               response, &resp_len, KLINE_TIMEOUT_MS)) {
        return false;
    }

    /* Validasi respons: SID respons = 0x40 + mode */
    if (resp_len < 2 || response[0] != (uint8_t)(0x40 | mode)) {
        return false;
    }

    /* Parsing DTC: setiap DTC = 3 byte (2 byte kode + 1 byte status) */
    list_out->count = 0;
    uint16_t offset = 1; /* Lewati byte SID respons */
    while (offset + 2 <= resp_len) {
        dtc_entry_t entry;
        entry.code_raw = (response[offset] << 8) | response[offset + 1];
        entry.status = (offset + 2 < resp_len) ? response[offset + 2] : 0;

        /* Konversi ke string 5 karakter (P/B/C/U + 4 digit) */
        dtc_code_to_string(entry.code_raw, entry.code_str, sizeof(entry.code_str));
        dtc_description_lookup(entry.code_str, entry.desc, sizeof(entry.desc)); /* Dari dtc_table.c */

        list_out->entries[list_out->count++] = entry;
        offset += 3;
    }

    return true;
}

/* Konversi kode DTC raw ke string 5 karakter */
void dtc_code_to_string(uint16_t raw, char *str, size_t len) {
    const char *system[] = {"P", "B", "C", "U"};
    uint8_t sys = (raw >> 12) & 0x03;
    uint16_t num = raw & 0x0FFF;
    snprintf(str, len, "%s%04X", system[sys], num);
}

/* Hapus DTC mode $04 */
bool j1979_clear_dtcs(void) {
    uint8_t request[2] = {SID_CLEAR_DTC, 0x00};
    uint8_t response[8];
    uint16_t resp_len = 0;
    return iso15765_send_request(CAN_FUNCTIONAL_ADDR, request, 2,
                                 response, &resp_len, KLINE_TIMEOUT_MS) &&
           resp_len >= 1 && response[0] == 0x44;
}
```

### 3.4 State Machine Utama (`main.c`)

```c
/* main.c — Titik masuk firmware dengan state machine utama */
#include "config/pinout.h"
#include "config/config.h"
#include "drivers/uart.h"
#include "drivers/can.h"
#include "drivers/kline.h"
#include "drivers/watchdog.h"
#include "drivers/sd_card.h"
#include "ui/display.h"
#include "protocol/j1979.h"
#include "protocol/uds.h"
#include "application/dtc_service.h"
#include "application/motorcycle.h"
#include "utils/logger.h"

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

static scanner_state_t current_state = STATE_POWER_ON;
static scanner_state_t previous_state = STATE_POWER_ON;
static uint8_t retry_count = 0;
static bool ignition_on = false;

int main(void) {
    /* Inisialisasi sistem */
    system_clock_init();
    watchdog_init();
    gpio_init();
    adc_init();
    display_init();
    buttons_init();
    sd_card_init();

    /* Indikator hidup */
    logger_init(LOG_UART_DEBUG, LOG_SD_DIAGNOSTIK);

    display_show_splash("OBD Scanner v1.0");

    while (1) {
        watchdog_reset(); /* Kick watchdog setiap loop */

        ignition_on = gpio_read(IGNITION_SENSE_PORT, IGNITION_SENSE_PIN);

        switch (current_state) {
            case STATE_POWER_ON:
                if (ignition_on) {
                    current_state = STATE_SELF_TEST;
                }
                break;

            case STATE_SELF_TEST:
                if (self_test_all()) {
                    current_state = STATE_IDLE;
                    retry_count = 0;
                } else {
                    current_state = STATE_ERROR;
                }
                break;

            case STATE_IDLE:
                if (!ignition_on) {
                    current_state = STATE_SLEEP;
                } else if (button_pressed(BTN_MENU)) {
                    current_state = STATE_KONEKSI;
                }
                display_show_home();
                break;

            case STATE_KONEKSI:
                if (detect_protocol_auto()) {
                    current_state = STATE_SELEKSI_PROTOKOL;
                    retry_count = 0;
                } else {
                    if (retry_count++ >= RETRY_MAX) {
                        current_state = STATE_ERROR;
                    }
                }
                break;

            case STATE_SELEKSI_PROTOKOL:
                /* Pilih OBD1 / OBD2 / UDS sesuai deteksi */
                current_state = STATE_BUKA_SESI;
                break;

            case STATE_BUKA_SESI:
                if (open_diagnostic_session()) {
                    current_state = STATE_OPERASI;
                } else {
                    current_state = STATE_TUTUP_SESI;
                }
                break;

            case STATE_OPERASI:
                run_selected_feature(); /* DTC, live data, tes, file, recovery */
                if (feature_complete()) {
                    current_state = STATE_TUTUP_SESI;
                }
                break;

            case STATE_TUTUP_SESI:
                close_diagnostic_session();
                current_state = STATE_IDLE;
                break;

            case STATE_SLEEP:
                peripheral_power_off();
                display_sleep();
                while (!gpio_read(IGNITION_SENSE_PORT, IGNITION_SENSE_PIN)) {
                    watchdog_reset();
                    low_power_sleep(); /* Tidur hingga kontak menyala */
                }
                current_state = STATE_POWER_ON;
                break;

            case STATE_ERROR:
                display_show_error("Kesalahan sistem");
                if (button_pressed(BTN_RESET)) {
                    current_state = STATE_POWER_ON;
                    retry_count = 0;
                }
                break;
        }

        /* Pengaman: jika mati kontak di tengah operasi, paksa tutup sesi */
        if (!ignition_on && current_state != STATE_SLEEP &&
            current_state != STATE_POWER_ON) {
            abort_current_session();
            current_state = STATE_SLEEP;
        }
    }
}

static bool detect_protocol_auto(void) {
    /* Coba K-Line dulu (dominan untuk motor dan OBD2 awal) */
    if (kline_init() == KLINE_OK) {
        protocol_set(PROTOCOL_KLINE);
        return true;
    }
    /* Coba CAN ISO 15765-4 */
    if (can_init(CAN_BAUDRATE) == CAN_OK) {
        protocol_set(PROTOCOL_CAN);
        return true;
    }
    return false;
}

static bool open_diagnostic_session(void) {
    switch (protocol_get()) {
        case PROTOCOL_OBD2_CAN:
        case PROTOCOL_OBD2_KLINE:
            return j1979_start_session();
        case PROTOCOL_UDS:
            return uds_start_session(0x01); /* Default session */
        case PROTOCOL_KWP2000:
            return kwp2000_start_session();
        case PROTOCOL_MOTORCYCLE:
            return motorcycle_start_session();
        default:
            return false;
    }
}

static void run_selected_feature(void) {
    switch (ui_get_selected_feature()) {
        case FEATURE_READ_DTC:       dtc_service_read(); break;
        case FEATURE_CLEAR_DTC:      dtc_service_clear(); break;
        case FEATURE_LIVE_DATA:      live_data_stream(); break;
        case FEATURE_TEST_SENSOR:    sensor_test_all(); break;
        case FEATURE_TEST_FUEL_PUMP: actuator_test_fuel_pump(); break;
        case FEATURE_TEST_INJECTOR:  actuator_test_injector(); break;
        case FEATURE_TEST_RELAY:     actuator_test_relay(); break;
        case FEATURE_BACKUP_ECU:     ecu_file_backup(); break;
        case FEATURE_WRITE_ECU:      ecu_file_write(); break;
        case FEATURE_RECOVERY:       recovery_uart(); break;
        case FEATURE_MOTORCYCLE_BLINK: motorcycle_read_blink(); break;
        default: break;
    }
}
```

### 3.5 Tes Aktuator dengan Interlock Keselamatan (`application/actuator_test.c`)

```c
/* application/actuator_test.c — Tes pompa, injektor, relai dengan interlock */
#include "actuator_test.h"
#include "protocol/j1979.h"
#include "protocol/uds.h"
#include "ui/warning_screen.h"
#include "drivers/watchdog.h"
#include "utils/time.h"

static bool actuator_active = false;
static uint32_t test_start_ms = 0;

bool actuator_test_start(actuator_type_t type, uint32_t duration_ms) {
    /* Interlock: hanya kontak ON, mesin dalam kondisi aman (biasanya mati) */
    if (!ignition_sense_is_on()) {
        warning_screen_show("Kontak harus ON");
        return false;
    }

    /* Konfirmasi dua kali sudah dilakukan di UI sebelum memanggil fungsi ini */

    /* Buka sesi diagnostik */
    if (!open_diagnostic_session_for_actuator()) {
        warning_screen_show("Gagal buka sesi");
        return false;
    }

    /* Kirim perintah kontrol komponen (mode $08 atau UDS $2F IOControl) */
    if (!send_actuator_command(type, ACTUATOR_ON)) {
        close_diagnostic_session();
        warning_screen_show("Gagal mengirim perintah");
        return false;
    }

    actuator_active = true;
    test_start_ms = millis();
    warning_screen_show_testing(type, duration_ms);
    return true;
}

void actuator_test_poll(void) {
    if (!actuator_active) return;

    uint32_t elapsed = millis() - test_start_ms;

    /* Paksa matikan jika melebihi durasi maksimum atau timeout paksa 30 detik */
    if (elapsed >= ACTUATOR_TEST_MAX_MS) {
        actuator_test_stop(true);
        return;
    }

    /* Matikan jika tombol STOP fisik ditekan */
    if (button_pressed(BTN_STOP)) {
        actuator_test_stop(true);
        return;
    }

    /* Pantau respons aktuator (contoh: feedback dari ECU jika ada) */
    monitor_actuator_feedback();
}

void actuator_test_stop(bool completed) {
    if (!actuator_active) return;

    send_actuator_command(ACTUATOR_OFF); /* Matikan output */
    close_diagnostic_session();
    actuator_active = false;

    if (completed) {
        warning_screen_show("Tes selesai");
    } else {
        warning_screen_show("Tes dibatalkan");
    }
}
```

### 3.6 Pemulihan via UART (`application/recovery.c`)

```c
/* application/recovery.c — Pemulihan alat via bootloader sistem MCU (UART) */
#include "recovery.h"
#include "drivers/uart.h"
#include "drivers/flash.h"
#include "utils/hmac.h"
#include "ui/warning_screen.h"

#define BOOT0_PIN       GPIO_PIN_0
#define BOOT0_PORT      GPIOD
#define RECOVERY_BAUD   115200

bool recovery_enter_bootloader(void) {
    /* Aktifkan pin BOOT0, lalu reset MCU */
    gpio_write(BOOT0_PORT, BOOT0_PIN, Bit_SET);
    system_reset();
    return true; /* Tidak akan sampai di sini (reset) */
}

bool recovery_flash_verify_image(const uint8_t *image, uint32_t size,
                                 const uint8_t *signature, uint32_t sig_len) {
    /* 1. Verifikasi tanda tangan (HMAC-SHA256) sebelum flash */
    if (!hmac_sha256_verify(image, size, signature, sig_len)) {
        warning_screen_show("Tanda tangan tidak valid");
        return false;
    }

    /* 2. Cadangkan citra firmware saat ini ke SD (jika memungkinkan) */
    flash_backup_current_to_sd();

    /* 3. Flash image dengan verifikasi blok per blok */
    if (!flash_write_dual_bank(image, size, true /* verify */)) {
        warning_screen_show("Gagal flash");
        return false;
    }

    /* 4. Verifikasi pasca-tulis (baca kembali dan bandingkan) */
    if (!flash_verify_after_write(image, size)) {
        warning_screen_show("Verifikasi gagal");
        return false;
    }

    warning_screen_show("Flash berhasil");
    return true;
}

bool recovery_ecu_uart_bootloader(const uint8_t *ecu_image, uint32_t size) {
    /* Pemulihan ECU via UART ke bootloader ECU (jika pabrikan membuka) */
    /* Implementasi bergantung pada prosedur pabrikan ECU */
    return false; /* Placeholder — perlu dokumentasi pabrikan */
}
```

### 3.7 Manajemen File ECU (`application/ecu_file.c`)

```c
/* application/ecu_file.c — Backup / baca / tulis file ECU dengan tanda tangan */
#include "ecu_file.h"
#include "drivers/sd_card.h"
#include "utils/hmac.h"
#include "utils/crc.h"
#include "utils/time.h"

typedef struct {
    char magic[8];          /* "OBDSCAN1" */
    char vin[17];
    char ecu_part[32];
    char cal_id[32];
    uint32_t timestamp;
    uint8_t protocol;
    uint8_t scanner_version[2];
    uint32_t image_size;
    uint8_t signature[32];  /* HMAC-SHA256 */
} __attribute__((packed)) ecu_backup_header_t;

bool ecu_file_backup(const char *filename) {
    uint8_t *image = NULL;
    uint32_t size = 0;

    /* Baca citra ECU via UDS $2E atau bootloader */
    if (!ecu_read_image(&image, &size)) return false;

    /* Buat header metadata */
    ecu_backup_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "OBDSCAN1", 8);
    get_vin(hdr.vin, sizeof(hdr.vin));
    get_ecu_part_number(hdr.ecu_part, sizeof(hdr.ecu_part));
    hdr.timestamp = rtc_get_unix();
    hdr.protocol = protocol_get();
    hdr.image_size = size;

    /* Tanda tangan HMAC-SHA256 pada citra + header */
    hmac_sha256(image, size, (uint8_t*)&hdr, sizeof(hdr), hdr.signature);

    /* Simpan header + citra ke SD (FATFS) */
    if (!sd_write_file(filename, (uint8_t*)&hdr, sizeof(hdr))) return false;
    if (!sd_append_file(filename, image, size)) return false;

    free(image);
    return true;
}

bool ecu_file_write(const char *filename) {
    ecu_backup_header_t hdr;
    uint8_t *image = NULL;
    uint32_t size = 0;

    /* Baca file dari SD */
    if (!sd_read_file(filename, (uint8_t*)&hdr, sizeof(hdr), &image, &size)) return false;

    /* Verifikasi tanda tangan sebelum tulis */
    if (!hmac_sha256_verify(image, size, hdr.signature, sizeof(hdr.signature))) {
        return false; /* File rusak atau dimodifikasi */
    }

    /* Cadangkan citra ECU saat ini sebelum menulis */
    ecu_file_backup("ECU_BACKUP_BEFORE_WRITE.bin");

    /* Tulis citra ke ECU via UDS $2F atau bootloader */
    if (!ecu_write_image(image, size)) return false;

    /* Verifikasi pasca-tulis */
    uint8_t *verify_image = NULL;
    uint32_t verify_size = 0;
    if (!ecu_read_image(&verify_image, &verify_size)) return false;
    if (verify_size != size || memcmp(verify_image, image, size) != 0) {
        return false; /* Verifikasi gagal */
    }

    return true;
}
```

---

## 4. Build & Flash (ESP32 DevKit V4 + MCP2515)

ESP32 tidak menggunakan ARM GCC. Pilih salah satu dari dua opsi build di bawah:

### 4.1 Opsi A — ESP-IDF (rekomendasi untuk kontrol penuh)

ESP-IDF menyediakan dukungan native untuk FreeRTOS, driver SPI/UART/GPIO, partisi flash dual-slot (OTA-like rollback), dan non-volatile storage (NVS) untuk cadangan.

**Prasyarat:**
- Instal ESP-IDF (https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) atau gunakan Docker image `espressif/idf`.
- Python 3 + `pip install -r $IDF_PATH/requirements.txt`.

Verifikasi:
```bash
idf.py --version
```

**Struktur proyek ESP-IDF:**
```
obd-scanner/
├── main/
│   ├── main.c              # Titik masuk (app_main)
│   └── CMakeLists.txt      # Daftar file sumber main
├── components/
│   ├── kline/              # Driver K-Line (init 5-baud, optocoupler)
│   ├── can_mcp2515/        # Driver CAN via MCP2515 SPI
│   ├── sd_card/            # FATFS microSD
│   ├── tft_ili9341/        # Driver TFT ILI9341
│   ├── protocol/           # J1979, KWP2000, UDS, ALDL, EEC-IV, blink
│   ├── application/        # DTC, live data, actuator, ecu_file, recovery
│   └── ui/                 # Display, touch, buttons, menu
├── config/
│   ├── pinout.h            # Pin mapping ESP32 (lihat Section 3.1)
│   └── config.h            # Konstanta sistem
├── partitions.csv          # Tabel partisi flash (factory + OTA + NVS)
├── sdkconfig               # Konfigurasi ESP-IDF (hasil `idf.py menuconfig`)
└── CMakeLists.txt          # Root CMake (ESP-IDF standar)
```

> **Catatan:** Implementasi nyata di project ini menggunakan **PlatformIO + Arduino-ESP32** (lihat `src/obd-scanner.ino` dan `platformio.ini`), bukan ESP-IDF. Tree ESP-IDF di atas tersedia sebagai referensi alternatif untuk tim yang ingin bermigrasi ke ESP-IDF di masa depan.

**Contoh `partitions.csv` (dual-slot untuk rollback firmware):**
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
otadata,  data, ota,     0xf000,  0x2000,
phy_init, data, phy,     0x11000, 0x1000,
factory,  0,    app,     0x12000, 0x100000,   # Slot A (1 MB)
ota_0,    0,    ota_0,   0x112000, 0x100000,   # Slot B (1 MB)
ota_1,    0,    ota_1,   0x212000, 0x100000,   # Cadangan tambahan
spiffs,   data, spiffs,  0x312000, 0x2F0000,   # File sistem untuk backup ECU/log
```

**Perintah build & flash (ESP-IDF):**
```bash
cd obd-scanner

# Konfigurasi (pilih board ESP32 DevKitV4, port serial, dsb.)
idf.py set-target esp32
idf.py menuconfig

# Bersihkan dan bangun
idf.py fullclean
idf.py build

# Flash ke ESP32 (tombol BOOT0 ditekan saat reset jika perlu mode download)
idf.py -p COM3 flash        # Windows: COM3; Linux: /dev/ttyUSB0

# Monitor output serial
idf.py -p COM3 monitor
```

**Recovery via UART bootloader (ESP32 ROM bootloader):**
1. Tekan dan tahan tombol **BOOT0 (GPIO0)**, lalu tekan **RESET** — ESP32 masuk ke ROM download mode.
2. Lepaskan BOOT0.
3. Flash ulang: `idf.py -p COM3 flash`
4. Tekan RESET untuk boot firmware baru.

---

### 4.2 Opsi B — Arduino ESP32 Core (pengembangan lebih cepat)

Cocok jika Anda ingin menggunakan library Arduino yang sudah matang (MCP2515, ILI9341, SD, Adafruit GFX, dll.).

**Prasyarat:**
- Arduino IDE 2.x atau PlatformIO (VS Code).
- Tambahkan board manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Pasang board "ESP32 Arduino" (espressif:esp32) dan pilih "ESP32 Dev Module".

**Struktur proyek Arduino:**
```
obd-scanner/
├── obd-scanner.ino         # setup() + loop() (state machine)
├── src/
│   ├── kline.cpp / kline.h
│   ├── can_mcp2515.cpp / can_mcp2515.h
│   ├── sd_card.cpp / sd_card.h
│   ├── tft_ili9341.cpp / tft_ili9341.h
│   ├── protocol/           # J1979, KWP2000, UDS, ALDL, EEC-IV, blink
│   ├── application/        # DTC, live data, actuator, ecu_file, recovery
│   └── ui/                 # Display, touch, buttons, menu
├── config/
│   ├── pinout.h            # Pin mapping ESP32
│   └── config.h            # Konstanta sistem
└── libraries/              # Library eksternal (MCP2515, ILI9341, SD, dsb.)
```

**Contoh `obd-scanner.ino` (skelet state machine):**
```cpp
#include "config/pinout.h"
#include "config/config.h"
#include "drivers/kline.h"
#include "drivers/can_mcp2515.h"
#include "drivers/sd_card.h"
#include "ui/display.h"

// State machine utama
enum State { POWER_ON, SELF_TEST, IDLE, KONEKSI, SELEKSI_PROTOKOL,
             BUKA_SESI, OPERASI, TUTUP_SESI, SLEEP, ERROR };
State state = POWER_ON;

void setup() {
    Serial.begin(115200);           // UART0 = debug
    display_init();
    kline_init(UART_KLINE_NUM, KLINE_TX_GPIO, KLINE_RX_GPIO);
    can_mcp2515_init(CAN_SPI_HOST, CAN_SPI_CS_GPIO, CAN_INT_GPIO);
    sd_card_init(SD_SPI_HOST, SD_CS_GPIO);
}

void loop() {
    switch (state) {
        case POWER_ON:
            if (digitalRead(IGNITION_SENSE_GPIO)) state = SELF_TEST;
            break;
        case SELF_TEST:
            if (self_test_all()) state = IDLE;
            else state = ERROR;
            break;
        case IDLE:
            // tampilkan layar home, tunggu tombol
            break;
        case KONEKSI:
            if (detect_protocol_auto()) state = SELEKSI_PROTOKOL;
            break;
        // ... dst (mirip main.c pada Section 3.4)
    }
    delay(10);
}
```

**Perintah build & flash (Arduino IDE):**
1. Buka `obd-scanner.ino` di Arduino IDE.
2. Pilih board: `ESP32 Dev Module`.
3. Pilih port serial (COM3 / /dev/ttyUSB0).
4. Klik **Upload** (tekan BOOT0 saat upload jika diperlukan).
5. Buka **Serial Monitor** 115200 baud untuk debug.

---

### 4.3 Pinout & Manajemen SPI (3 device pada SPI)

ESP32 DevKit V4 memiliki 2 host SPI (HSPI/VSPI = SPI2_HOST dan SPI3_HOST). Tiga device SPI (MCP2515, microSD, TFT ILI9341) membutuhkan **chip select terpisah**:

| Device | SPI Host | CS GPIO | Catatan |
|--------|----------|---------|---------|
| MCP2515 (CAN) | SPI2_HOST | GPIO 5 | INT ke GPIO 4 |
| microSD | SPI3_HOST | GPIO 13 | FATFS, file backup ECU |
| TFT ILI9341 | SPI2_HOST | GPIO 15 | DC = GPIO 2, Reset = GPIO 22 |

Jika dua device condivide satu SPI host (mis. MCP2515 dan TFT pada SPI2_HOST), pastikan transaksi SPI diwasi dengan `spi_device_acquire_bus` / `spi_device_release_bus` (ESP-IDF) atau `SPI.beginTransaction` / `SPI.endTransaction` (Arduino) dan CS yang benar-benar terpisah per device.

UART alokasi:
- **UART0** — Debug console (USB-Serial bawaan DevKit), jangan pakai untuk K-Line.
- **UART1** — K-Line ISO 9141-2 (TX=GPIO17, RX=GPIO16).
- **UART2** — Recovery UART (BOOT0 + sistem bootloader), 115200 baud.

---

### 4.4 Partisi Flash & Rollback (ESP-IDF)

ESP32 menggunakan partisi flash bukan alamat absolut seperti STM32. Dual-slot rollback dilakukan melalui partisi `factory` (slot A) dan `ota_0` (slot B) plus `otadata` untuk menandai slot mana yang aktif. Bootloader ESP-IDF otomatis menangani fallback jika slot yang ditandai rusak.

Untuk citra cadangan firmware alat, gunakan partisi terpisah atau simpan ke spiffs/microSD. Verifikasi HMAC-SHA256 wajib sebelum flash (mirip Section 3.7).

---

### 4.5 Troubleshooting Build (ESP32)

| Masalah | Kemungkinan penyebab | Solusi |
|---------|----------------------|--------|
| `idf.py: command not found` | ESP-IDF tidak terinstal / export.sh belum dijalankan | Jalankan `$IDF_PATH/export.sh` atau pakai Docker espressif/idf |
| Gagal flash, timeout | ESP32 tidak dalam mode download | Tekan BOOT0 + RESET, lalu flash ulang |
| Output serial berantakan | Baud rate monitor salah | Setel 115200 baud di Serial Monitor / idf.py monitor |
| MCP2515 tidak terdeteksi | CS pin salah atau SPI bus bentrok | Periksa CS GPIO (GPIO 5), pisahkan dari SD dan TFT |
| K-Line tidak terinit | UART pin salah atau logika tidak terbalik | Periksa KLINE_INVERT_LOGIC, TX/RX GPIO (17/16) |
| TFT tidak muncul | CS/DC pin salah atau SPI speed terlalu tinggi | Periksa pinout TFT, turunkan clock SPI |

---

**File terkait:** `MASTER_PLAN.md` (BOM lengkap di Bab 4.8), `MODUL_MIKROKONTROLER.md` (referensi modul MCU), `DTC_TABEL_MOTOR.md` (tabel DTC per merek motor).

---

## 5. Catatan Implementasi (ESP32 DevKit V4)

- **K-Line optocoupler (PC817):** periksa logika terbalik dan sesuaikan `KLINE_INVERT_LOGIC` di `pinout.h`. Validasi timing 5-baud dan data 10.400 baud dengan osiloskop sebelum ke lapangan; hitung penundaan propagasi optocoupler.
- **SPI multi-device:** MCP2515 (CAN), microSD, dan TFT ILI9341 perlu chip select GPIO yang terpisah. Kelola transaksi SPI agar tidak bentrok antar device.
- **UART alokasi:** UART0 = debug (jangan pakai untuk K-Line), UART1 = K-Line, UART2 = recovery (115200 baud).
- **Partisi flash:** ESP32 menggunakan partisi (factory / ota_0 / ota_1 / NVS / spiffs), bukan alamat absolut. Dual-slot rollback via partisi `factory` dan `ota_0` dengan `otadata`.
- **Tanda tangan citra:** verifikasi HMAC-SHA256 wajib sebelum flash ECU atau firmware alat.
- **Motorcycle adaptor:** setiap merek motor membutuhkan kabel adaptor proprietary 2–6 pin; siapkan wiring diagram service manual.
- **Watchdog:** ESP32 watchdog (MWDT / TGWDT) harus dikalibrasi untuk reset otomatis jika firmware terjebak.
- **FreeRTOS:** ESP32 menjalankan FreeRTOS dual-core bawaan; gunakan tugas terpisah untuk UI, protokol, logging SD, dan manajemen daya (hati-hati dengan payload ESP-IDF vs Arduino FreeRTOS).
- **WiFi/BLE:** tersedia untuk aplikasi pendamping nirkabel (opsional, non-aktifkan pada fase prototipe awal untuk hemat daya).

---

**File pendukung:** `MASTER_PLAN.md` (master plan lengkap), `DTC_TABEL_MOTOR.md` (tabel DTC per merek motor).