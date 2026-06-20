#include <Adafruit_NeoPixel.h>

// ===== Konfigurasi =====
#define LED_PIN    4    // DI lampu WS2812B -> GPIO 4 ESP32
#define NUMPIXELS  12   // Jumlah LED pada strip/ring WS2812B
#define BRIGHTNESS 50   // Kecerahan 0-255 (jangan max dulu, hemat arus)

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  pixels.begin();
  pixels.setBrightness(BRIGHTNESS);
  pixels.clear();
  pixels.show();
  Serial.println("WS2812B siap! Mulai ganti warna...");
}

void setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void loop() {
  setAll(255, 0, 0);   // Merah
  Serial.println("Merah");
  delay(800);

  setAll(0, 255, 0);   // Hijau
  Serial.println("Hijau");
  delay(800);

  setAll(0, 0, 255);   // Biru
  Serial.println("Biru");
  delay(800);

  setAll(255, 255, 255); // Putih
  Serial.println("Putih");
  delay(800);
}
