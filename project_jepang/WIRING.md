# WIRING LENGKAP — 2 Board ESP32 (Project Jepang)

Board: **ESP32 DEVKIT V1 (WROOM, 30 pin)** untuk keduanya (Sensor + Kontrol).
Komunikasi antar board = **ESP-NOW (nirkabel)**, jadi **tidak ada kabel antar board**.
Tabel di bawah = wiring **di dalam tiap board** (komponen ke ESP32-nya sendiri).

> ⚠️ ATURAN UMUM
> 1. **Semua GND jadi satu** (common ground) dengan GND ESP32.
> 2. **Pin ADC ESP32 maksimal 3.3V.** Sensor analog yang output-nya bisa >3.3V
>    (MQ-2, MG811, pH) butuh pembagi tegangan / modul yang sudah aman 3.3V.
> 3. Pin **input-only** ESP32: GPIO 34/35/36/39 — hanya bisa baca (tidak bisa OUTPUT).

---

## BOARD 1 — SENSOR
Tugas: baca semua sensor → kirim ke ALCURA via ESP-NOW (tampil di TFT) **dan** push
semua data ke Firebase RTDB. Tidak mengontrol apa pun.
Parameter yang dikirim (13): **pH, TDS, Water Temp, Water Level, Turbidity, UV Index,
Air Temp, Humidity, H2, CH4, CO, CO2, O2** (gas H2/CH4/CO2/O2 = simulasi, CO = sensor FF asli).

### Analog (ADC1) — pin input-only
| Sensor | Pin sensor | → ESP32 | Parameter | VCC |
|---|---|---|---|---|
| pH meter | AO | **GPIO 39 (VN)** | pH Level | 5V modul |
| TDS meter | AO | **GPIO 33** | TDS | 3.3–5V |
| UV sensor | AO | **GPIO 36 (VP)** | UV Index | 3.3V |
| Gas FF (asap/CO) | AO | **GPIO 34** | CO (real) | 5V heater |
| Gas MP-02 | AO | **GPIO 35** | (cadangan) | 5V heater |
| Gas MQ-135 | AO | **GPIO 32** | (cadangan) | 5V heater |

### Digital, ultrasonik & warna
| Sensor | Pin | → ESP32 | Parameter | VCC |
|---|---|---|---|---|
| Ultrasonik HC-SR04 | TRIG | **GPIO 5** | Water Level | 5V |
| Ultrasonik HC-SR04 | ECHO | **GPIO 18** | (echo, bagi tegangan ke 3.3V) | |
| DHT11 (suhu/lembap) | DATA | **GPIO 4** | Air Temp + Humidity | 3.3V |
| TCS3200 (warna→turbidity) | S0 | **GPIO 16** | Turbidity | 3.3V |
| TCS3200 | S1 | **GPIO 13** | | |
| TCS3200 | S2 | **GPIO 14** | | |
| TCS3200 | S3 | **GPIO 15** | | |
| TCS3200 | OUT | **GPIO 27** | | |
| TCS3200 | LED | **GPIO 26** | | |

### I2C
| Sensor | Alamat | SDA | SCL | Parameter | VCC |
|---|---|---|---|---|---|
| MLX90614 (suhu objek IR) | 0x5A | **GPIO 21** | **GPIO 22** | Water Temp | 3.3V (pull-up 4.7k WAJIB) |

```
                ┌──────────── ESP32 (BOARD SENSOR) ───────────┐
  Analog AO ───►│ 39 33 36 34 35 32                           │
  Ultrasonik ──►│ TRIG 5  ECHO 18                             │
  DHT11 DATA ──►│ 4                                           │
  TCS3200    ──►│ 16 13 14 15 27 26                           │
  I2C SDA/SCL ─►│ 21  22  ◄── MLX90614 (pull-up 4.7k)         │
  3.3V / 5V / GND ke tiap sensor (GND disatukan)              │
                └─────────────────────────────────────────────┘
```

> ⚠️ **WiFi + ESP-NOW di 1 radio:** board ini konek WiFi (Firebase) sekaligus broadcast
> ESP-NOW. Channel ESP-NOW ikut channel router. ALCURA/relay/lampu dikunci **channel 1**,
> jadi **set router/hotspot ke channel 1** agar data sampai ke ALCURA.

---

## BOARD 2 — KONTROL (Relay + Lampu)  [GABUNGAN]
Tugas: terima perintah dari ALCURA → gerakkan **relay 4-channel** (2 kipas + 2 pompa
udara) **dan** **5 lampu addressable** (1 ring + 4 strip) + brightness global.
Sketch: `board_kontrol/` (gabungan dari board_relay + board_lampu lama).

### A) ESP32 → Modul Relay 4-Channel (sisi logika)
| Relay | → ESP32 | Fungsi | State (ESP-NOW) |
|---|---|---|---|
| IN1 | **GPIO 16** | Kipas 1 | `fanState[0]` |
| IN2 | **GPIO 22** | Kipas 2 | `fanState[1]` |
| IN3 | **GPIO 21** | Pompa udara 1 | `pumpState[0]` |
| IN4 | **GPIO 17** | Pompa udara 2 | `pumpState[1]` |
| VCC | **5V (Vin)** | daya modul relay | |
| GND | **GND** | | |

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

### B) ESP32 → Lampu addressable (5 output DATA terpisah)
| Lampu | LED | Pin DATA → ESP32 | State (ESP-NOW) | Warna order |
|---|---|---|---|---|
| Ring | 12 | **GPIO 4** | `lampState[0]` | `NEO_GRB` |
| Strip 1 | 30 | **GPIO 18** | `lampState[1]` | `NEO_RBG` |
| Strip 2 | 30 | **GPIO 19** | `lampState[2]` | `NEO_RBG` |
| Strip 3 | 30 | **GPIO 23** | `lampState[3]` | `NEO_RBG` |
| Strip 4 | 30 | **GPIO 25** | `lampState[4]` | `NEO_RBG` |

`brightness` (0–100 dari ALCURA) = kecerahan **global** semua lampu (kiri=redup, kanan=terang).
Warna nyala default = **putih** (255,255,255).

### Daya lampu
| Bagian | Tegangan | Sumber | GND |
|---|---|---|---|
| Ring | **5V** | dari ESP32 (5V & GND) | satukan |
| 4× Strip | **12V** | **adaptor 12V terpisah** | **GND adaptor WAJIB disatukan dgn GND ESP32** |

```
        ┌──────────── ESP32 (BOARD KONTROL) ────────────┐      ┌─ Relay 4CH ─┐
        │ 16 ───────────────────────────────────────────┼─IN1─►│ ch1 COM/NO ├─► Kipas 1
        │ 22 ───────────────────────────────────────────┼─IN2─►│ ch2 COM/NO ├─► Kipas 2
        │ 21 ───────────────────────────────────────────┼─IN3─►│ ch3 COM/NO ├─► Pompa 1
        │ 17 ───────────────────────────────────────────┼─IN4─►│ ch4 COM/NO ├─► Pompa 2
        │  4 ─► Ring.DI                                  │ 5V─►VCC  GND─►GND │
        │ 18 ─► Strip1.DI   19 ─► Strip2.DI              │      └─────────────┘
        │ 23 ─► Strip3.DI   25 ─► Strip4.DI              │
        │ 5V  ─► (+) Ring      GND ─► GND Ring + GND adaptor 12V (disatukan)   │
        └────────────────────────────────────────────────┘
          Daya kipas/pompa = adaptor terpisah · Strip (+) = adaptor 12V
```

> ⚠️ **GND ESP32 + GND adaptor 12V HARUS nyambung**, kalau tidak data strip ngaco/tidak nyala.
> ⚠️ Kalau warna strip salah → ubah `NEO_RBG` di kode (coba `NEO_RGB`/`NEO_GRB`). Kalau kedip → `NEO_KHZ400`.
> ⚠️ Kalau strip 12V tidak stabil membaca data 3.3V, tambah **level shifter** (74HCT125) di jalur data tiap strip.
> Di kode, sesuaikan `lampCounts[]` bila jumlah LED tiap fixture beda.

---

## DAYA TIAP BOARD ESP32
Tiap ESP32 cukup diberi daya lewat **port USB**-nya (ke adaptor/charger HP/powerbank,
tidak harus ke laptop). Laptop hanya diperlukan **sekali** saat upload program.

| Board | Daya ESP32 | Daya komponen |
|---|---|---|
| Sensor | USB 5V | dari pin 3.3V/5V ESP32 |
| Kontrol | USB 5V | **adaptor terpisah** untuk kipas/pompa (12V) & strip (12V); ring 5V dari ESP32 |
