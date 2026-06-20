# Project Jepang — 3 Board ESP32

Sistem dibagi menjadi **3 board ESP32 terpisah**, semuanya terhubung ke layar
**ALCURA TFT** lewat **ESP-NOW** (broadcast, channel 1). Konsepnya "tinggal colok":
beri daya tiap board, lalu semua langsung bekerja dan bisa dikontrol dari ALCURA.

```
                       ESP-NOW broadcast (channel 1)
   ┌─────────────┐        SensorData (msgType 0)        ┌──────────────┐
   │ board_sensor│ ───────────────────────────────────▶│              │
   └─────────────┘                                      │   ALCURA     │
   ┌─────────────┐        ControlData (msgType 1)       │     TFT      │
   │ board_relay │ ◀───────────────────────────────────│ (receiver +  │
   └─────────────┘                                      │  controller) │
   ┌─────────────┐        ControlData (msgType 1)       │              │
   │ board_lampu │ ◀───────────────────────────────────└──────────────┘
   └─────────────┘
```

## Isi folder

| Folder              | Board | Fungsi |
|---------------------|-------|--------|
| `board_sensor/`     | 1     | Baca 9 sensor → kirim `SensorData` ke ALCURA. |
| `board_relay/`      | 2     | Terima `ControlData` → 2 kipas + 2 pompa udara via **relay 4-channel**. |
| `board_lampu/`      | 3     | Terima `ControlData` → lampu **WS2812B** (5 grup on/off + brightness). |
| `test_semua_sensor/`| —     | Alat diagnostik: pantau semua sensor di Serial Monitor (tanpa WiFi). |
| `i2c_scan/`         | —     | Alat diagnostik: scan alamat device I2C. |

## Struct ESP-NOW (harus identik di semua board + ALCURA)

```c
struct SensorData {            // msgType 0  (board_sensor → ALCURA)
  uint8_t msgType;
  float o2, pm25, pm10, hcho, voc, humidity, doValue, suhuAir, turbidity, aqi;
};

struct ControlData {           // msgType 1  (ALCURA → board_relay & board_lampu)
  uint8_t msgType;
  bool    lampState[5];        // board_lampu
  uint8_t brightness;          // board_lampu (0–100)
  bool    fanState[2];         // board_relay (2 kipas)
  bool    pumpState[2];        // board_relay (2 pompa udara)
};
```

> Kalau salah satu field di struct diubah, **wajib** ubah di semua board dan di
> `esp32_tft_project.ino` (ALCURA) agar `sizeof()` tetap cocok.

## Catatan pemasangan

- **board_relay** default `RELAY_ACTIVE_LOW = true` (mayoritas modul relay). Kalau
  perilaku ON/OFF terbalik, ubah jadi `false`.
- **board_lampu** menggerakkan 2× ring WS2812B (GPIO 4, 5V) + 4× strip 12V (GPIO 5),
  total 144 LED dibagi 5 grup. Warna default putih hangat. Sesuaikan `RING_NUM` /
  `STRIP_NUM` / `*_COUNT_EACH` bila jumlah berbeda. Detail wiring di `WIRING.md`.
- Pompa udara dikontrol dari ALCURA lewat halaman **Pompa** (menu → kartu "Pompa"),
  gayanya sama dengan halaman Fan. Default `pumpState = {true, true}` (nyala saat start).
- Callback ESP-NOW memakai guard `ESP_ARDUINO_VERSION` agar kompilasi di core ESP32
  v2.x maupun v3.x (signature recv/send cb berubah di v3.x).

## Library Arduino

`Adafruit_NeoPixel`, `DHT sensor library`, `Adafruit_AHTX0`,
`Adafruit_MLX90614`, `ScioSense_ENS160` (ESP-NOW & WiFi sudah bawaan core ESP32).
