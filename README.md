# ALCURA Display System

Antarmuka (UI) berbasis **ESP32 + layar TFT 480×320 dengan touchscreen** untuk
monitoring kualitas **udara & air** serta kontrol **lampu** dan **kipas**.
Dibuat dengan library [`TFT_eSPI`](https://github.com/Bodmer/TFT_eSPI) dan
digambar sepenuhnya secara prosedural (tanpa LVGL).

> **Status:** UI sudah jalan penuh di perangkat. Data sensor masih **dummy** —
> langkah selanjutnya adalah menghubungkan sensor asli (rencana via **ESP-NOW**).

---

## 🔌 File firmware yang di-upload ke ESP32

**`esp32_tft_project/esp32_tft_project.ino`** ← INI satu-satunya file yang di-flash ke board.

File `.ino` lain di repo ini **BUKAN** firmware aktif:
- `test_serial/test_serial.ino` → sketch kecil untuk tes komunikasi serial saja.
- `.kimchi/docs/*.ino` → backup/baseline versi lama (arsip, jangan dipakai).

---

## Hardware

| Komponen   | Keterangan                                            |
|------------|-------------------------------------------------------|
| Board      | ESP32 (ESP32-D0WD / "ESP32-035")                      |
| Display    | TFT LCD 480×320, landscape (rotasi 1), kemungkinan ILI9488 |
| Touch      | XPT2046 (resistif) — SPI                               |
| Backlight  | Dikontrol via pin `TFT_BL`                             |
| Serial     | 115200 baud (untuk debug & input cadangan)            |

---

## Cara Build & Upload

### 1. Prasyarat
- [Arduino IDE](https://www.arduino.cc/en/software) **atau** [arduino-cli](https://arduino.github.io/arduino-cli/)
- Core ESP32 terpasang (`esp32:esp32`)
- Library **TFT_eSPI** (Bodmer)

### 2. PENTING — konfigurasi TFT_eSPI
`TFT_eSPI` harus dikonfigurasi sesuai wiring layar Anda. Edit
`User_Setup.h` (atau pilih lewat `User_Setup_Select.h`) di folder library
`TFT_eSPI`, sesuaikan:
- Driver layar (mis. `ILI9488`)
- Pin SPI: `TFT_MOSI`, `TFT_MISO`, `TFT_SCLK`, `TFT_CS`, `TFT_DC`, `TFT_RST`, `TFT_BL`
- Pin touch: `TOUCH_CS`

> Kalau layar blank / warna kacau / touch tidak respons, 99% masalahnya ada di
> konfigurasi `User_Setup.h` ini, bukan di kode sketch.

Kalibrasi touch ada di sketch (baris ~11):
```cpp
uint16_t calData[5] = {275, 3620, 264, 3532, 1};  // XPT2046
```
Catatan: di mode landscape, **sumbu X touch terbalik** — kode sudah
mengoreksinya dengan `dx = 480 - touchX` di tiap halaman.

### 3. Upload via arduino-cli
```bash
# dari root repo
arduino-cli compile --fqbn esp32:esp32:esp32 --upload -p COM7 esp32_tft_project
```
Ganti `COM7` dengan port board Anda (cek: `arduino-cli board list`).

### 4. Upload via Arduino IDE
Buka `esp32_tft_project/esp32_tft_project.ino` → pilih board **ESP32 Dev Module**
→ pilih Port → klik **Upload**.

---

## Struktur Proyek

```
opo_wae/
├── esp32_tft_project/
│   └── esp32_tft_project.ino   ← FIRMWARE UTAMA (yang di-flash ke ESP32)
├── yongalah/                   ← desain acuan halaman GRAFIK per sensor (9 PNG)
│   ├── monitor_o2.png  monitor_pm25.png  monitor_pm10.png
│   ├── monitor_hcho.png monitor_voc.png  monitor_humidity.png
│   └── monitor_do.png  monitor_suhu.png  monitor_turbidity.png
├── amboh/                      ← desain chart versi lama (co2/do/ph/suhu)
├── files/                      ← aset UI (splash, menu, dll)
├── test_serial/                ← sketch tes serial (bukan firmware utama)
├── .kimchi/                    ← arsip dokumen & backup kode versi lama
└── find_arduino.ps1            ← util cari lokasi arduino-cli.exe (Windows)
```

---

## Fitur UI (ringkas)

Program adalah **state machine** (`enum AppState`) di `loop()`:

| Halaman        | Fungsi                | Isi                                              |
|----------------|-----------------------|--------------------------------------------------|
| Splash         | `showSplashScreen()`  | Animasi logo ALCURA ~3,5 detik                   |
| Menu           | `showMainMenu()`      | Kartu Home, Setting, Fan, Light                  |
| Home           | `showHome()`          | Daftar sensor Udara (6) & Air (3), bisa di-tap   |
| Chart Detail   | `drawChartDetail()`   | Grafik tren 12 jam per sensor + Min/Avg/Max      |
| Settings       | `showSettings()`      | Toggle WiFi                                       |
| Light          | `showLight()`         | 5 lampu + slider kecerahan                        |
| Fan            | `showFan()`           | 2 kipas                                           |

**Tema warna halaman grafik:**
- Sensor **Udara** (O₂, PM2.5, PM10, HCHO, VOC, Humidity) → tema **hijau**
- Sensor **Air** (DO, Suhu Air, Turbidity) → tema **biru**

Tiap kartu sensor di Home membuka halaman grafiknya masing-masing (lihat
mapping di `showHome()` dan `case`-nya di `drawChartDetail()`).

**Input cadangan via Serial** (115200): `U/D/L/R` navigasi, `E` pilih, `B` kembali.

---

## Catatan untuk Pengembangan Lanjutan

- **Data sensor masih dummy.** Tiap sensor punya array `dXXX[12]` + `bigValue` /
  `statMin/Avg/Max` di dalam `drawChartDetail()`. Ganti nilai-nilai ini dengan
  data sensor asli.
- **Rencana ESP-NOW:** menghubungkan 2 ESP32 (sensor → display) secara lokal.
  Belum diimplementasi (menunggu hardware sensor).
- **Menambah halaman grafik baru:** tambahkan `case` di `drawChartDetail()`
  (set data, judul, label sumbu Y, ikon, dan `isWater` untuk tema biru), lalu
  petakan kartu Home-nya di `showHome()`.
- **Ikon** digambar manual (fungsi `draw*Icon`) dari primitif `fillCircle` /
  `fillTriangle` / `fillRect` — tidak ada file gambar bitmap.

---

## Lisensi
Belum ditentukan.
