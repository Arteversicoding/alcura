/*
 * Tes khusus MLX90614 (GY-906) - I2C
 * SDA=21, SCL=22, VCC=3.3V
 * I2C diturunkan ke 50kHz supaya pembacaan SMBus lebih stabil (anti-nan).
 */
#include <Wire.h>
#include <Adafruit_MLX90614.h>

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
bool ok = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Tes MLX90614 ===");

  Wire.begin(21, 22);
  Wire.setClock(50000);   // 50kHz, lebih lambat = lebih stabil utk MLX

  // Scan cepat alamat 0x5A
  Wire.beginTransmission(0x5A);
  if (Wire.endTransmission() == 0) Serial.println("Device 0x5A: TERDETEKSI (ACK)");
  else Serial.println("Device 0x5A: TIDAK ACK");

  ok = mlx.begin();   // default address 0x5A
  Serial.print("mlx.begin(): ");
  Serial.println(ok ? "OK" : "GAGAL");
}

void loop() {
  double amb = mlx.readAmbientTempC();
  double obj = mlx.readObjectTempC();

  if (isnan(amb) || isnan(obj)) {
    Serial.println("Baca GAGAL (nan) - coba reseat SDA/SCL atau tambah pull-up 4.7k");
  } else {
    Serial.printf("Ambient: %.2f C   Objek: %.2f C\n", amb, obj);
  }
  delay(800);
}
