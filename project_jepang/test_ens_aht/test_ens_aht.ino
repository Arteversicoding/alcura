/*
 * Tes khusus ENS160 + AHT21 (modul gabungan) - I2C
 * SDA=21, SCL=22, VCC=3.3V, I2C 50kHz (stabil)
 *
 * AHT21  alamat: 0x38
 * ENS160 alamat: 0x53 (atau 0x52)
 */
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "ScioSense_ENS160.h"

Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(0x53);   // ganti 0x52 kalau scan nunjukin 0x52

bool aht_ok = false, ens_ok = false;

void scanI2C() {
  Serial.println("--- Scan I2C ---");
  byte n = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("  ada device: 0x%02X\n", a); n++; }
  }
  if (n == 0) Serial.println("  (kosong - tidak ada device)");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Tes ENS160 + AHT21 ===");
  Wire.begin(21, 22);
  Wire.setClock(50000);

  scanI2C();

  aht_ok = aht.begin();
  Serial.print("AHT21 begin: "); Serial.println(aht_ok ? "OK" : "GAGAL");

  ens_ok = ens160.begin();
  Serial.print("ENS160 begin: "); Serial.println(ens_ok ? "OK" : "GAGAL");
  if (ens_ok) ens160.setMode(ENS160_OPMODE_STD);
}

void loop() {
  // AHT21
  if (aht_ok) {
    sensors_event_t h, t;
    aht.getEvent(&h, &t);
    Serial.printf("[AHT21] Suhu: %.2f C  Lembap: %.2f %%\n", t.temperature, h.relative_humidity);
  } else {
    Serial.println("[AHT21] tidak aktif");
  }

  // ENS160
  if (ens_ok && ens160.available()) {
    ens160.measure(true);
    Serial.printf("[ENS160] AQI: %d  eCO2: %d ppm  TVOC: %d ppb\n",
                  ens160.getAQI(), ens160.geteCO2(), ens160.getTVOC());
  } else {
    Serial.println("[ENS160] tidak aktif / belum siap");
  }

  Serial.println("---");
  delay(1000);
}
