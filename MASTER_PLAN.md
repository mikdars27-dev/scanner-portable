# MASTER PLAN TEKNIS — Scanner OBD1/OBD2/Euro5 Portabel

**Dokumen ini ditulis dalam Bahasa Indonesia; istilah teknis dalam Bahasa Inggris.**  
Target: alat diagnostik kendaraan portabel yang mendukung OBD1 (pre-1996), OBD2/EOBD, dan Euro5/EOBD, dengan fitur baca/hapus DTC, streaming data hidup (PID), uji semua sensor, uji aktuator (pompa bahan bakar, injektor, relai), cadangan dan penulisan file ECU, dan pemulihan via UART.

---

## 1. Ringkasan Eksekutif & Visi

### 1.1 Visi produk

Menghadirkan alat diagnostik kendaraan portabel, handal, dan terjangkau untuk bengkel dan teknisi di Indonesia, mampu berkomunikasi dengan mobil kelas OBD1, OBD2/EOBD, Euro5/EOBD serta sepeda motor (termasuk Euro 4/5 motor), dengan antarmuka fisik multi-protokol (transceiver CAN, antarmuka K-Line tolerant 12 V berbasis optocoupler, dan UART untuk pemulihan), cadangan file ECU dengan verifikasi tanda tangan, dan pemulihan alat melalui bootloader sistem MCU.

### 1.2 Misi

1. Dukungan multi-protokol fisik: transceiver CAN (ISO 11898-2, 500 kbps), antarmuka K-Line (ISO 9141-2/KWP2000) tolerant tegangan baterai, dan UART recovery.
2. Dukungan modular OBD1 per pabrikan (GM ALDL 8192 baud, Ford EEC-IV VPW/PWM, Toyota blink TE1/TC, Honda LED) dengan adaptor kabel dan tabel kode tersendiri.
3. Stack diagnostik OBD2/Euro5: SAE J1979 mode $01–$0A, ISO 15765-4 CAN, KWP2000 ISO 14230-4, dan UDS ISO 14229 (termasuk sesi diagnostik, seed/key, DTC, kontrol output).
4. Keamanan operasi: interlock tes aktuator, verifikasi tanda tangan sebelum menulis ECU, cadangan file sebelum flash, dan cut-off daya saat kontak mati.

### 1.3 Fitur inti (29 fitur)

| No | Fitur | Deskripsi singkat |
|----|-------|-------------------|
| 1 | Baca DTC | Baca DTC terkonfirmasi ($03), pending ($07), permanent ($0A), dan via UDS $19 |
| 2 | Hapus DTC | Hapus DTC dan informasi terkait ($04) atau UDS $14 |
| 3 | Data hidup / PID streaming | Streaming PID mode $01 dengan dukungan grup A–D dan parameter monitor Euro5 |
| 4 | Uji semua sensor | Hasil uji sensor O2 ($05) dan monitor non-kontinu ($06) |
| 5 | Uji pompa bahan bakar | Kontrol komponen dua arah mode $08 atau UDS $2F |
| 6 | Uji injektor | Kontrol injektor via mode $08 atau UDS $2F |
| 7 | Uji relai | Kontrol output relai via mode $08 atau UDS $2F |
| 8 | Backup / baca file ECU | Baca citra ECU (UDS $2E atau dump bootloader) dengan header metadata dan tanda tangan |
| 9 | Tulis file ECU | Tulis citra ECU (UDS $2F) dengan verifikasi tanda tangan, checksum, cadangan, dan rollback |
| 10 | Pemulihan via UART | Pemulihan alat via bootloader sistem MCU (BOOT0 + UART) dan pemulihan ECU via bootloader/SWD-JTAG |
| 11 | Adaptor konnector motor | Kabel adaptor proprietary 2–6 pin per merek motor (Honda, Yamaha, Suzuki, Kawasaki, dll.) |
| 12 | Baca kode blink motor | Membaca DTC via lampu indikator blink pada motor tua tanpa OBD |
| 13 | Deteksi protokol motor otomatis | Deteksi otomatis K-Line / CAN / proprietary pada sepeda motor |
| 14 | Tabel DTC motor spesifik | Tabel kode DTC dan deskripsi per merek motor |
| 15 | Uji sensor motor spesifik | Sensor suhu oli, posisi throttel, posisi gear, sensor kecepatan roda, sensor posisi keriting |
| 16 | Membaca Informasi Sistem | Baca VIN, nama ECU, versi software via J1979 $09 atau UDS $22 |
| 17 | Freeze Frame | Baca parameter mesin saat DTC pertama kali muncul via Mode $02 |
| 18 | DTC Query | Pencarian built-in deskripsi kode DTC tanpa manual eksternal |
| 19 | ECU Program | Reset ECU, sesi programming, seed/key, transfer data via UDS $11/$10/$27/$36 |
| 20 | Keluaran Sinyal | Simulasi sinyal sensor ke ECU via UDS $2F IOControl atau J1979 Mode $08 |
| 21 | Pengujian Aktuator Lain | Uji coil pembuangan, pompa bahan bakar 2, kipas radiator |
| 22 | Modul ABS | Baca/hapus DTC ABS, uji komponen rem hidraulik/elektronik |
| 23 | Deteksi Baterai | Tes tegangan baterai, resistansi internal, estimasi CCA via ADC |
| 24 | Tes Start-up/Cranking | Monitoring penurunan tegangan baterai saat cranking |
| 25 | Tes Pengisian Daya | Monitoring performa alternator/regulator saat idle dan mesinet-rev |
| 26 | Penyesuaian CO Idle | Sesuaikan campuran bahan bakar kental/kurang saat idle via IOControl |
| 27 | Pembaruan Online | Update firmware via WiFi OTA atau USB/SD, update tabel DTC motor via SD |
| 28 | Program Kunci (Smartkey) | Duplikasi/pendaftaran remote/keyless baru via scanner dengan sesi pemrograman dan security access (seed/key UDS $27) |
| 29 | Deteksi Smart Key | Deteksi keberadaan smart key, status baterai, dan kekuatan sinyal (RSSI) melalui sistem immobilizer |

### 1.4 Diferensiasi

- Tidak terbatas pada interprete ELM327: kontrol mentah (raw) K-Line via L9637 memungkinkan baca-tulis dan pemulihan ECU.
- File cadangan ECU dengan verifikasi tanda tangan (SHA-256/HMAC) disimpan di kartu SD.
- Pemulihan alat itu sendiri via bootloader sistem MCU (BOOT0 + UART, umumnya 115200 baud).
- Desain portabel IP54, layar TFT sentuh, baterai, tahan lingkungan bengkel.

### 1.5 Target pengguna

Bengkel resmi dan independen, teknisi OBD, pengembang firmware ECU, dan hobiis modifikasi kendaraan di Indonesia.

### 1.6 Batasan cakupan

Tidak janjikan cakupan 100% semua merek/ECU. Beberapa fungsi terkunci di balik seed/key dan sesi pabrikan. Pemrograman ECU yang mengubah emisi dapat melanggar regulasi (lihat Bab 14). Alat ini bukan pengganti peralatan dealer resmi.

---

## 2. Cakupan Produk & Matriks Kendaraan Target

### 2.1 Matriks kendaraan dan protokol

| Kelas | Contoh | Protokol fisik | Kecepatan | Konektor | Fitur yang didukung | Catatan |
|-------|--------|----------------|-----------|----------|---------------------|---------|
| OBD1 GM | GM ALDL ECM (pre-1996) | Kawat tunggal ALDL (TTL/baterai tergantung ECM) | 8192 baud (beberapa ECM 160 baud) | DLC 12-pin non-standar, bawah dasbor | Baca data spesifik pabrikan, kode kesalahan | Adaptor kabel dan tabel data per model/tahun; tidak ada PID standar |
| OBD1 Ford | Ford EEC-IV | VPW/PWM | Self-test KOEO/KOER | DLC non-standar | Kode self-test mesin mati dan nyala | Tabel kode per tahun |
| OBD1 Toyota | Toyota | Terminal TE1/TC | Blink lampu Check Engine | DLC service | Blink kode DTC | Pendekatkan TE1 ke E1 |
| OBD1 Honda | Honda | LED ECU + connector pemeriksaan layanan | Blink LED | Service check connector | Kode blink ECU | Biasanya di bawah kursi penumpang |
| OBD2 bensin | Mobil penumpang post-1996 / EOBD | K-Line ISO 9141-2 atau CAN ISO 15765-4 | 10.4 kbps (K-Line) / 500 kbps (CAN) | DLC16 SAE J1962 | Mode $01–$0A (PID, DTC, freeze frame, readiness, tes sensor, kontrol komponen) | Pin 6/14 CAN, pin 7 K-Line, pin 15 L-Line |
| OBD2 diesel | Mobil komersial ringan diesel | Sama dengan OBD2 bensin | Sama | DLC16 | Sama + parameter diesel | Periksa jenis protokol per pabrikan |
| Euro5 / ERO | Tipe baru sejak 2009, pendaftaran baru sejak 2011 | CAN ISO 15765-4 (+ DoIP pada beberapa) | 500 kbps CAN | DLC16 / WWH-OBD | J1979 + UDS, readiness, permanent DTC, monitor katalis/O2/heater/EVAP/EGR | Beberapa layanan terkunci pabrikan (seed/key) |
| Sepeda motor | Bebas variei (bebas vario, bebek, sport, touring) | K-Line ISO 9141-2 (dominan), beberapa CAN | 10.4 kbps (K-Line) / 500 kbps (CAN) | Konnector proprietary 2–6 pin (bukan selalu J1962), ada juga OBD2-like | Baca/hapus DTC, live data terbatas, readiness, tes sensor | Setiap pabrikan motor (Honda, Yamaha, Suzuki, Kawasaki, dll.) punya konnector dan tabel kode sendiri; Euro 4/5 motor berlaku sejak 2017/2020 |

### 2.2 Prioritas OBD1 di Indonesia (asumsi pasar)

1. GM ALDL 8192 baud — prioritas pertama
2. Ford EEC-IV VPW/PWM
3. Toyota TE1/TC blink
4. Honda LED blink

Setiap adaptor OBD1 membutuhkan kabel spesifik pabrikan dan tabel kode PID/tabel kesalahan tersendiri.

### 2.3 Di luar cakupan (out of scope)

- Pemrograman kunci/immobilizer.
- Pemrograman ECU pasca-pasarkan di luar mode OBD standar (chipping/remap di luar jaminan).
- Diagnosa transmisi/ABS/airbag di luar PID standar yang diexpose ECU.
- Jaminan 100% cakupan semua merek Euro5.

### 2.4 Fitur khusus sepeda motor

| Fitur motor | Deskripsi | Catatan |
|-------------|-----------|---------|
| Adaptor konnector motor | Kabel adaptor proprietary 2–6 pin per merek (Honda 2-pin, Yamaha 4-pin, Suzuki 6-pin, dll.) | Perlu wiring diagram service manual per merek/model |
| Baca kode blink motor | Membaca DTC via pola blink lampu indikator (Check Engine/FI) pada motor tua tanpa OBD | Beberapa motor tua hanya menyampaikan kode via blink, tanpa konnector digital |
| Deteksi protokol motor otomatis | Deteksi otomatis K-Line ISO 9141-2 / CAN / proprietary pada motor | K-Line dominan pada motor; beberapa motor modern pakai CAN |
| Tabel DTC motor spesifik | Tabel kode DTC dan deskripsi per merek motor | Lihat Bab 15.3 (tabel DTC per merek motor) |
| Uji sensor motor spesifik | Suhu oli, posisi throttel, posisi gear, sensor kecepatan roda, sensor posisi keriting | PID sensor bervisialisasi per merek; tidak semua diexpose via OBD |
| Pemulihan ECU motor | Pemulihan via bootloader/SWD-JTAG pada ECU motor | Tidak semua pabrikan motor membuka bootloader ECU via OBD |

Prioritas adaptor motor di Indonesia: Honda (bebas vario, beat, pcx), Yamaha (mio, nmax), Suzuki (address, satria), Kawasaki.

---

## 3. Matriks Protokol Komunikasi

### 3.1 Pinout DLC16 SAE J1962

| Pin | Fungsi | Keterangan |
|-----|--------|------------|
| 1 | Vendor | Bergantung pabrikan |
| 2 | Vendor / J1850 bus+ | |
| 3 | Vendor | |
| 4 | Ground chassis | |
| 5 | Ground signal | |
| 6 | CAN-H | ISO 15765-4 CAN high speed |
| 7 | K-Line | ISO 9141-2 |
| 8 | Vendor | |
| 9 | Vendor | |
| 10 | J1850 bus- | |
| 11 | Vendor | Sering K-Line KWP2000 |
| 12 | Vendor | |
| 13 | Vendor | |
| 14 | CAN-L | ISO 15765-4 CAN high speed |
| 15 | L-Line | ISO 9141-2 |
| 16 | Baterai + | +12 V dari kendaraan |

**Konektor sepeda motor:** banyak sepeda motor tidak menggunakan DLC16 J1962. Konnector proprietary umumnya 2–6 pin (mis. 2-pin K-Line saja, 4-pin, atau 6-pin) dengan lokasi yang bervariasi per merek/model (bawah tempuran, di balik penutup body, atau dekat kendala). Scanner perlu kabel adaptor per merek motor dan pandangan wiring diagram dari service manual. Beberapa motor modern sudah menggunakan konnector mirip OBD2 atau K-Line tunggal yang dapat diakses melalui adaptor 2 pin.

| Konektor motor (umum) | Pin | Fungsi | Catatan |
|-----------------------|-----|--------|---------|
| Honda (bebas vario, beat, pcx) | 2 | K-Line, Ground | Adaptor 2-pin khusus Honda |
| Yamaha (mio, nmax) | 4 | K-Line, Ground, +12V, TX/RX | Beberapa pakai diagnosa dealer |
| Suzuki (address, satria) | 6 | K-Line, CAN-H, CAN-L, Ground, +12V, Reset | Kombinasi K-Line/CAN |
| Kawasaki | 2–4 | K-Line / proprietary | Tergantung model dan tahun |
| Umum 2-pin | 1 | K-Line | Untuk motor tua, K-Line tunggal |
| Umum 2-pin | 2 | Ground | Referensi ground |

Lokasi konnector motor bervariasi: bawah tempuran, di balik penutup body samping, atau dekat kendala mesin. Selalu merujuk pada wiring diagram service manual sebelum mencabut atau menyambung adaptor.

### 3.2 Tabel matriks protokol

| Protokol | Lapis fisik | Kecepatan | Inisialisasi | Pin DLC16 | Format frame | Contoh |
|----------|-------------|-----------|--------------|-----------|--------------|--------|
| GM ALDL | Kawat tunggal ALDL | 8192 baud (beberapa 160 baud) | Spesifik ECM | DLC 12-pin | Data spesifik pabrikan | GM pre-OBD2 |
| Ford EEC-IV | VPW/PWM | Self-test | KOEO/KOER | DLC non-standar | Pulsa kode self-test | Ford |
| Toyota / Honda | Blink via terminal/LED | Blink visual | Pendekatkan TE1 ke E1 | Service connector | Kode blink lampu | Toyota, Honda |
| ISO 9141-2 K-Line | K-Line open-drain | 10.400 baud | 5-baud wake-up | Pin 7 (dan 15) | Byte karakter | OBD2 awal |
| KWP2000 | K-Line / CAN | Variabel | 5-baud atau fast init PWM | Pin 7/11 | Kata kunci layanan | ECU pabrikan |
| ISO 15765-4 CAN high speed | CAN diferensial | 500 kbps, 11-bit ID | Tidak perlu inisialisasi spesial | Pin 6/14 | ISO 15765-2 transport | OBD2/Euro5 modern |
| ISO 15765-4 CAN low speed | CAN diferensial | 125/50 kbps, 29-bit ID | Tidak perlu inisialisasi spesial | Pin 6/14 | ISO 15765-2 transport | Beberapa body/chassis |
| SAE J1979 | Atas K-Line/CAN | Bergantung lapis fisik | Mode $01–$0A | Pin 6/7/14 | Request 0x7DF + SID + PID; respons 0x7E8 | Semua OBD2 |
| UDS ISO 14229 | Atas K-Line/CAN | Bergantung lapis fisik | $10 sesi, $27 seed/key | Pin 6/7/14 | Request/response dengan NRC | Euro5/EOBD modern |
| WWH-OBD ISO 27145 / DoIP | CAN / Ethernet | Bergantung lapis fisik | Sesi diagnostik | DLC / DoIP gateway | Layanan diagnostik terharmonisasi | Euro5/6 pabrikan |

### 3.3 Detail inisialisasi per protokol

**K-Line ISO 9141-2 (init 5-baud):**
1. ECU menghasilkan wake-up pulse rendah 200–400 ms.
2. Tester mengirim start bit 5-baud (≈200 ms/bit).
3. ECU membalas byte inisialisasi.
4. Operasi data 10.400 baud, 8N1.
5. K-Line idle tinggi ditarik ke tegangan baterai (~12 V, 13,5–14,5 V mesin nyala); aktif/dominan ditarik rendah ≈0 V (open-drain dengan pull-up ke Vbatt).

**KWP2000 (ISO 14230-4):** inisialisasi 5-baud atau fast init PWM, lalu komunikasi berbasis kata kunci layanan.

**CAN ISO 15765-4:** tidak perlu inisialisasi spesial. Alamat fungsi 0x7DF, permintaan fisik 0x7E0–0x7E7, respons 0x7E8–0x7EF. Transport ISO 15765-2: Single Frame, First Frame, Consecutive Frame, Flow Control, block size, STmin.

**SAE J1979:** request mode $01–$0A dengan PID; respons berisi data yang diminta. PID 00 berisi daftar dukungan untuk PID 01–20; setiap grup 20 PID diikuti byte dukungan.

**UDS ISO 14229:** $10 DiagnosticSessionControl (pilih sesi default/programming/extended), $19 ReadDTCInformation, $14 ClearDTCInformation, $27 SecurityAccess (seed/key), $2E ReadDataByIdentifier, $2F IOControlByIdentifier; respons negatif NRC (contoh 0x22 kondisi tidak terpenuhi, 0x33 timeout, 0x35 tidak ada kunci, 0x78 respons tertunda).

### 3.4 Karakteristik listrik

- CAN recessif: CAN_H = CAN_L = 2,5 V (selisih 0 V).
- CAN dominan: CAN_H ≈ 3,5 V, CAN_L ≈ 1,5 V (selisih 2 V).
- Terminasi 120 Ω di kedua ujung bus.
- K-Line idle tinggi ≈ tegangan baterai (~12 V); aktif ditarik rendah ≈ 0 V.
- Perlindungan transien menurut ISO 7637, polaritas terbalik, dan pengatur daya untuk sistem kendaraan 9–16 V (load dump bisa hingga puluhan volt).

### 3.5 Format DTC (ISO 15031-6)

5 karakter: huruf pertama sistem (P powertrain, B body, C chassis, U jaringan); digit kedua 0 = standar SAE/ISO, 1 = pabrikan; digit ketiga dan keempat subsistem (contoh P03xx pengapian, P04xx sistem emisi). Freeze frame menyimpan snapshot parameter saat kesalahan terdeteksi. Readiness menunjukkan monitor emisi yang sudah selesai (kontinu dan non-kontinu). Menurut EOBD, MIL menyala setelah kesalahan terdeteksi pada dua siklus pengemudi berturut-turut.

---

## 4. Arsitektur Hardware

### 4.1 Blok diagram (Mermaid)

```mermaid
graph TB
    subgraph Kendaraan
    VCB[Konektor OBD2 DLC16 SAE J1962<br/>Pin 6 CAN-H, 14 CAN-L, 7 K-Line, 15 L-Line, 4/5 GND, 16 BAT+]
    ADAPT[Adaptor OBD1 per pabrikan<br/>GM ALDL / Ford EEC-IV / Toyota / Honda]
    end

    subgraph Scanner
    MPU[MCU ESP32-WROOM-32 DevKit V4<br/>4 MB flash, ~520 KB SRAM, 240 MHz dual-core, WiFi/BLE, 3 UART<br/>CAN via MCP2515 SPI (external)]
    CAN[Transceiver CAN SN65HVD230<br/>ISO 11898-2, 500 kbps, 3.3 V logika, hubung ke MCP2515]
    MCP2515[Kontroler CAN SPI MCP2515<br/>CS terpisah dari microSD dan TFT]
    KLINE[Antarmuka K-Line optocoupler PC817<br/>ISO 9141-2, tolerant 12 V, open-drain, logika terbalik]
    UART[UART Recovery<br/>USART + BOOT0 / USB-UART bridge (CP2102/CH340)]
    PWR[Rantai daya 12 V<br/>Fuse 10A, polaritas terbalik, TVS load dump, buck 5V, LDO 3.3V, ignition-sense PC817]
    SD[microSD SPI + FATFS<br/>Backup ECU, log diagnostik]
    DISP[TFT ILI9341 2.8" 240x320 + touch<br/>Fallback OLED SSD1306 / LCD 20x4]
    BT[HC-05 Bluetooth<br/>Aplikasi pendamping]
    end

    VCB --- MPU
    ADAPT --- MPU
    CAN --- MCP2515 --- MPU
    KLINE --- MPU
    UART --- MPU
    PWR --- MPU
    SD --- MPU
    DISP --- MPU
    BT --- MPU

    BAT[(Aku baterai kendaraan<br/>9–16 V nominal)]
    BAT --- PWR
```

### 4.2 Pilihan MCU

| MCU | Kelebihan | Kekurangan | Catatan |
|-----|-----------|------------|---------|
| ESP32-WROOM-32 DevKit V4 | WiFi/BLE terintegrasi, biaya rendah, banyak tersedia di Indonesia | Tidak ada CAN native (butuh MCP2515 via SPI), 3,3 V logika, hanya 3 UART | **Pilihan yang dipakai**; sesuai stack hardware Anda |
| STM32F407VGT6 | 3+ UART, 2 CAN internal, 1 MB flash, 192 KB RAM, 168 MHz, banyak GPIO | Tidak AEC-Q100, mahal, kurang tersedia di toko online Indonesia | Alternatif jika butuh dual CAN native |
| RP2040 | 264 KB SRAM, 133 MHz, hemat biaya | Hanya 2 UART, tidak ada CAN, flash eksternal QSPI | Fallback hemat biaya |
| STM32F103RE | 512 KB flash, 64 KB RAM, CAN internal | Lebih lambat 72 MHz | Opsi jika F407 terlalu mahal |

Catatan: tidak ada MCU kandidat yang AEC-Q100 automotive-qualified. Untuk produksi, layakkan komponen, tambahkan derating, dan validasi di -40..+85 °C (ideal +105 °C).

### 4.3 Antarmuka K-Line (modul custom berbasis optocoupler, online shop Indonesia)

- Pilihan utama: modul K-Line custom berbasis **optocoupler (PC817 atau setara)** yang banyak tersedia di online shop Indonesia (Tokopedia/Shopee/Lazada), dengan rangkaian pendukung transistor (BC547/2N2222) untuk mendorong K-Line, pull-up ke Vbatt, resistor seri, dan Zener/TVS pada garis K-Line.
- Persyaratan wajib modul: tolerant tegangan baterai (idle ~12 V, aktif ditarik rendah ≈0 V), mendukung inisialisasi 5-baud (wake-up pulse 200–400 ms), operasi data 10.400 baud 8N1, dan memiliki proteksi short-circuit/transien (ISO 7637).
- Catatan optocoupler: memberikan **isolasi galvanik** antara MCU dan kendaraan (lebih aman terhadap offset ground), tetapi menambahkan **penundaan propagasi** yang harus dipertimbangkan pada inisialisasi 5-baud dan timing data 10.400 baud. Periksa **polaritas logika** — optocoupler umumnya menghasilkan logika terbalik (inverted) — dan sesuaikan driver UART MCU (inversi via software atau inverter tambahan) sesuai.
- Catatan: verifikasi bahwa modul memberikan akses mentah (raw UART) ke K-Line, bukan hanya interprete AT-command (seperti ELM327 klon), karena fitur baca-tulis dan pemulihan ECU membutuhkan kontrol frame-level.
- Alternatif jika modul custom tidak tersedia atau memerlukan kontrol frame-level penuh: L9637 (ST) driver monolit ISO 9141, supply 4,5–36 V, mendorong K-Line pada tegangan baterai langsung, dukungan wake-up, short-circuit protection. MCU bit-bang UART termasuk init 5-baud.
- Tidak disarankan untuk read/write/recovery: ELM327 (~60.000 IDR) terbatas pada AT command dan klon berkualitas bervariasi.

### 4.4 Transceiver CAN

- Primer: SN65HVD230 (3,3 V logika native, cocok langsung dengan ESP32 3,3 V dan MCP2515, standby ultra rendah 370 µA, bus fault sekitar ±12 V).
- Alternatif: MCP2551/TJA1050 (5 V, butuh level shifter antara MCP2515 dan ESP32 3,3 V, proteksi bus ±40/+42 V).
- Isolasi galvanik: ISO1050 (~80.000 IDR) jika diperlukan untuk proteksi offset ground antar ECU.
- Catatan: MCP2515 mengontrol transceiver CAN melalui SPI; pastikan pin CS MCP2515 terpisah dari microSD dan TFT ILI9341.

### 4.5 UART recovery

- Pin USART MCU + BOOT0 untuk memasuki bootloader sistem (STM32 sistem bootloader biasanya 115200 baud, tergantung kristal).
- USB-UART bridge (CP2102/CH340) opsi untuk pemulihan via PC.
- Untuk pemulihan ECU: UART ke bootloader ECU jika didukung pabrikan, atau SWD/JTAG sebagai cadangan.

### 4.6 Rantai daya

Pin 16 baterai +12 V → sekering blade 10 A → perlindungan polaritas terbalik (1N5822 Schottky atau PMOS ideal diode) → TVS load-dump clamp (SMDJ36CA / 1.5KE36CA) → buck converter 12 V → 5 V → LDO 3,3 V (AP2112/AMS1111) untuk MCU/periferal. Input pengasupan kontak via optokopel PC817 untuk wake MCU dan cut-off agar tidak mencuri daya saat kontak mati. Budget arus sepi harus dikomposisikan minimal.

### 4.7 Penyimpanan, layar, nirkabel, konektor, casing

- microSD modul SPI + kartu 8 GB, sistem berkas FATFS, untuk backup ECU dan log diagnostik.
- Layar utama: TFT ILI9341 2,8 inci sentuh SPI 240×320. Fallback: OLED SSD1306 (~35.000 IDR) atau LCD 20×4 (~45.000 IDR).
- Nirkabel: HC-05 Bluetooth untuk aplikasi pendamping (tidak diperlukan jika MCU ESP32).
- Konektor: OBD2 DLC16 + kabel SAE J1962; adaptor OBD1 per pabrikan.
- Casing: IP54 (mobil) / IP65–IP67 (motor, tahan air hujan dan cuci motor), suhu otomotif, tahan getaran. Untuk digunakan pada sepeda motor, bentuk fisik harus kompak dan ringan, dengan kabel adaptor pendek dan stempel kabel yang kuat agar tidak goyah saat berkendara atau dicuci.

### 4.8 Tabel BOM (estimasi biaya IDR)

| No | Komponen | Peran | Qty | Estimasi IDR | Alternatif |
|----|----------|-------|-----|--------------|------------|
| 1 | ESP32-WROOM-32 DevKit V4 | MCU utama (WiFi/BLE terintegrasi) | 1 | 75.000 | STM32F407VGT6 ~220.000 |
| 2 | Modul K-Line custom | Driver K-Line ISO 9141-2 (online shop Indonesia) | 1 | 35.000 | L9637 ~130.000 / ELM327 ~60.000 |
| 3 | SN65HVD230 | Transceiver CAN | 1 | 20.000 | MCP2551/TJA1050 + level shifter |
| 4 | MCP2515 | Kontroler CAN SPI | 1 | 35.000 | Tidak perlu jika STM32 internal CAN |
| 5 | Buck converter 12→5 V | Rantai daya utama | 1 | 25.000 | TPS54331 / LM2576HV |
| 6 | LDO 3,3 V AP2112/AMS1111 | Rail 3,3 V MCU | 1 | 8.000 | Buck kedua |
| 7 | Blade fuse 10 A + holder | Proteksi overcurrent | 1 | 12.000 | PTC resettable |
| 8 | TVS SMDJ36CA / 1.5KE36CA | Load-dump clamp | 1 | 10.000 | Load-dump suppressor dedicated |
| 9 | 1N5822 / PMOS | Polaritas terbalik | 1 | 6.000 | Diode bridge |
| 10 | PC817 optokopel | Ignition-sense wake | 1 | 6.000 | Resistive divider + Zener |
| 11 | TFT ILI9341 2,8" + touch | Layar utama | 1 | 90.000 | OLED 35.000 / LCD 45.000 |
| 12 | Modul microSD SPI | Penyimpanan | 1 | 20.000 | — |
| 13 | microSD 8 GB | Media penyimpanan | 1 | 75.000 | 16 GB |
| 14 | HC-05 Bluetooth | Nirkabel | 1 | 35.000 | ESP32 integrated |
| 15 | OBD2 DLC16 + kabel | Konektor kendaraan | 1 | 60.000 | Header 16-pin + flying leads |
| 16 | Casing IP54 | Rangka portabel | 1 | 100.000 | ABS box ~40.000 |
| 17 | Pasokan, PCB, konnektor | Resistor, kapasitor, Zener, PCB | 1 | 90.000 | Perfboard |
| 18 | Kabel adaptor konnector motor | Adaptor proprietary 2–6 pin per merek motor | 1 | 25.000 | Kabel universal + flying leads |
| | **TOTAL** | | | **≈ 727.000 IDR** | (~$45 USD @ 16.000 IDR/USD) |

### 4.9 Catatan implementasi engineer

- Pisahkan chip select SPI untuk MCP2515 dan kartu microSD (dan TFT ILI9341 jika pakai SPI); kelola transaksi SPI dengan hati-hati agar tidak bentrok antar device.
- ESP32 DevKit V4 beroperasi pada logika 3,3 V. SN65HVD230 transceiver CAN native 3,3 V disarankan; jika pakai MCP2551/TJA1050 (5 V) butuh level shifter pada garis SPI dan CAN antara MCP2515 dan ESP32.
- K-Line idle ~12 V: pastikan modul K-Line optocoupler (PC817) tolerant tegangan baterai dan terisolir galvanik; optocoupler menghasilkan logika terbalik — sesuaikan driver UART ESP32 (inversi software atau inverter tambahan) dan hitung penundaan propagasi pada inisialisasi 5-baud dan data 10.400 baud.
- ESP32 memiliki 3 UART: alokasikan UART0 untuk debug console, UART1 untuk K-Line, UART2 untuk recovery (BOOT0 + UART sistem). Jika UART0 digunakan USB-Serial bridge bawaan DevKit, pertimbangkan pindahkan debug ke UART lain agar tidak bentrok dengan flashing.
- Pinout ESP32 DevKit V4 (GPIO numbering): gunakan GPIO yang bukan input-only (mis. hindari GPIO34–39 sebagai output) untuk CS SPI dan kontrol K-Line/CAN. Lihat `STRUKTUR_PROYEK_FIRMWARE.md` bagian pinout ESP32.
- Pilih build sistem ESP-IDF atau Arduino ESP32 core (bukan ARM GCC). Gunakan ESP-IDF untuk kontrol register-level dan dual-partition OTA-like rollback; gunakan Arduino untuk pengembangan lebih cepat dengan library MCP2515, ILI9341, dan SD yang sudah matang.
- 64 KB RAM minimum ketat untuk FATFS + framebuffer UI; ESP32 ~520 KB SRAM cukup, tetapi gunakan buffer kecil untuk streaming live data dan framebuffer UI.
- Validasi osiloskop untuk inisialisasi K-Line dan level CAN sebelum penggunaan lapangan.
- Casing IP54, tahan getaran dan suhu; konektor dan kabel mechanically robust karena sering dicabut dan dicokok.
- MCU tidak AEC-Q100: bagi prototipe internal diterima; bagi produk komersial perlukan uji lingkungan dan kelayakan otomotif.
- WiFi/BLE ESP32 tersedia untuk aplikasi pendamping nirkabel (opsional, tidak diutamakan pada fase prototipe awal).

---

## 5. Arsitektur Firmware

### 5.1 Layering

```
APLIKASI (UI, fitur DTC/live data/tes/file/recovery)
      ↓
LAYER PROTOKOL (J1979, KWP2000, UDS, ALDL, EEC-IV, parser blink)
      ↓
LAYER DATA LINK (driver K-Line, driver CAN, transport ISO 15765-2, driver UART)
      ↓
BSP/HAL (GPIO, UART, CAN, SPI, ADC, watchdog, flash)
```

Detail layer:

- **BSP/HAL:** driver MCU untuk GPIO, UART, CAN, SPI, ADC, watchdog, dan operasi flash dual-bank.
- **Data Link Layer:** driver K-Line (bit-bang UART, init 5-baud, wake-up), driver CAN (filter ID 0x7DF/0x7E8), transport ISO 15765-2 (Single Frame, First Frame, Consecutive Frame, Flow Control, block size, STmin), driver UART recovery.
- **Protocol Layer:** GM ALDL, Ford EEC-IV, parser blink Toyota/Honda, SAE J1979 mode $01–$0A, KWP2000, UDS ISO 14229.
- **Application Layer:** layanan DTC, live data, tes sensor/aktuator, baca-tulis file ECU, pemulihan, manajemen file SD.
- **UI Layer:** layar TFT, tombol/touch, menu, peringatan keamanan.
- **Power Management:** ignition-sense, sleep mode, pengaturan daya periferal.

### 5.2 RTOS vs bare metal

Rekomendasi: **FreeRTOS** pada STM32F407 dengan tugas terpisah untuk UI (layar/touch), komunikasi (protokol), logging (SD), dan manajemen daya. Opsi bare metal tersedia untuk varian lebih kecil/hemat biaya. Perhatikan latensi tulis SD terhadap refresh rate data hidup.

### 5.3 State machine utama

```
POWER_ON → SELF_TEST → IDLE → KONEKSI → SELEKSI_PROTOKOL → BUKA_SESI → OPERASI → TUTUP_SESI → IDLE / SLEEP
                                          ↓ERROR↓
```

Setiap transisi memiliki state ERROR dengan penanganan ulang (retry) atau pengembalian ke IDLE. Detail state:

- **POWER_ON:** inisialisasi hardware, deteksi ignition-sense.
- **SELF_TEST:** uji memori, layar, SD, transceiver.
- **IDLE:** menunggu input pengguna, tampilkan status siaga.
- **KONEKSI:** deteksi protokol otomatis atau manual, inisialisasi K-Line/CAN.
- **SELEKSI_PROTOKOL:** pilih OBD1/OBD2/UDS sesuai kendaraan.
- **BUKA_SESI:** buka sesi diagnostik (seed/key jika diperlukan).
- **OPERASI:** jalankan fitur yang dipilih (DTC, live data, tes, file).
- **TUTUP_SESI:** tutup sesi, lepaskan bus.
- **SLEEP:** matikan periferal, tunggu kontak mati.

### 5.4 Manajemen daya

Wake saat kontak menyala melalui optokopel PC817; sleep saat kontak mati; cut-off daya periferal non-esensial; indikator baterai rendah. Budget arus sepi kontak mati harus minimal untuk tidak mengosongkan aku kendaraan.

### 5.5 Keandalan firmware alat

Watchdog, dual-bank flash dengan fallback, image firmware cadangan di SD, verifikasi tanda tangan sebelum flashing alat itu sendiri.

---

## 6. Spesifikasi Detail Setiap Fitur

### 6.1 Fitur 1 — Baca DTC

- **Input:** pilihan kendaraan/protokol, mode ($03 DTC terkonfirmasi terkait emisi, $07 pending, $0A permanent, UDS $19).
- **Proses:** buka sesi diagnostik, kirim request, terima respons, parsing DTC 5 karakter, baca freeze frame jika tersedia.
- **Output:** daftar DTC dengan deskripsi, data freeze frame, status MIL dan readiness.
- **Batasan:** tabel kode pabrikan diperlukan untuk OBD1; beberapa DTC aman pabrikan.

### 6.2 Fitur 2 — Hapus DTC

- **Input:** konfirmasi pengguna, mode ($04, atau UDS $14).
- **Proses:** buka sesi, kirim perintah hapus, verifikasi respons, periksa kembali readiness.
- **Output:** status berhasil/gagal, daftar DTC sisa (jika ada).
- **Batasan:** permanent DTC tidak dapat dihapus oleh scan tool biasa; konfirmasi pengguna wajib.

### 6.3 Fitur 3 — Data hidup / PID streaming

- **Input:** pilihan PID, laju refresh, durasi streaming.
- **Proses:** kirim request mode $01 dengan PID, terima respons, konversi nilai mentah ke satuan fisik, tampilkan gauge/grafik.
- **Output:** nilai PID real-time (status pengawas, beban mesin, suhu pendingin, trim bahan bakar, kecepatan, MAF, parameter monitor Euro5), grafik time-series.
- **Batasan:** dukungan PID bergantung ECU; PID 00 diperlukan untuk cek dukungan; laju refresh terbatas oleh protokol dan bus.

### 6.4 Fitur 4 — Uji semua sensor

- **Input:** pilihan sensor (O2, katalis, heater O2, EVAP, EGR, udara sekunder, aliran buang).
- **Proses:** gunakan mode $05 (hasil uji sensor O2) dan $06 (hasil uji monitor non-kontinu), baca status monitor siap/gagal.
- **Output:** daftar sensor dengan status uji, nilai hasil, indikator readiness per monitor.
- **Batasan:** tidak semua monitor diexpose oleh semua ECU; beberapa membutuhkan siklus pengemudi tertentu.

### 6.5 Fitur 5 — Uji pompa bahan bakar

- **Input:** konfirmasi keamanan, durasi tes, mode ($08 atau UDS $2F IOControlByIdentifier).
- **Proses:** buka sesi diagnostik, aktifkan output pompa bahan bakar, pantau respons, matikan setelah timeout atau permintaan pengguna.
- **Output:** status pompa (nyala/mati), durasi tes, indikator selesai.
- **Batasan/Keselamatan:** hanya pada mesin dalam kondisi aman (biasanya mesin mati); timeout paksa default 30 detik; indikator klar; tombol STOP fisik.

### 6.6 Fitur 6 — Uji injektor

- **Input:** konfirmasi keamanan, pilihan silinder/durasi, mode $08/$2F.
- **Proses:** buka sesi, aktifkan pulsa injektor secara terkontrol, batasi durasi, matikan otomatis.
- **Output:** status injektor, jumlah pulsa, durasi, indikator selesai.
- **Batasan/Keselamatan:** interlock sama dengan pompa bahan bakar; jangan berlebihan untuk mencegah terlalu banyak bahan bakar di silinder.

### 6.7 Fitur 7 — Uji relai

- **Input:** konfirmasi keamanan, pilihan relai, mode $08/$2F.
- **Proses:** buka sesi, nyalakan/matikan output relai, indikasi status.
- **Output:** status relai (nyala/mati), indikator klar.
- **Batasan/Keselamatan:** hanya kondisi aman; timeout paksa; indikator klar.

### 6.8 Fitur 8 — Backup / baca file ECU

- **Input:** pilihan ECU, sesi diagnostik (seed/key jika perlu), lokasi penyimpanan SD.
- **Proses:** buka sesi, baca citra ECU via UDS $2E ReadDataByIdentifier atau dump memori melalui bootloader, simpan ke SD dengan header metadata.
- **Output:** file cadangan ECU (bin) dengan header metadata (VIN, nomor part ECU, ID kalibrasi, stempel waktu, protokol, versi scanner), SHA-256/HMAC.
- **Batasan:** beberapa ECU mengunci membaca memori di balik seed/key; ukuran file bergantung ECU.

### 6.9 Fitur 9 — Tulis file ECU

- **Input:** file cadangan yang diverifikasi, konfirmasi pengguna, sesi diagnostik.
- **Proses:** verifikasi tanda tangan dan checksum file, buat cadangan saat ini, tulis citra via UDS $2F atau bootloader, verifikasi pasca-tulis, dukungan rollback dual-bank.
- **Output:** status berhasil/gagal, laporan verifikasi, log flash.
- **Batasan/Keselamatan:** verifikasi tanda tangan wajib; cadangan sebelum tulis; tidak flash saat tegangan tidak stabil atau mesin berkedip; write-protect region bootloader.

### 6.10 Fitur 10 — Pemulihan via UART

- **Input:** pemilihan mode pemulihan (alat vs ECU), image firmware di SD, pin BOOT0 (untuk alat).
- **Proses (alat):** aktifkan BOOT0 + reset, bootloader sistem via UART (STM32 umumnya 115200 baud), flashing ulang image terverifikasi, dual-bank fallback.
- **Proses (ECU):** UART ke bootloader ECU jika didukung, atau SWD/JTAG cadangan.
- **Output:** status pemulihan berhasil/gagal, versi firmware setelah pemulihan.
- **Batasan:** tidak semua pabrikan membuka bootloader ECU via OBD; verifikasi tanda tangan image sebelum flash untuk mencegah ECU mati.

### 6.11 Fitur 11 — Adaptor konnector motor

- **Input:** pilihan merek/model motor, tipe konnector (2-pin/4-pin/6-pin).
- **Proses:** pasang kabel adaptor proprietary ke konnector motor, deteksi jenis konnector, inisialisasi komunikasi.
- **Output:** koneksi terdeteksi, indikator siap berkomunikasi.
- **Batasan:** setiap merek motor memerlukan kabel adaptor yang berbeda; wiring diagram diperlukan.
- **Interlock:** pastikan kontak motor mati saat mencabut/pasang adaptor.

### 6.12 Fitur 12 — Baca kode blink motor

- **Input:** pilihan merek motor tua, pola blink lampu indikator (Check Engine/FI).
- **Proses:** hitung pola blink lampu indikator, cocokkan dengan tabel kode blink per merek.
- **Output:** DTC dari kode blink, deskripsi kesalahan.
- **Batasan:** hanya pada motor tua tanpa konnector digital/OBD; akurasi bergantung kejelasan pola blink.
- **Interlock:** —

### 6.13 Fitur 13 — Deteksi protokol motor otomatis

- **Input:** konektor motor terpasang, kontak motor nyala.
- **Proses:** coba inisialisasi K-Line (5-baud), jika gagal coba CAN, jika gagal tandai sebagai proprietary/tidak dikenal.
- **Output:** protokol terdeteksi (K-Line/CAN/proprietary), indikator kecepatan komunikasi.
- **Batasan:** protokol proprietary tidak dikenali memerlukan tabel manual per merek.
- **Interlock:** —

### 6.14 Fitur 14 — Tabel DTC motor spesifik

- **Input:** DTC terbaca, merek motor.
- **Proses:** cocokkan kode DTC dengan tabel DTC spesifik per merek (Honda, Yamaha, Suzuki, Kawasaki).
- **Output:** deskripsi DTC dalam Bahasa Indonesia, kemungkinan penyebab, tindakan saran.
- **Batasan:** tabel DTC harus diperbarui terus-menerus untuk model motor baru.
- **Interlock:** —

### 6.15 Fitur 15 — Uji sensor motor spesifik

- **Input:** pilihan sensor motor (suhu oli, posisi throttel, posisi gear, kecepatan roda, posisi keriting).
- **Proses:** baca data sensor via PID proprietary atau K-Line, tampilkan nilai real-time.
- **Output:** nilai sensor spesifik motor, grafik time-series.
- **Batasan:** tidak semua sensor motor diexpose via OBD/K-Line; beberapa membuka hanya via diagnosa dealer.
- **Interlock:** —

### 6.16 Tabel ringkasan 15 fitur

| Fitur | Input | Proses utama | Output | Batasan | Interlock |
|-------|-------|--------------|--------|---------|-----------|
| Baca DTC | Kendaraan, mode | Request respons, parsing | DTC + freeze frame | Tabel pabrikan OBD1 | — |
| Hapus DTC | Konfirmasi, mode | Kirim hapus, verifikasi | Status, DTC sisa | Permanent DTC tidak bisa | Konfirmasi |
| Data hidup | PID, laju | Streaming, konversi | Gauge/grafik | Dukungan ECU | — |
| Uji sensor | Pilihan sensor | Mode $05/$06 | Status monitor | Tidak semua diexpose | — |
| Uji pompa | Konfirmasi, durasi | Kontrol output $08/$2F | Status pompa | Mesin aman | Timeout, STOP |
| Uji injektor | Konfirmasi, silinder | Kontrol pulsa | Status injektor | Batas durasi | Timeout, STOP |
| Uji relai | Konfirmasi, relai | Kontrol output | Status relai | Mesin aman | Timeout, STOP |
| Backup ECU | ECU, lokasi SD | Baca citra, simpan | File bin + tanda tangan | Seed/key | — |
| Tulis ECU | File, konfirmasi | Verifikasi, tulis, verifikasi | Laporan flash | Seed/key, tegangan stabil | Cadangan dulu |
| Pemulihan | Mode, image | Bootloader UART/SWD-JTAG | Status pulih | Tidak semua ECU terbuka | Tanda tangan |
| Adaptor motor | Merek, tipe konnector | Pasang adaptor, deteksi | Koneksi siap | Adaptor per merek | Kontak mati saat pasang |
| Baca kode blink | Merek motor tua | Hitung pola blink lampu | DTC dari blink | Hanya motor tua tanpa OBD | — |
| Deteksi protokol motor | Konektor terpasang | Coba K-Line/CAN/proprietary | Protokol terdeteksi | Proprietary tidak dikenal | — |
| Tabel DTC motor | DTC, merek motor | Cocokkan tabel per merek | Deskripsi DTC | Perlu update tabel baru | — |
| Uji sensor motor | Pilihan sensor motor | Baca PID proprietary | Nilai sensor real-time | Tidak semua diexpose | — |

---

## 7. Antarmuka Pengguna & Alur UX

### 7.1 Tata letak layar

- **Beranda:** status siaga, tombol fitur utama, indikator baterai dan koneksi.
- **Pemilihan kendaraan:** pilih merek/tahun atau deteksi otomatis protokol.
- **Pemilihan protokol:** OBD1/OBD2/UDS, K-Line atau CAN.
- **Daftar DTC:** daftar kode dengan deskripsi dan freeze frame.
- **Data hidup:** gauge dan grafik time-series untuk PID yang dipilih.
- **Menu tes aktuator:** pompa bahan bakar, injektor, relai — dengan layar peringatan penuh sebelum eksekusi.
- **Manajer file:** backup, restore, dan log diagnostik di SD.
- **Pengaturan:** bahasa, satuan, timeout, brightness.
- **Tentang:** versi, informasi lisensi, peringatan hukum.

### 7.2 Alur UX langkah demi langkah

1. Sambungkan kabel OBD2 ke DLC16 (atau adaptor OBD1 ke kendaraan).
2. Nyalakan kontak (mesin belum perlu menyala tergantung fitur).
3. Scanner mendeteksi daya, layar menyala, jalankan self-test.
4. Pilih kendaraan/protokol (atau gunakan deteksi otomatis).
5. Buka sesi diagnostik (masukkan seed/key jika diminta).
6. Pilih fitur (DTC, live data, tes, file, recovery).
7. Eksekusi dengan indikator progres pada layar.
8. Tutup sesi, cabut kabel, simpan hasil jika diperlukan.

### 7.3 Peringatan keamanan

Sebelum setiap tes aktuator dan flash ECU, tampilkan layar peringatan penuh dengan konfirmasi eksplisit, hitung mundur timeout, dan tombol STOP fisik untuk membatalkan operasi.

### 7.4 Checklist UX

- [ ] Layar mudah dibaca di cahaya ruangan dan di luar ruangan (pertimbangkan varian high-brightness/anti-glare).
- [ ] Navigasi dapat dilakukan dengan tombol fisik dan sentuh.
- [ ] Indikator progres ditampilkan selama operasi panjang (flash, streaming lama).
- [ ] Pesan kesalahan berbahasa Indonesia dengan kode kesalahan teknis.
- [ ] Konfirmasi dua kali untuk operasi destruktif (hapus DTC, tulis ECU).

---

## 8. Mekanisme Keselamatan & Interlock

### 8.1 Interlock tes aktuator

- Hanya pada mesin dalam kondisi aman (biasanya mesin mati) — selalu periksa manual pabrikan.
- Timeout paksa default 30 detik, konfigurabel.
- Indikator klar (layar + bunyi/LED) selama tes berjalan.
- Tombol darurat/stop fisik untuk membatalkan kapan saja.
- Batas arus dan sekering 10 A sebagai proteksi berlebih.
- Dilarang melakukan tes aktuator saat kendaraan berjalan di jalan raya.

### 8.2 Interlock flash ECU

- Tegangan baterai stabil (tidak selama mencabut kabel atau memutar mesin).
- Cadangan file sebelum menulis.
- Verifikasi tanda tangan (SHA-256/HMAC) dan checksum citra sebelum menulis.
- Tulis dual-bank dengan fallback/rollback.
- Write-protect region bootloader.
- Jangan flash saat suhu di luar spesifikasi operasi.

### 8.3 Perlindungan hardware

Sekering blade 10 A, perlindungan polaritas terbalik, TVS load-dump clamp, isolasi galvanik opsi (ISO1050), suhu operasi, dan tahan getaran.

### 8.4 SOP flash ECU (checklist sebelum/sesudah)

- [ ] Verifikasi identitas ECU dan nomor part.
- [ ] Buat cadangan citra ECU saat ini ke SD.
- [ ] Periksa tegangan baterai stabil (12–14 V).
- [ ] Verifikasi tanda tangan dan checksum file tujuan.
- [ ] Jalankan flash dengan verifikasi blok per blok.
- [ ] Verifikasi pasca-tulis (baca kembali dan bandingkan).
- [ ] Catat log flash dengan stempel waktu dan VIN.

---

## 9. Cadangan, Baca/Tulis File ECU, dan Pemulihan (Recovery)

### 9.1 Format file cadangan ECU

File cadangan terdiri dari:

- **Header metadata:** VIN, nomor part ECU, ID kalibrasi, stempel waktu, protokol, versi scanner.
- **Citra biner:** data memori ECU.
- **Tanda tangan:** SHA-256 atau HMAC untuk verifikasi integritas.

Disimpan di kartu microSD dengan sistem berkas FATFS.

### 9.2 Alur baca dan tulis

**Baca:** pilih ECU → buka sesi (seed/key jika perlu) → baca dengan indikator progres → verifikasi → simpan ke SD.

**Tulis:** verifikasi tanda tangan dan checksum file → cadangkan citra saat ini → tulis citra dengan verifikasi blok → verifikasi pasca-tulis → tutup sesi.

### 9.3 Pemulihan alat (scanner itu sendiri)

Aktifkan pin BOOT0 lalu reset untuk memasuki bootloader sistem MCU melalui UART (STM32 sistem bootloader umumnya 115200 baud, tergantung kristal). Lakukan flashing ulang image terverifikasi. Dual-bank flash menyediakan fallback, dan image cadangan disimpan di SD.

### 9.4 Pemulihan ECU

Jika ECU mendukung, gunakan UART ke bootloader ECU untuk pemulihan. Cadangan lain adalah SWD/JTAG. Perlu diperhatikan bahwa tidak semua pabrikan membuka bootloader ECU melalui OBD, sehingga pemulihan ECU tidak dapat dijamin untuk semua merek.

### 9.5 Checklist pemulihan

- [ ] Verifikasi identitas perangkat/ECU sebelum flash.
- [ ] Pastikan sumber daya stabil (baterai atau supply eksternal).
- [ ] Gunakan image dengan tanda tangan terverifikasi.
- [ ] Lakukan cadangan sebelum operasi tulis apa pun.
- [ ] Verifikasi setelah pemulihan selesai.

---

## 10. Rencana Uji Coba & Validasi

### 10.1 Uji bangun (bench)

- Osiloskop: verifikasi inisialisasi K-Line (5-baud, wake-up pulse 200–400 ms).
- Level dan terminasi CAN: recessif 2,5 V/2,5 V, dominan 3,5 V/1,5 V, terminasi 120 Ω.
- Simulasi transien ISO 7637 pada rantai daya.

### 10.2 Uji HIL (Hardware-in-the-Loop)

- ECU bekas + rangkaian bekas + supply stabil 13,8 V.
- Uji konformansi J1979, KWP2000, dan UDS.
- Tes aktuator pada ECU bekas tanpa risiko kendaraan nyata.

### 10.3 Uji kendaraan nyata

Matriks pengujian:

- OBD1 mobil: GM ALDL, Ford EEC-IV, Toyota, Honda.
- OBD2 mobil: mobil bensin dan diesel.
- Euro5 mobil: kendaraan tipe baru sejak 2009.
- Sepeda motor: skuter/bebas vario dan motor sport (Honda, Yamaha, Suzuki, Kawasaki, dll.), terutama yang menggunakan K-Line proprietary dan Euro 4/5 motor (seit 2017/2020).

Parameter yang diuji: readiness, baca/hapus DTC, live data, freeze frame, dan tes aktuator.

### 10.4 Uji usang dan lingkungan

- Siklus suhu operasi (-20..+60 °C).
- Uji geturan dan jatuh.
- Arus sepi saat kontak mati (hindari pengosongan aku).
- Uji EMC pre-compliance untuk produk komersial.

### 10.5 Tabel checklist uji

| Uji | Objek | Kriteria lulus | Status |
|-----|-------|----------------|--------|
| Inisialisasi K-Line | Osiloskop | Wake-up pulse 200–400 ms, byte init diterima | [ ] |
| Level CAN | Osiloskop | Recessif/dominan sesuai spesifikasi | [ ] |
| Terminasi CAN | Multimeter | 120 Ω di kedua ujung | [ ] |
| Baca DTC OBD2 | Kendaraan nyata | DTC ditampilkan dengan benar | [ ] |
| Hapus DTC | Kendaraan nyata | DTC hilang, readiness diperbarui | [ ] |
| Live data | Kendaraan nyata | PID diupdate dalam waktu wajar | [ ] |
| Tes pompa | ECU bekas | Pompa nyala/mati sesuai perintah | [ ] |
| Flash ECU | ECU bekas | Tanda tangan diverifikasi, pasca-tulis OK | [ ] |
| Pemulihan UART | Alat | Bootloader diakses, flash berhasil | [ ] |
| Arus sepi | Alat kontak mati | Arus minimal, tidak mengosongkan aku | [ ] |

---

## 11. Roadmap Pengembangan (12 Minggu, 6 Fasa)

| Fasa | Minggu | Milestone | Deliverable |
|------|--------|-----------|-------------|
| 1 Riset & komponen | 1–2 | BOM final, skema PCB, pesan komponen | Daftar komponen, skema |
| 2 Prototipe hardware | 3–4 | Uji rantai daya & proteksi, inisialisasi K-Line/CAN | Prototipe hardware berfungsi |
| 3 Firmware lapis fisik & data link | 5–6 | Driver UART/CAN/K-Line, transport ISO 15765-2 | Driver komunikasi |
| 4 Protokol diagnostik + fitur inti | 7–8 | J1979/UDS/OBD1, DTC, live data | Fitur DTC dan live data |
| 5 Fitur aplikasi & UI | 9–10 | Tes aktuator, baca-tulis file, recovery, UI lengkap | Aplikasi lengkap |
| 6 Uji & validasi | 11–12 | Kendaraan referensi, casing, baterai, dokumen, revisi | Alat siap prototipe akhir |

Milestone utama akhir fase 6: prototipe akhir yang telah diuji pada matriks kendaraan nyata, dengan dokumentasi lengkap dan laporan uji.

---

## 12. Estimasi Biaya

Total BOM diperkirakan **≈ 727.000 IDR** (sekitar $45 USD @ 16.000 IDR/USD) tanpa biaya pengemasan dan tenaga kerja.

Biaya prototipe tambahan (perkiraan):

- Pembuatan PCB dan assembly: 300.000–600.000 IDR
- Peralatan uji (osiloskop, supply, dll.): tergantung ketersediaan laboratorium
- Kontingensi 15–20%: 150.000–300.000 IDR

Estimasi total prototipe awal: **2–3 juta IDR**.

Catatan penurunan biaya volume: pembelian besar dapat menurunkan harga komponen; ketersediaan L9637 di Indonesia perlu diverifikasi (mungkin perlu impor).

---

## 13. Risiko & Mitigasi

| Risiko | Probabilitas | Dampak | Mitigasi |
|--------|--------------|--------|----------|
| ECU mati (brick) saat flash file rusak | Sedang | Tinggi | Verifikasi tanda tangan, cadangan, dual-bank, write-protect |
| Protokol tidak dikenali ECU tertentu | Tinggi | Sedang | Dukungan modular per pabrikan, daftar target validasi, jangan janjikan 100% |
| L9637 sulit didapat di Indonesia | Sedang | Tinggi | Alternatif discrete/ELM327, impor awal |
| Layar kurang terbaca di sinar matahari | Rendah | Sedang | Varian high-brightness atau anti-glare |
| Mencuri daya baterai saat kontak mati | Rendah | Tinggi | Ignition-sense cut-off, budget arus sepi minimal |
| Modul HC-05 klon bermasalah | Sedang | Rendah | Uji modul, opsi ESP32 terintegrasi |
| Regulasi/emisi jika dijual komersial | Sedang | Tinggi | Uji EMC, klaim Euro5 divalidasi kendaraan nyata, disclaimer |

---

## 14. Hukum, Etika & Kepatuhan Emisi

- Alat diagnostik bengkel umumnya tidak memerlukan sertifikat OBD, tetapi klaim cakupan Euro5 (mobil) dan Euro 4/5 (motor) harus divalidasi pada kendaraan nyata sebelum dipasarkan.
- Pemrograman ECU yang mengubah emisi (misalnya menghapus permanent DTC atau mengubah kalibrasi emisi) dapat melanggar regulasi emisi, baik untuk mobil maupun sepeda motor.
- Permanent DTC tidak dapat dihapus oleh scan tool biasa — jangan menjanikannya kepada pengguna.
- Alat hanya boleh digunakan dengan izin pemilik kendaraan, tidak dipergunakan di jalan raya saat kendaraan beraksi, dan disertai disclaimer risiko. Tanggung jawab penggunaan ada pada teknisi/pengguna.
- Untuk sepeda motor di Indonesia, perhatikan regulasi emisi motor (Euro 4 berlaku untuk tipe baru sejak 2017, Euro 5 sejak 2020) serta standar pengujian kelayakan emisi yang berlaku.

---

## 15. Glosarium & Referensi

### 15.1 Glosarium

| Istilah | Arti |
|---------|------|
| DTC | Diagnostic Trouble Code — kode kesalahan diagnostik 5 karakter |
| PID | Parameter ID — pengidentifikasi parameter data hidup |
| MIL | Malfunction Indicator Lamp — lampu indikator kerusakan |
| Readiness | Indikator kesiapan monitor emisi |
| Freeze frame | Snapshot parameter saat kesalahan terdeteksi |
| K-Line | Satu kabel diagnostik open-drain ISO 9141-2 |
| CAN | Controller Area Network — bus diferensial otomotif |
| UDS | Unified Diagnostic Services ISO 14229 |
| KWP2000 | Keyword Protocol 2000 ISO 14230-4 |
| ALDL | Assembly Line Diagnostic Link — protokol GM OBD1 |
| NRC | Negative Response Code — respons kesalahan diagnostik |
| Seed/Key | Mekanisme keamanan UDS untuk akses sesi terpadu |
| WWH-OBD | World-Wide Harmonized OBD ISO 27145 |
| DoIP | Diagnostics over Internet Protocol |
| HIL | Hardware-in-the-Loop — uji dengan hardware nyata |
| Load Dump | Sinkronisasi tegangan tinggi saat beban terputus tiba-tiba |
| OBD1/OBD2/EOBD/Euro5 | Generasi standar diagnosa kendaraan |
| DLC/J1962 | Data Link Connector — konektor 16-pin standar OBD2 |

### 15.2 Referensi

- https://tnlab.org/en/sae-j1979-2017-obd-diagnostic-test-modes-explained-for-engineers/ (SAE J1979 mode $01–$0A)
- https://en.wikipedia.org/wiki/OBD-II_PIDs (PID, format DTC, readiness)
- https://en.wikipedia.org/wiki/CAN_bus (CAN bus, ISO 15765-4)
- https://www.picoauto.com/library/application-notes/k-line-decoding (K-Line ISO 9141-2)
- https://www.iso.org/obp/ui/en/#!iso:std:72439:en (UDS/KWP2000 ISO 14229/14230)
- https://leadfootautomotive.com/car-electronics/obd2-scanner/is-obd2-mandatory-in-europe/ (Euro5/EOBD)
- https://www.autopi.io/blog/what-is-wwh-obd/ (WWH-OBD ISO 27145)
- https://imatrixsys.com/engineering/can-bus-connectors/obd2-pinout/ (pinout OBD2)
- https://github.com/z51bob/8192-Baud-ALDL-for-Arduino (GM ALDL 8192 baud)
- https://lukeskaff.com/projects/car/gm-obd-i-obd1-aldl-microcontroller-lcd-interface-scan-tool/ (DLC OBD1)
- https://www.st.com/en/automotive-analog-and-power/l9637.html (L9637 K-Line driver)

---

## 16. Appendix: Pinout per Protokol & Contoh Rangkaian

### 16.1 Pinout DLC16 (ulang dari Bab 3.1)

Pin 4/5 ground, pin 6 CAN-H, pin 7 K-Line, pin 14 CAN-L, pin 15 L-Line, pin 16 baterai +12 V; pin 1, 2, 3, 8, 9, 10, 11, 12, 13 bergantung pabrikan/vendor.

### 16.2 Mapping pin per protokol

| Protokol | Pin yang digunakan | Keterangan |
|----------|-------------------|------------|
| GM ALDL | DLC 12-pin (non-J1962) | Adaptor kabel spesifik GM |
| Ford EEC-IV | DLC non-standar | VPW/PWM, adaptor spesifik Ford |
| Toyota / Honda | DLC service | Terminal TE1/TC atau LED ECU |
| ISO 9141-2 K-Line | Pin 7 (dan 15) | Open-drain, pull-up ke Vbatt |
| KWP2000 | Pin 7/11 | Kata kunci layanan |
| ISO 15765-4 CAN | Pin 6, 14 | CAN-H, CAN-L, terminasi 120 Ω |
| SAE J1979 | Pin 6/7/14 | Atas K-Line atau CAN |
| UDS | Pin 6/7/14 | Atas K-Line atau CAN, seed/key |

### 16.3 Contoh rangkaian (deskripsi sambungan)

**K-Line dengan modul optocoupler (PC817):** Vbatt (pin 16) menjadi pull-up K-Line melalui resistor dan Zener/TVS proteksi. Sisi kendaraan (K-Line pin 7 DLC) diisolasi melalui optocoupler ke sisi MCU (3,3 V). Transistor pendukung (BC547/2N2222) mendorong K-Line rendah saat transmit. Pin RX/TX modul disambung ke UART MCU. Perhatikan logika terbalik dari optocoupler — inversi dilakukan di firmware driver UART atau dengan inverter tambahan. Tambahkan Zener/TVS pada garis K-Line untuk transien.

**CAN dengan SN65HVD230:** Vcc 3,3 V. Pin CANH ke pin 6 DLC, pin CANL ke pin 14 DLC. Resistor terminasi 120 Ω di antara CANH dan CANL di ujung scanner. Pin RXD/TXD ke controller CAN internal MCU. Tambahkan TVS pada bus CAN.

**Rantai daya:** Pin 16 → sekering 10 A → diode polaritas terbalik → TVS load dump → buck 12→5 V → LDO 3,3 V. Optokopel PC817 membaca pengasupan kontak untuk wake/cut-off.

**UART recovery:** Header 4 pin (BOOT0, USART TX, USART RX, GND) dengan tombol reset. Untuk bootloader sistem STM32, aktifkan BOOT0 lalu reset, flashing via UART 115200 baud.

Catatan: rangkaian lengkap harus diverifikasi oleh engineer hardware dengan skema EMC dan kelistrikan kendaraan sebelum produksi komersial.
