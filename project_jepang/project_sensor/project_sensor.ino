/*
 * PROJECT JEPANG - Pembacaan Semua Sensor
 * ESP32 + WS2812B Ring (12 LED) + TCS3200/GY-31 + MQ-2 + DHT11
 *
 * Membaca: warna (GY-31), gas (MQ-2 digital), suhu & kelembapan (DHT11)
 * Hasil ditampilkan di Serial Monitor (115200 baud).
 * LED ring ikut berubah warna sesuai warna yang dibaca GY-31 (demo).
 */

#include <Adafruit_NeoPixel.h>
#include "DHT.h"

// ===== Pin WS2812B =====
#define LED_PIN     4
#define NUMPIXELS   12
#define BRIGHTNESS  50

// ===== Pin TCS3200 / GY-31 =====
#define TCS_S0   25
#define TCS_S1   26
#define TCS_S2   27
#define TCS_S3   14
#define TCS_OUT  13

// ===== Pin MQ-2 (digital) =====
#define MQ_DO    32

// ===== Pin DHT11 =====
#define DHT_PIN   18
#define DHT_TYPE  DHT11

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== PROJECT JEPANG - Mulai membaca sensor ===");

  // WS2812B
  pixels.begin();
  pixels.setBrightness(BRIGHTNESS);
  pixels.clear();
  pixels.show();

  // TCS3200: S0/S1 atur skala frekuensi. HIGH/LOW = 20% (cukup & stabil)
  pinMode(TCS_S0, OUTPUT);
  pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT);
  pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S0, HIGH);
  digitalWrite(TCS_S1, LOW);

  // MQ-2 digital
  pinMode(MQ_DO, INPUT);

  // DHT11
  dht.begin();
}

// Baca satu kanal warna lewat pulseIn.
// Nilai = periode pulsa (mikrodetik). MAKIN KECIL = warna itu MAKIN KUAT.
int bacaWarna(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2);
  digitalWrite(TCS_S3, s3);
  delay(50);                          // beri waktu filter stabil
  return pulseIn(TCS_OUT, LOW, 50000); // timeout 50ms
}

void setRing(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, pixels.Color(r, g, b));
  pixels.show();
}

void loop() {
  // ---- TCS3200 / GY-31 ----
  int merah = bacaWarna(LOW, LOW);   // filter Merah
  int hijau = bacaWarna(HIGH, HIGH); // filter Hijau
  int biru  = bacaWarna(LOW, HIGH);  // filter Biru

  // ---- MQ-2 ----
  // Banyak modul: DO = LOW saat gas terdeteksi (di atas ambang potensiometer)
  bool adaGas = (digitalRead(MQ_DO) == LOW);

  // ---- DHT11 ----
  float suhu = dht.readTemperature();
  float lembap = dht.readHumidity();

  // ---- Tampilkan ----
  Serial.println("--------------------------------");
  Serial.printf("Warna (periode us, kecil=kuat) -> R:%d  G:%d  B:%d\n", merah, hijau, biru);
  Serial.printf("Gas MQ-2: %s\n", adaGas ? "TERDETEKSI!" : "aman");
  if (isnan(suhu) || isnan(lembap)) {
    Serial.println("DHT11: gagal baca (cek wiring data)");
  } else {
    Serial.printf("Suhu: %.1f C   Kelembapan: %.1f %%\n", suhu, lembap);
  }

  // ---- Demo: warnai ring sesuai warna dominan ----
  // periode kecil = warna kuat, jadi cari yang paling kecil
  if (merah > 0 && merah <= hijau && merah <= biru)      setRing(255, 0, 0);
  else if (hijau > 0 && hijau <= merah && hijau <= biru) setRing(0, 255, 0);
  else if (biru > 0)                                     setRing(0, 0, 255);

  delay(1000);
}
