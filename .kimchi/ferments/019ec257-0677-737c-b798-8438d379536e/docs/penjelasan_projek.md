# ACURA Display System — Penjelasan Proyek

## Nama & Tujuan Proyek
Proyek ini bernama **ACURA Display System v1.0**, yaitu antarmuka digital untuk LCD TFT berbasis ESP32 yang dirancang untuk menampilkan menu navigasi interaktif gaya smartphone.

## Hardware & Stack
- **Board:** ESP32-035
- **Display:** TFT LCD 320×480 pixel (landscape)
- **Library:** `TFT_eSPI` (line 1, library utama untuk rendering)
- **Backlight:** Dikontrol via pin GPIO `TFT_BL`

## Fitur UI
Aplikasi memiliki 6 layar (state) yang dikelola dalam mesin state:
1. **Splash Screen** (`showSplashScreen`, line 86) — logo ACURA dengan progress bar selama 3 detik.
2. **Main Menu** (`showMainMenu`, line 123) — tampilan kartu 2×2 berwarna berisi Home, Setting, Info, dan About.
3. **Home** (`showHome`, line 303) — halaman utama sambutan.
4. **Settings** (`showSettings`, line 331) — pengaturan brightness, rotasi, timer.
5. **Info** (`showInfo`, line 359) — informasi perangkat dan uptime.
6. **About** (`showAbout`, line 387) — tentang sistem ACURA.

## Arsitektur & State Machine
Program mengimplementasikan **state machine sederhana** dengan enum `AppState` (line 6):
`SPLASH_SCREEN → MENU → {HOME | SETTINGS | INFO | ABOUT}`.
Pada `loop()` (line 52), program melakukan switch-case terhadap `currentState`, dan hanya clear screen jika state berubah — ini mencegah flickering.

## Kontrol & Input
Navigasi dilakukan melalui **Serial Command** (`Serial.available`):
- `U/D/L/R`: Navigasi menu (atas/bawah/kiri/kanan)
- `E`: Enter / Pilih menu
- `B`: Kembali ke Menu

## Catatan Teknis
- Optimasi redraw dilakukan dengan flag `screenDrawn` dan `previousMenuSelection` (line 19, 20) agar tidak menggambar ulang tanpa perubahan.
- Kartu menu menggunakan efek rounded corners (`drawLVGLCard`, line 258) yang digambar manual via `fillRect` + `fillCircle`.
- Ikon (home, gear, info) digambar manual secara prosedural via fungsi `draw*Icon`.
- Semua teks dan warna dapat dikustomisasi dengan mudah melalui define di awal file (line 11–17).
