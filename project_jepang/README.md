# Project Jepang — 2 Board ESP32

Sistem dibagi menjadi **2 board ESP32 terpisah**, keduanya terhubung ke layar
**ALCURA TFT** lewat **ESP-NOW** (broadcast, channel 1). Konsepnya "tinggal colok":
beri daya tiap board, lalu semua langsung bekerja dan bisa dikontrol dari ALCURA.
(Dulu relay & lampu jadi 2 board terpisah; sekarang **digabung** jadi `board_kontrol`.)

```
                        ESP-NOW broadcast (channel 1)
   ┌──────────────┐       SensorData (msgType 0)        ┌──────────────┐
   │ board_sensor │ ───────────────────────────────────▶│   ALCURA     │
   └──────────────┘                                      │     TFT      │
   ┌──────────────┐       ControlData (msgType 1)        │ (receiver +  │
   │ board_kontrol│ ◀───────────────────────────────────│  controller) │
   │ relay + lampu│                                      └──────────────┘
   └──────────────┘
```

## Isi folder

| Folder              | Board | Fungsi |
|---------------------|-------|--------|
| `board_sensor/`     | 1     | Baca 13 sensor → kirim `SensorData` ke ALCURA + push Firebase `/sensor.json`. |
| `board_kontrol/`    | 2     | Terima `ControlData` → 2 kipas + 2 pompa udara (**relay 4-channel**) **dan** 5 lampu **WS2812B** (Ring + 4 Strip, on/off + brightness global). |
| `test_semua_sensor/`| —     | Alat diagnostik: pantau semua sensor di Serial Monitor (tanpa WiFi). |
| `i2c_scan/`         | —     | Alat diagnostik: scan alamat device I2C. |

## Struct ESP-NOW (harus identik di semua board + ALCURA)

```c
struct SensorData {            // msgType 0  (board_sensor → ALCURA)
  uint8_t msgType;
  float o2, pm25, pm10, hcho, voc, humidity, doValue, suhuAir, turbidity, aqi;
};

struct ControlData {           // msgType 1  (ALCURA → board_kontrol)
  uint8_t msgType;
  bool    lampState[5];        // lampu: 0=Ring, 1..4=Strip 1..4
  uint8_t brightness;          // lampu (0–100, kiri=redup → kanan=terang)
  bool    fanState[2];         // relay: 2 kipas
  bool    pumpState[2];        // relay: 2 pompa udara
};
```

> Kalau salah satu field di struct diubah, **wajib** ubah di semua board dan di
> `esp32_tft_project.ino` (ALCURA) agar `sizeof()` tetap cocok.

## Catatan pemasangan

- **board_kontrol** default `RELAY_ACTIVE_LOW = true` (mayoritas modul relay). Kalau
  perilaku ON/OFF terbalik, ubah jadi `false`.
- **board_kontrol** juga menggerakkan 5 lampu addressable terpisah: Ring WS2812B
  (GPIO 4, 12 LED, 5V) + 4× strip (GPIO 18/19/23/25, 30 LED, 12V). Warna default
  putih. Sesuaikan `lampCounts[]` bila jumlah LED tiap fixture beda. Wiring di `WIRING.md`.
- Pompa udara dikontrol dari ALCURA lewat halaman **Pompa** (menu → kartu "Pompa"),
  gayanya sama dengan halaman Fan. Default `pumpState = {true, true}` (nyala saat start).
- Callback ESP-NOW memakai guard `ESP_ARDUINO_VERSION` agar kompilasi di core ESP32
  v2.x maupun v3.x (signature recv/send cb berubah di v3.x).

## Library Arduino

`Adafruit_NeoPixel`, `DHT sensor library`, `Adafruit_AHTX0`,
`Adafruit_MLX90614`, `ScioSense_ENS160` (ESP-NOW & WiFi sudah bawaan core ESP32).
