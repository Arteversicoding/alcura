/*
 * PROJECT JEPANG - Tes LED Strip Krisbow (addressable 12V) + WS2812B Ring
 * ESP32
 *
 * WS2812B Ring (12 LED)        : DI -> GPIO 4
 * Krisbow strip addressable    : DI -> GPIO 5  (12V & GND ke adaptor, GND nyambung ESP32)
 *
 * Keduanya dikontrol seperti NeoPixel.
 * Catatan: kalau strip nyala tapi WARNANYA salah (mis. minta merah jadi hijau),
 *          ganti NEO_GRB di STRIP jadi NEO_RGB. Kalau kedip2/ngaco, ganti KHZ800 -> KHZ400.
 */

#include <Adafruit_NeoPixel.h>

// ===== WS2812B Ring =====
#define RING_PIN    4
#define RING_COUNT  12

// ===== Krisbow strip (addressable) =====
#define STRIP_PIN    5
#define STRIP_COUNT  30   // ~30 LED. Ubah sesuai jumlah aslinya kalau perlu

#define BRIGHTNESS  50

Adafruit_NeoPixel ring(RING_COUNT, RING_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip(STRIP_COUNT, STRIP_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Tes Strip Krisbow + Ring WS2812B ===");

  ring.begin();
  ring.setBrightness(BRIGHTNESS);
  ring.clear();
  ring.show();

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();
}

// Isi seluruh strip dengan satu warna
void isiStrip(Adafruit_NeoPixel &px, uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < px.numPixels(); i++) px.setPixelColor(i, px.Color(r, g, b));
  px.show();
}

void loop() {
  // 1) Color wipe: titik berjalan sepanjang strip (jelas kalau terhubung)
  strip.clear();
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 255)); // biru berjalan
    strip.show();
    delay(40);
  }
  Serial.println("Strip: color wipe selesai");

  // 2) Semua warna bergantian di strip + ring ikut
  isiStrip(strip, 255, 0, 0);  isiStrip(ring, 255, 0, 0);  // Merah
  Serial.println("Merah");  delay(700);
  isiStrip(strip, 0, 255, 0);  isiStrip(ring, 0, 255, 0);  // Hijau
  Serial.println("Hijau");  delay(700);
  isiStrip(strip, 0, 0, 255);  isiStrip(ring, 0, 0, 255);  // Biru
  Serial.println("Biru");   delay(700);
  isiStrip(strip, 255, 255, 255); isiStrip(ring, 255, 255, 255); // Putih
  Serial.println("Putih");  delay(700);
}
