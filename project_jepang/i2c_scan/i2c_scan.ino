/*
 * I2C Scanner - cek device I2C di bus (SDA=21, SCL=22)
 */
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(400);
  Wire.begin(21, 22);
  Serial.println("\n=== I2C Scanner (SDA=21, SCL=22) ===");
}

void loop() {
  byte count = 0;
  Serial.println("Memindai...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Ditemukan device di alamat 0x%02X\n", addr);
      count++;
    }
  }
  if (count == 0) Serial.println("  TIDAK ADA device I2C terdeteksi!");
  else Serial.printf("Total: %d device\n", count);
  Serial.println();
  delay(2000);
}
