# WIRING LENGKAP — 3 Board ESP32 (Project Jepang)

Board: **ESP32 DEVKIT V1 (WROOM, 30 pin)** untuk ketiganya.
Komunikasi antar board = **ESP-NOW (nirkabel)**, jadi **tidak ada kabel antar board**.
Tabel di bawah = wiring **di dalam tiap board** (komponen ke ESP32-nya sendiri).

> ⚠️ ATURAN UMUM
> 1. **Semua GND jadi satu** (common ground) dengan GND ESP32.
> 2. **Pin ADC ESP32 maksimal 3.3V.** Sensor analog yang output-nya bisa >3.3V
>    (MQ-2, MG811, pH) butuh pembagi tegangan / modul yang sudah aman 3.3V.
> 3. Pin **input-only** ESP32: GPIO 34/35/36/39 — hanya bisa baca (tidak bisa OUTPUT).

---

## BOARD 1 — SENSOR
Tugas: baca 9 sensor → kirim ke ALCURA. Tidak mengontrol apa pun.

### Analog (ADC) — pin input-only
| Sensor | Pin sensor | → ESP32 | VCC |
|---|---|---|---|
| MQ-2 #1 (asap/gas) | AO | **GPIO 32** | 5V (heater) |
| MQ-2 #2 (asap/gas) | AO | **GPIO 34** | 5V (heater) |
| MG811 (CO₂) | AO | **GPIO 35** | 5V |
| UV sensor | AO | **GPIO 36 (VP)** | 3.3V |
| pH meter | AO | **GPIO 39 (VN)** | 5V modul |
| TDS meter | AO | **GPIO 33** | 3.3–5V |

### Digital & warna
| Sensor | Pin | → ESP32 | VCC |
|---|---|---|---|
| DHT11 (suhu/lembap) | DATA | **GPIO 18** | 3.3V |
| TCS3200 (warna) | S0 | **GPIO 16** | 3.3V |
| TCS3200 | S1 | **GPIO 13** | |
| TCS3200 | S2 | **GPIO 14** | |
| TCS3200 | S3 | **GPIO 15** | |
| TCS3200 | OUT | **GPIO 27** | |
| TCS3200 | LED | **GPIO 26** | |

### I2C (3 sensor numpuk di 1 bus)
| Sensor | Alamat | SDA | SCL | VCC |
|---|---|---|---|---|
| AHT21 (suhu/lembap) | 0x38 | **GPIO 21** | **GPIO 22** | 3.3V |
| MLX90614 (suhu objek) | 0x5A | GPIO 21 | GPIO 22 | 3.3V |
| ENS160 (VOC/eCO₂/AQI) | 0x53 | GPIO 21 | GPIO 22 | 3.3V |

```
                ┌──────────── ESP32 (BOARD SENSOR) ───────────┐
  Analog AO ───►│ 32 34 35 36 39 33                           │
  DHT11 DATA ──►│ 18                                          │
  TCS3200    ──►│ 16 13 14 15 27 26                           │
  I2C SDA/SCL ─►│ 21  22  ◄── AHT21 + MLX90614 + ENS160       │
  3.3V / 5V / GND ke tiap sensor (GND disatukan)              │
                └─────────────────────────────────────────────┘
```

---

## BOARD 2 — RELAY (2 Kipas + 2 Pompa udara)
Tugas: terima perintah dari ALCURA → gerakkan relay 4-channel.

### ESP32 → Modul Relay 4-Channel (sisi logika)
| Relay | → ESP32 | Fungsi |
|---|---|---|
| IN1 | **GPIO 19** | Kipas 1 |
| IN2 | **GPIO 23** | Kipas 2 |
| IN3 | **GPIO 18** | Pompa udara 1 |
| IN4 | **GPIO 21** | Pompa udara 2 |
| VCC | **5V (Vin)** | daya modul relay |
| GND | **GND** | |

### Modul Relay → Kipas/Pompa (sisi daya, pakai adaptor terpisah)
Tiap channel relay punya 3 terminal sekrup: **COM – NO – NC**. Pakai **COM** & **NO**.

```
  Adaptor 12V (+) ──► COM (channel 1)
  NO (channel 1)  ──► (+) Kipas 1
  Adaptor 12V (−) ─────────────────► (−) Kipas 1      ← langsung ke kipas
```
Ulangi pola yang sama untuk Kipas 2, Pompa 1, Pompa 2 di channel 2/3/4.

> ⚠️ Kipas & pompa **TIDAK** ambil daya dari ESP32. Dayanya dari **adaptor sendiri**
> (sesuaikan tegangan komponen, mis. 12V). ESP32 hanya menggerakkan pin IN.
> Default kode `RELAY_ACTIVE_LOW = true`. Kalau ON/OFF terbalik, ubah ke `false`.

```
        ┌──────── ESP32 (BOARD RELAY) ────────┐      ┌─ Relay 4CH ─┐
        │ 19 ─────────────────────────────────┼─IN1─►│ ch1  COM/NO ├─► Kipas 1 (+12V)
        │ 23 ─────────────────────────────────┼─IN2─►│ ch2  COM/NO ├─► Kipas 2
        │ 18 ─────────────────────────────────┼─IN3─►│ ch3  COM/NO ├─► Pompa 1
        │ 21 ─────────────────────────────────┼─IN4─►│ ch4  COM/NO ├─► Pompa 2
        │ 5V ─────────────────────────────────┼─VCC  │             │
        │ GND ────────────────────────────────┼─GND  └─────────────┘
        └─────────────────────────────────────┘   Daya 12V kipas/pompa = adaptor terpisah
```

---

## BOARD 3 — LAMPU
Tugas: terima perintah dari ALCURA → atur 5 grup lampu + brightness.
Hardware: **2× ring WS2812B (12 LED, 5V)** + **4× strip addressable (30 LED, 12V)**.

### Pin DATA ke ESP32
| Bagian | Pin | → ESP32 |
|---|---|---|
| Ring (rantai 2 ring) | DI ring-1 | **GPIO 4** |
| Strip (rantai 4 strip) | DI strip-1 | **GPIO 5** |

### Chaining (DO → DI sambung berurutan)
```
  Ring : ESP32 GPIO 4 ─► Ring1.DI ;  Ring1.DO ─► Ring2.DI
  Strip: ESP32 GPIO 5 ─► Strip1.DI ; Strip1.DO ─► Strip2.DI ─► Strip3.DI ─► Strip4.DI
```

### Daya
| Bagian | Tegangan | Sumber | GND |
|---|---|---|---|
| 2× Ring | **5V** | dari ESP32 (5V & GND) | satukan |
| 4× Strip | **12V** | **adaptor 12V terpisah** | **GND adaptor WAJIB disatukan dgn GND ESP32** |

```
        ┌──────── ESP32 (BOARD LAMPU) ────────┐
        │ GPIO 4 ─► Ring1.DI ─►(DO)─► Ring2.DI │   Ring: 5V & GND dari ESP32
        │ GPIO 5 ─► Strip1.DI ─►...─► Strip4   │   Strip: 12V dari ADAPTOR
        │ 5V  ─► (+) Ring                      │
        │ GND ─► GND Ring  +  GND Adaptor 12V  │ ◄── semua GND jadi satu
        └──────────────────────────────────────┘
                 Adaptor 12V (+) ─► (+) Strip
```

> ⚠️ **GND ESP32 + GND adaptor 12V HARUS nyambung**, kalau tidak data strip ngaco/tidak nyala.
> ⚠️ Kalau warna strip salah → ubah `NEO_RBG` di kode (coba `NEO_RGB`/`NEO_GRB`). Kalau kedip → `NEO_KHZ400`.
> ⚠️ Kalau strip 12V tidak stabil membaca data 3.3V, tambah **level shifter** (74HCT125) di jalur data GPIO 5.
> Di kode, sesuaikan `RING_NUM`, `STRIP_NUM`, `RING_COUNT_EACH`, `STRIP_COUNT_EACH` bila jumlahnya beda.
> Saat ini: 2 ring × 12 + 4 strip × 30 = **144 LED**, dibagi 5 grup lampu.

---

## DAYA TIAP BOARD ESP32
Tiap ESP32 cukup diberi daya lewat **port USB**-nya (ke adaptor/charger HP/powerbank,
tidak harus ke laptop). Laptop hanya diperlukan **sekali** saat upload program.

| Board | Daya ESP32 | Daya komponen |
|---|---|---|
| Sensor | USB 5V | dari pin 3.3V/5V ESP32 |
| Relay | USB 5V | **adaptor terpisah** untuk kipas/pompa |
| Lampu | USB 5V | 5V ESP32 (atau adaptor 5V bila LED banyak) |
