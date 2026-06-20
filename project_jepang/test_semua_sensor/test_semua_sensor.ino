/*
 * PROJECT JEPANG - MONITOR SEMUA SENSOR (untuk tes satu per satu)
 * Board: ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 * Cara pakai:
 *  1. Upload sketch ini SEKALI.
 *  2. Buka Serial Monitor, baud 115200.
 *  3. Colok VCC + GND satu sensor (data sudah terhubung ke GPIO masing-masing).
 *  4. Lihat baris sensor itu -> kalau nilainya masuk akal/berubah = sensor OK.
 *  5. Pindah ke sensor berikutnya. Sensor yang belum dikasih daya nilainya ngaco/0 (abaikan).
 *
 * Catatan: sketch ini TANPA WiFi, supaya ADC2 (GPIO 25) tetap bisa dipakai.
 */

#include <Wire.h>
#include "DHT.h"
#include <Adafruit_AHTX0.h>
#include <Adafruit_MLX90614.h>
#include "ScioSense_ENS160.h"

// ================= PIN MAP =================
// Analog (ADC)
#define PIN_MQ2_1   32
#define PIN_MQ2_3   34
#define PIN_MG811   35
#define PIN_UV      36   // VP
#define PIN_PH      39   // VN
#define PIN_TDS     33   // DIPINDAH dari 25 (ADC2/WiFi) -> 33 (ADC1, slot MQ-2 #2 rusak)

// Digital
#define PIN_DHT      4
#define PIN_TRIG     5
#define PIN_ECHO    18

// TCS3200 warna
#define TCS_S0  16   // DIPINDAH dari GPIO 12 (strapping pin -> bikin boot loop!)
#define TCS_S1  13
#define TCS_S2  14
#define TCS_S3  15
#define TCS_OUT 27
#define TCS_LED 26

// I2C
#define PIN_SDA 21
#define PIN_SCL 22
#define ENS160_ADDR 0x53   // kalau tak terdeteksi, coba ganti 0x52

// ================= OBJEK =================
DHT dht(PIN_DHT, DHT11);
Adafruit_AHTX0 aht;
Adafruit_MLX90614 mlx;
ScioSense_ENS160 ens160(ENS160_ADDR);

bool aht_ok = false, mlx_ok = false, ens_ok = false;

// ================= HELPER =================
bool i2cAda(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

long bacaJarakCM() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long dur = pulseIn(PIN_ECHO, HIGH, 30000); // timeout 30ms (~5m)
  if (dur == 0) return -1;
  return dur / 58;
}

int bacaTCS(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2);
  digitalWrite(TCS_S3, s3);
  delay(40);
  return pulseIn(TCS_OUT, LOW, 50000);
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n\n===== MONITOR SEMUA SENSOR - PROJECT JEPANG =====");

  // ADC: input-only pins tidak perlu pinMode, tapi set resolusi
  analogReadResolution(12);

  // DHT
  dht.begin();

  // HC-SR04
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // TCS3200
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT); pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW); // skala 20%
  digitalWrite(TCS_LED, HIGH);                            // LED sensor ON

  // I2C
  Wire.begin(PIN_SDA, PIN_SCL);

  Serial.println("Siap. Colok VCC/GND sensor satu per satu...\n");
}

void loop() {
  Serial.println("======================================================");

  // ---------- ANALOG (raw 0-4095, mV) ----------
  Serial.println("[ANALOG]  (raw / mV)");
  Serial.printf("  MQ-2 #1 (G32): %4d / %4d mV\n", analogRead(PIN_MQ2_1), analogReadMilliVolts(PIN_MQ2_1));
  Serial.printf("  MQ-2 #3 (G34): %4d / %4d mV\n", analogRead(PIN_MQ2_3), analogReadMilliVolts(PIN_MQ2_3));
  Serial.printf("  MG811   (G35): %4d / %4d mV\n", analogRead(PIN_MG811), analogReadMilliVolts(PIN_MG811));
  Serial.printf("  UV      (G36): %4d / %4d mV\n", analogRead(PIN_UV),    analogReadMilliVolts(PIN_UV));
  Serial.printf("  pH      (G39): %4d / %4d mV\n", analogRead(PIN_PH),    analogReadMilliVolts(PIN_PH));
  Serial.printf("  TDS     (G33): %4d / %4d mV\n", analogRead(PIN_TDS),   analogReadMilliVolts(PIN_TDS));

  // ---------- DHT11 ----------
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  Serial.print("[DHT11 G4] ");
  if (isnan(t) || isnan(h)) Serial.println("belum terbaca");
  else Serial.printf("Suhu: %.1f C  Lembap: %.1f %%\n", t, h);

  // ---------- HC-SR04 ----------
  long jarak = bacaJarakCM();
  Serial.print("[HC-SR04] ");
  if (jarak < 0) Serial.println("tidak ada echo");
  else Serial.printf("Jarak: %ld cm\n", jarak);

  // ---------- TCS3200 ----------
  int R = bacaTCS(LOW, LOW);
  int G = bacaTCS(HIGH, HIGH);
  int B = bacaTCS(LOW, HIGH);
  Serial.printf("[TCS3200] R:%d  G:%d  B:%d  (periode us, kecil=kuat)\n", R, G, B);

  // ---------- I2C: AHT21 ----------
  if (!aht_ok && i2cAda(0x38)) aht_ok = aht.begin();
  Serial.print("[AHT21] ");
  if (aht_ok) {
    sensors_event_t hum, tmp;
    aht.getEvent(&hum, &tmp);
    Serial.printf("Suhu: %.1f C  Lembap: %.1f %%\n", tmp.temperature, hum.relative_humidity);
  } else Serial.println("belum terdeteksi");

  // ---------- I2C: MLX90614 ----------
  if (!mlx_ok && i2cAda(0x5A)) mlx_ok = mlx.begin();
  Serial.print("[MLX90614] ");
  if (mlx_ok) Serial.printf("Ambient: %.1f C  Objek: %.1f C\n", mlx.readAmbientTempC(), mlx.readObjectTempC());
  else Serial.println("belum terdeteksi");

  // ---------- I2C: ENS160 ----------
  if (!ens_ok && i2cAda(ENS160_ADDR)) {
    if (ens160.begin()) { ens160.setMode(ENS160_OPMODE_STD); ens_ok = true; }
  }
  Serial.print("[ENS160] ");
  if (ens_ok && ens160.available()) {
    ens160.measure(true);
    Serial.printf("AQI: %d  eCO2: %d ppm  TVOC: %d ppb\n", ens160.getAQI(), ens160.geteCO2(), ens160.getTVOC());
  } else Serial.println("belum terdeteksi / belum siap");

  Serial.println();
  delay(1500);
}
