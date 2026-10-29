# REFERENSI MODUL MIKROKONTROLER — Scanner OBD1/OBD2/Euro5 & Motor

Dokumen ini adalah referensi modul mikrokontroler yang perlu Anda siapkan untuk membangun scanner portabel. Semua modul ini umumnya tersedia di online shop Indonesia (Tokopedia, Shopee, Lazada, Toko Elektronik, dll.) atau melalui import.

Pilihan utama yang direkomendasikan oleh master plan adalah **ESP32 DevKit V4 + MCP2515 CAN via SPI** karena biaya rendah, WiFi/BLE terintegrasi, banyak tersedia di online shop Indonesia, dan cukup untuk seluruh fitur scanner (DTC, live data, tes aktuator, backup/tulis ECU, recovery).

---

## 1. Pilihan Utama: ESP32 DevKit V4 + MCP2515 CAN via SPI

| Spesifikasi | Detail |
|-------------|--------|
| Modul | ESP32-WROOM-32 DevKit V4 + Modul MCP2515 CAN |
| Core | Dual-core Xtensa LX6, 240 MHz |
| Flash | 4 MB (terintegrasi pada modul ESP32) |
| SRAM | ~520 KB |
| UART | 3 (K-Line, recovery, debug) |
| CAN | **Tidak bawaan** — MCP2515 melalui SPI |
| Nirkabel | WiFi + BLE terintegrasi |
| Tegangan | 3,3 V |

**Keuntungan:**
- WiFi/BLE terintegrasi untuk aplikasi pendamping nirkabel
- Harga relatif murah dan banyak tersedia di online shop Indonesia
- 3 UART memisahkan K-Line, recovery UART, dan debug UART
- FreeRTOS dual-core bawaan untuk tugas UI, protokol, dan logging paralel

**Kekurangan:**
- Tidak ada CAN internal — perlu modul MCP2515 via SPI (konflik SPI dengan microSD/TFT butuh CS terpisah)
- 3,3 V logika — perlu level shifter jika pakai transceiver CAN 5 V (MCP2551/TJA1050); SN65HVD230 (3,3 V) disarankan
- Tidak AEC-Q100
- Karena pakai MCP2515 eksternal, performa CAN sedikit lebih rendah dari CAN internal STM32

**Di mana beli (Indonesia):**
- Tersedia melimpah di online shop Indonesia (Tokopedia/Shopee/Lazada) — cari "ESP32 DevKit V4" dan "modul MCP2515 CAN"
- ESP32 DevKit ≈ 45.000 – 70.000 IDR; modul MCP2515 ≈ 35.000 IDR

**Estimasi harga total:** ≈ 80.000 – 105.000 IDR (ESP32 + MCP2515).

**Status:** **Pilihan yang dipakai** — sesuai stack hardware komitmen (ESP32 DevKit V4 + MCP2515 + K-Line PC817 + microSD + TFT ILI9341).

---

## 2. Alternatif Lengkap: STM32F407VGT6

| Spesifikasi | Detail |
|-------------|--------|
| Modul | STM32F407VGT6 Minimum System Board / Discovery Board |
| Core | ARM Cortex-M4F, 168 MHz |
| Flash | 1 MB |
| SRAM | 192 KB |
| UART | 3+ (digunakan untuk K-Line + UART Recovery + debug) |
| CAN | 2 x CAN 2.0B internal (digunakan untuk OBD2/Euro5 CAN) |
| GPIO | Melimpah (untuk TFT, tombol, SD, ignition-sense, BOOT0) |
| Tegangan operasional | 3,3 V |
| Rentang suhu | -40 .. +85 °C (industrial, tidak AEC-Q100) |
| Paket | LQFP100 (VGT6) |

**Keuntungan:**
- Dua CAN internal — tidak perlu MCP2515 eksternal untuk CAN
- Flash 1 MB dan RAM 192 KB cukup untuk FreeRTOS + UI framebuffer + protokol lengkap
- 3+ UART memisahkan K-Line, recovery UART, dan debug UART
- Dukungan besar dari STM32 HAL / libopencm3 / Arduino STM32 core

**Kekurangan:**
- Tidak AEC-Q100 automotive-qualified (cukup untuk prototipe dan bengkel; untuk produk komersial perlu uji lingkungan)
- 3,3 V logika — perlu level shifter jika pakai transceiver CAN 5 V (MCP2551/TJA1050); SN65HVD230 (3,3 V) disarankan
- Kurang tersedia di toko online Indonesia dibanding ESP32

**Di mana beli (Indonesia):**
- Online shop elektronika (Tokopedia/Shopee/Lazada) — cari "STM32F407VGT6 minimum system" atau "STM32F407 discovery"
- Toko komponen elektronika di Jakarta/Bandung/Surabaya
- Import dari LCSC / DigiKey / Mouser jika tidak tersedia lokal

**Estimasi harga:** ≈ 220.000 – 350.000 IDR tergantung vendor dan apakah sudah termasuk board minimum system.

---

## 3. Alternatif Murah: RP2040 (Raspberry Pi Pico)

| Spesifikasi | Detail |
|-------------|--------|
| Modul | Raspberry Pi Pico / Pico-compatible board |
| Core | ARM Cortex-M0+, 133 MHz, dual-core |
| Flash | 2 MB QSPI eksternal |
| SRAM | 264 KB |
| UART | 2 (tepat memenuhi minimum: 1 untuk K-Line, 1 untuk recovery) |
| CAN | **Tidak bawaan** — perlu MCP2515 via SPI |
| Tegangan | 3,3 V |

**Keuntungan:**
- Harga sangat murah
- RAM 264 KB cukup untuk protokol ringan + UI sederhana

**Kekurangan:**
- Hanya 2 UART — tidak ada UART tersisa untuk debug yang nyaman
- Tidak ada CAN internal — perlu MCP2515
- Tidak AEC-Q100
- Ekosistem library protokol OBD lebih kecil dari STM32

**Estimasi harga:** ≈ 55.000 – 85.000 IDR (Pico + MCP2515).

---

## 4. Alternatif Jika STM32F407 Terlalu Mahal: STM32F103RE

| Spesifikasi | Detail |
|-------------|--------|
| Modul | STM32F103RE Minimum System Board |
| Core | ARM Cortex-M3, 72 MHz |
| Flash | 512 KB |
| SRAM | 64 KB |
| UART | 3 |
| CAN | 1 x CAN 2.0B internal |
| Tegangan | 3,3 V |

**Catatan:** Memenuhi floor minimum (256 KB flash / 64 KB RAM) tetapi lebih lambat dari F407 dan hanya 1 CAN internal. Cukup untuk prototipe satu protokol (K-Line atau CAN pada satu waktu).

**Estimasi harga:** ≈ 90.000 – 140.000 IDR.

---

## 5. Tidak Direkomendasikan sebagai MCU Utama

| Modul | Alasan |
|-------|--------|
| Arduino Uno / Nano (ATmega328P) | Flash/RAM terlalu kecil, tidak ada CAN, tidak cukup untuk protokol OBD lengkap |
| Arduino Mega 2560 | SRAM hanya 8 KB (< 64 KB minimum), tidak ada CAN internal, logika 5 V perlu level shifter |
| STM32F103C8T6 "Blue Pill" | Flash 64 KB dan SRAM 20 KB di bawah floor minimum 256 KB / 64 KB |

---

## 6. Tabel Perbandingan

| Kriteria | STM32F407VGT6 | ESP32 + MCP2515 | RP2040 + MCP2515 | STM32F103RE |
|----------|---------------|-----------------|------------------|-------------|
| CAN internal | 2 x CAN 2.0B | Tidak (MCP2515) | Tidak (MCP2515) | 1 x CAN 2.0B |
| UART | 3+ | 3 | 2 | 3 |
| Flash | 1 MB | 4 MB | 2 MB | 512 KB |
| SRAM | 192 KB | ~520 KB | 264 KB | 64 KB |
| Kecepatan | 168 MHz | 240 MHz | 133 MHz | 72 MHz |
| Nirkabel | Tidak | WiFi + BLE | Tidak | Tidak |
| Harga est. IDR | 220–350 ribu | 80–105 ribu | 55–85 ribu | 90–140 ribu |
| Tersedia ID | Sedang | Sangat banyak | Banyak | Sedang |
| Cocok untuk | Prototipe lengkap | Versi nirkabel, hemat biaya | Versi paling murah | Prototipe menengah |

---

## 7. Rekomendasi Pembelian (Pertama Kali)

Jika Anda ingin membangun prototipe yang sesuai 1:1 dengan kode firmware di `STRUKTUR_PROYEK_FIRMWARE.md` (adaptasi ESP32):

**Paket A (Direkomendasikan — stack hardware yang dipakai):**
1. ESP32-WROOM-32 DevKit V4 × 1
2. Modul MCP2515 CAN (SPI) × 1
3. Modul transceiver CAN SN65HVD230 × 1
4. Modul K-Line optocoupler (PC817, custom online shop Indonesia) × 1
5. Modul microSD card SPI × 1
6. Kartu microSD 8 GB × 1
7. Modul TFT ILI9341 2,8 inch + sentuh × 1
8. Konektor OBD2 DLC16 + kabel SAE J1962 × 1
9. Kabel adaptor konnector motor (2–6 pin) × 1–3 (per merek target)
10. Buck converter 12→5 V + LDO 3,3 V × 1
11. Sekering blade 10 A + TVS load dump + dioda polaritas terbalik × 1
12. Optokopel PC817 (ignition-sense wake) × 1
13. Casing IP54/IP65 × 1

**Paket B (Alternatif lengkap — dual CAN internal, sesuai kode STM32 asli):**
1. STM32F407VGT6 Minimum System Board × 1
2. Modul transceiver CAN SN65HVD230 × 1
3. Sisa komponen sama seperti Paket A (kecuali MCP2515)

---

## 8. Catatan Kompatibilitas dengan Firmware

Kode firmware di `STRUKTUR_PROYEK_FIRMWARE.md` ditulis dengan asumsi:
- **MCU utama: ESP32 DevKit V4 + MCP2515** (pinout GPIO dan driver sesuai `config/pinout.h` Section 3.1)
- Jika Anda memilih **STM32F407** atau **RP2040**, perlu menyesuaikan:
  - Driver UART/CAN/SPI di `drivers/` (MCP2515 SPI → CAN internal STM32)
  - Pin mapping di `config/pinout.h` (ESP32 GPIO numbering → STM32 HAL GPIO_PIN_x / PORT)
  - Build sistem: ESP-IDF / Arduino ESP32 → ARM GCC (Makefile/CMake untuk STM32)
- Semua modul beroperasi pada logika 3,3 V; transceiver CAN 5 V (MCP2551/TJA1050) perlu level shifter atau ganti dengan SN65HVD230 (3,3 V native).
- ESP32 tidak memiliki dual-bank flash bawaan seperti STM32 linker script; gunakan partisi ESP-IDF (factory / ota_0 / ota_1) untuk rollback firmware.

---

**File terkait:** `MASTER_PLAN.md` (BOM lengkap di Bab 4.8), `STRUKTUR_PROYEK_FIRMWARE.md` (kode dan arsitektur firmware).