# SKEMA WIRING — Scanner OBD1/OBD2/Euro5 & Motor

Dokumen ini berisi skema rangkaian antar komponen hardware untuk scanner OBD portabel berbasis **ESP32 DevKit V4 + MCP2515 CAN via SPI**. Semua pin mapping sesuai `config/pinout.h` (source of truth) dan `config/config.h`.

> ⚠️ **Peringatan keselamatan:** Rangkaian ini terhubung langsung ke sistem immobilizer dan ECU kendaraan. Pastikan pemasangan dilakukan oleh teknisi terlatih. Pemrograman ECU yang mengubah emisi dapat melanggar regulasi setempat.

---

## 1. Daftar Komponen

| # | Komponen | Jenis | Tegangan | Catatan |
|---|----------|-------|----------|---------|
| 1 | ESP32-WROOM-32 DevKit V4 | Mikrokontroler utama | 3,3 V | Dual-core 240 MHz, WiFi+BLE |
| 2 | Modul MCP2515 CAN | Kontroler CAN via SPI | 3,3 V | CS = GPIO 5, INT = GPIO 4 |
| 3 | Transceiver CAN SN65HVD230 | Transceiver CAN bus | 3,3 V | Kompatibel logika 3,3 V ESP32 |
| 4 | Modul K-Line optocoupler PC817 | Antarmuka K-Line ISO 9141-2 | 12 V → 3,3 V | TX=GPIO17, RX=GPIO16, logika terbalik |
| 5 | Modul microSD card SPI | Penyimpanan cadangan ECU/log | 3,3 V | CS = GPIO 13 |
| 6 | Modul TFT ILI9341 2,8" + sentuh | Display utama | 3,3 V | CS=15, DC=2, RST=22, BL=21 |
| 7 | Tombol fisik 4× (UP/DOWN/SELECT/BACK) | Navigasi UI | 3,3 V | GPIO 26/27/32/33 |
| 8 | Konektor OBD2 DLC16 (SAE J1962) | Antarmuka kendaraan | 12 V / 5 V | Pin 6/14 = CAN, Pin 7 = K-Line |
| 9 | Buck converter 12→5 V | Regulasi daya utama | 12 V in, 5 V out | Daya sistem |
| 10 | LDO 3,3 V | Regulasi daya logika MCU | 5 V in, 3,3 V out | ESP32 dan periferal |
| 11 | TVS load dump + sekering 10 A | Perlindungan daya | 12 V | Anti lonjakan tegangan baterai |
| 12 | Optokopel PC817 (ignition-sense) | Deteksi kontak kendaraan | 12 V → 3,3 V | GPIO 34 (input-only) |
| 13 | Voltage divider + ADC | Pemantauan baterai | 0–3,3 V | GPIO 35 (ADC1, input-only) |

---

## 2. Tabel Pinout Lengkap (ESP32 DevKit V4)

### 2.1 UART

| Fungsi | UART | TX GPIO | RX GPIO | Kecepatan | Catatan |
|--------|------|---------|---------|-----------|---------|
| Debug console | UART0 | GPIO 1 | GPIO 3 | 115200 | Terkait USB-Serial DevKit, **jangan** untuk K-Line |
| K-Line ISO 9141-2 | UART1 | GPIO 17 | GPIO 16 | 10400 | Via PC817, `KLINE_INVERT_LOGIC = 1` |
| Recovery UART | UART2 | — | — | 115200 | BOOT0 + sistem bootloader |

### 2.2 SPI — MCP2515 (CAN)

| Sinyal | ESP32 GPIO | MCP2515 Pin | Catatan |
|--------|------------|-------------|---------|
| SCK | GPIO 18 | SCK | SPI2_HOST / VSPI |
| MOSI | GPIO 23 | SI/SDI | |
| MISO | GPIO 19 | SO/SDO | |
| CS | GPIO 5 | CS | Terpisah dari SD dan TFT |
| INT | GPIO 4 | INT | Interrupt CAN ke ESP32 |

### 2.3 SPI — microSD card

| Sinyal | ESP32 GPIO | microSD Pin | Catatan |
|--------|------------|-------------|---------|
| SCK | GPIO 14 | CLK | SPI3_HOST / HSPI |
| MOSI | GPIO 12 | CMD/DI | |
| MISO | GPIO 25 | DO | |
| CS | GPIO 13 | CS/DAT3 | Terpisah dari MCP2515 dan TFT |

### 2.4 SPI — TFT ILI9341

| Sinyal | ESP32 GPIO | TFT Pin | Catatan |
|--------|------------|---------|---------|
| SCK | GPIO 18 | CLK | Shares SPI2_HOST dengan MCP2515 (CS terpisah) |
| MOSI | GPIO 23 | SDA/SDI | |
| MISO | GPIO 19 | SDO | Opsional (TFT biasanya write-only) |
| CS | GPIO 15 | CS | |
| DC | GPIO 2 | DC/RS | Data/Command select |
| Reset | GPIO 22 | RST | |
| Backlight | GPIO 21 | LED/BL | PWM untuk kecerahan |

### 2.5 Tombol Fisik (Navigasi UI)

| Tombol | ESP32 GPIO | Mode | Catatan |
|--------|------------|------|---------|
| UP | GPIO 26 | INPUT_PULLUP | Navigasi menu naik |
| DOWN | GPIO 27 | INPUT_PULLUP | Navigasi menu turun |
| SELECT | GPIO 32 | INPUT_PULLUP | Konfirmasi / OK |
| BACK | GPIO 33 | INPUT_PULLUP | Kembali / STOP tes aktuator |

### 2.6 Power & System

| Fungsi | ESP32 GPIO | Catatan |
|--------|------------|---------|
| Ignition-sense | GPIO 34 | Input-only, via PC817, deteksi kontak ON |
| BOOT0 | GPIO 0 | Pilih mode bootloader (reset saat tekan) |
| ADC baterai | GPIO 35 | ADC1 input-only, voltage divider untuk 12V |
| Buck enable | — | (tidak digunakan pada build saat ini) |

---

## 3. Diagram Wiring Antar Komponen

```
                         +12 V BATERAI KENDARAAN
                              |
                    +---------+---------+
                    |                   |
                 [TVS]               [Sekering 10A]
   Load Dump      |                   |
   Proteksi       |                   |
                    |                   |
                    v                   v
              +-----+-----+     +-------+-------+
              | Buck Conv |     |               |
              | 12 → 5 V  |     |   (cadangan)  |
              +-----+-----+     +---------------+
                    |
                    v
              +-----+-----+
              |   LDO     |
              | 5 → 3,3 V |<─────────────────────┐
              +-----+-----+                      |
                    |                            |
       +------------+------------+               |
       |                         |               |
   +---+---+                +----+----+          |
   | ESP32 |                | MCP2515 |          |
   | DevKit|                |  CAN    |          |
   | V4    | 3,3 V          |   SPI   |          |
   +---+---+                +----+----+          |
       |                         |               |
       | SCK/MOSI/MISO/CS/INT    |               |
       +------------+------------+               |
                    |                            |
                    v                            v
              +-----+-----+              +-------+-------+
              | SN65HVD230 |              |   OBD2 DLC16 |
              | CAN Trans. |              |   (J1962)    |
              +-----+-----+              +-------+-------+
                    |                            |
             CAN_H / CAN_L                      |
                    |                            |
                    +------------+---------------+
                                 |
                                 v
                           [Kendaraan / ECU]

K-Line Antarmuka (via PC817):
  ESP32 GPIO17 (TX) ──> [PC817 LED] ──> K-Line (Pin 7 OBD2)
  K-Line (Pin 7) ──> [PC817 Opto] ──> ESP32 GPIO16 (RX)
                Pull-up ke +12 V melalui resistor

Tombol Fisik:
  GPIO26 (UP) ──> [Tombol UP] ──> GND
  GPIO27 (DOWN) ──> [Tombol DOWN] ──> GND
  GPIO32 (SELECT) ──> [Tombol SELECT] ──> GND
  GPIO33 (BACK) ──> [Tombol BACK] ──> GND
  (Semua pakai INPUT_PULLUP di ESP32)

microSD (SPI3_HOST):
  GPIO14 (SCK) ──> microSD CLK
  GPIO12 (MOSI) ──> microSD DI
  GPIO25 (MISO) ──> microSD DO
  GPIO13 (CS) ──> microSD CS

TFT ILI9341 (SPI2_HOST, shares dengan MCP2515):
  GPIO18 (SCK) ──> TFT CLK
  GPIO23 (MOSI) ──> TFT SDA
  GPIO15 (CS) ──> TFT CS
  GPIO2 (DC) ──> TFT DC
  GPIO22 (RST) ──> TFT RST
  GPIO21 (BL) ──> TFT Backlight (PWM)

Ignition-Sense (via PC817):
  +12 V Kontak ──> [PC817 LED] ──> GND (melalui resistor)
  PC817 Output ──> ESP32 GPIO34 (INPUT)

ADC Baterai:
  +12 V Baterai ──> [Voltage Divider] ──> ESP32 GPIO35 (ADC1)
                (Rasio 11:1, jadi 12V → ~1,1V di ADC)
```

---

## 4. Konektor OBD2 DLC16 (SAE J1962)

| Pin | Sinyal | Koneksi ke ESP32 / Komponen | Catatan |
|-----|--------|----------------------------|---------|
| 1 | Manufacturer specific | — | Tidak digunakan |
| 2 | SAE J1850 PWM+ | — | Tidak digunakan |
| 3 | Manufacturer specific | — | Tidak digunakan |
| 4 | Chassis Ground | GND | Ground kendaraan |
| 5 | Signal Ground | GND | Ground sinyal referensi |
| 6 | CAN High (ISO 15765-4) | SN65HVD230 CAN_H | 500 kbps |
| 7 | K-Line (ISO 9141-2) | PC817 → GPIO16/GPIO17 | 10.4 kbps |
| 8 | Manufacturer specific | — | Tidak digunakan |
| 9 | Manufacturer specific | — | Tidak digunakan |
| 10 | SAE J1850 PWM− | — | Tidak digunakan |
| 11 | Manufacturer specific | — | Tidak digunakan |
| 12 | SAE J1850 VPW | — | Tidak digunakan |
| 13 | Manufacturer specific | — | Tidak digunakan |
| 14 | CAN Low (ISO 15765-4) | SN65HVD230 CAN_L | 500 kbps |
| 15 | L-Line (ISO 9141-2) | — | Tidak digunakan (beberapa OBD2) |
| 16 | Battery Power (+12 V) | → Buck Conv → 5V/3.3V | Daya sistem scanner |

---

## 5. Rangkaian Antarmuka Detail

### 5.1 K-Line (PC817 Optocoupler)

```
ESP32 GPIO17 ──> 220Ω ──> [PC817 Anode]
[PC817 Cathode] ──> GND

K-Line (OBD2 Pin 7) ──> 1kΩ ──> [PC817 Collector]
[PC817 Emitter] ──> GND
K-Line ──> 560Ω ──> +12 V (pull-up)

ESP32 GPIO16 ──> [PC817 Emitter, sisi output]
                (atau via optocoupler kedua untuk isolasi penuh)
```

**Catatan:** `KLINE_INVERT_LOGIC = 1` karena optokopel membalikkan logika (aktif rendah pada sisi MCU = K-Line ditarik rendah).

### 5.2 CAN (MCP2515 + SN65HVD230)

```
ESP32 GPIO18 (SCK) ──> MCP2515 SCK
ESP32 GPIO23 (MOSI) ──> MCP2515 SI
ESP32 GPIO19 (MISO) ──> MCP2515 SO
ESP32 GPIO5 (CS) ──> MCP2515 CS
ESP32 GPIO4 (INT) ──> MCP2515 INT

MCP2515 TXD ──> SN65HVD230 TXD (D1)
MCP2515 RXD ──> SN65HVD230 RXD (R1)
SN65HVD230 CAN_H ──> OBD2 Pin 6
SN65HVD230 CAN_L ──> OBD2 Pin 14
SN65HVD230 VCC ──> 3,3 V (ESP32)
SN65HVD230 GND ──> GND
```

**Catatan:** SN65HVD230 dipilih karena kompatibel logika 3,3 V native ESP32 tanpa level shifter. Jika menggunakan MCP2551/TJA1050 (5 V), perlu level shifter antara MCP2515 dan transceiver.

### 5.3 Ignition-Sense (PC817)

```
+12 V Kontak ──> 10kΩ ──> [PC817 Anode]
[PC817 Cathode] ──> GND

[PC817 Collector] ──> 3,3 V (via pull-up 10kΩ)
[PC817 Emitter] ──> GND
[PC817 Collector] ──> ESP32 GPIO34 (INPUT)
```

Ketika kontak ON: LED optokopel menyala, transistor conduc­t, GPIO33 dibaca LOW.
Ketika kontak OFF: transistor tidak conduc­t, GPIO33 dibaca HIGH (via pull-up).

### 5.4 ADC Pemantauan Baterai (Voltage Divider)

```
+12 V Baterai ──> 100kΩ ──>+──> 10kΩ ──> GND
                         |
                         v
                    ESP32 GPIO35 (ADC1)
```

Rasio pembagi: (100k + 10k) / 10k = 11:1 → `ADC_DIVIDER_RATIO = 11.0`
12 V baterai → ~1,09 V pada ADC (aman di bawah 3,3 V range ESP32).

---

## 6. Distribusi Daya

```
+12 V Baterai Kendaraan
        |
    [Sekering 10A]
        |
    [TVS Load Dump]
        |
        +------------------> K-Line pull-up (via resistor)
        |
        +------------------> Ignition-sense (via PC817)
        |
        v
    [Buck Converter 12→5 V]
        |
        +------------------> TFT Backlight (5V)
        |
        v
    [LDO 5→3,3 V]
        |
        +------------------> ESP32 VCC (3,3V)
        +------------------> MCP2515 VCC (3,3V)
        +------------------> SN65HVD230 VCC (3,3V)
        +------------------> microSD VCC (3,3V)
        +------------------> TFT VCC (3,3V)
        |
        GND (semua komponen bersama ground)
```

---

## 7. Perhatian Spesial

### 7.1 Konflik SPI (3 device pada 2 host SPI)

ESP32 DevKit V4 memiliki 2 host SPI:
- **SPI2_HOST (VSPI):** Digunakan oleh MCP2515 (CS=GPIO5) dan TFT (CS=GPIO15)
- **SPI3_HOST (HSPI):** Digunakan oleh microSD (CS=GPIO13)

Ketika MCP2515 dan TFT condivide SPI2_HOST, pastikan transaksi SPI dilindungi dengan `SPI.beginTransaction()` / `SPI.endTransaction()` (Arduino) atau `spi_device_acquire_bus()` / `spi_device_release_bus()` (ESP-IDF), dan setiap device memiliki CS GPIO yang terpisah.

### 7.2 Logika Terbalik K-Line

Modul optokoupler PC817 biasanya membalikkan logika. Pastikan `KLINE_INVERT_LOGIC = 1` di `config/pinout.h` dan validasi timing 5-baud (±200 ms) serta data 10.400 baud dengan osiloskop sebelum pemasangan ke kendaraan.

### 7.3 Tegangan Logika 3,3 V

Semua komponen logika beroperasi pada 3,3 V. Jika menggunakan transceiver CAN 5 V (MCP2551/TJA1050), perlu level shifter atau ganti dengan **SN65HVD230** (3,3 V native) yang sudah direkomendasikan dalam skema ini.

### 7.4 GPIO Input-Only

GPIO 34 (ignition-sense) dan GPIO 35 (ADC) adalah input-only pada ESP32. Jangan mengkonfigurasikannya sebagai output.

### 7.5 Proteksi Daya

- Selalu pasang sekering 10A di sisi +12 V input
- Pasang TVS dioda untuk proteksi load dump (lonjakan tegangan saat busai mati)
- Pasang dioda polaritas terbalik untuk proteksi terhadap pemasangan baterai terbalik

---

## 8. File Terkait

- `config/pinout.h` — Mapping GPIO aktual (source of truth untuk wiring)
- `config/config.h` — Konstanta sistem (baud rate, timeout, ADC)
- `MODUL_MIKROKONTROLER.md` — Referensi modul mikrokontroler dan BOM
- `STRUKTUR_PROYEK_FIRMWARE.md` — Struktur folder dan arsitektur firmware
- `MASTER_PLAN.md` — Master plan teknis (29 fitur)

---

*Skema wiring ini berdasarkan `config/pinout.h` versi saat ini. Jika pin mapping diubah, perbarui dokumen ini agar tetap sinkron.*
