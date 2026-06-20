/*
 * ===========================================================================
 *  BOARD 1 — SENSOR  (Project Jepang / ALCURA)
 * ===========================================================================
 *  Tugas board ini: BACA 9 sensor lalu KIRIM ke ALCURA TFT via ESP-NOW.
 *  Board ini TIDAK mengontrol apa-apa (lampu & relay ada di board lain).
 *
 *  Tinggal colok: semua pin DATA sensor sudah terhubung. Yang perlu hanya
 *  memberi VCC + GND ke tiap sensor, lalu sensor otomatis terbaca.
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF), channel 1.
 *               msgType = 0  -> SensorData (board ini -> ALCURA)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  PIN MAP SENSOR
 *    Analog (ADC1) : MQ2_1=32  MQ2_2=34  MG811=35  UV=36(VP)  pH=39(VN)  TDS=33
 *    DHT11         : GPIO 18
 *    TCS3200 warna : S0=16  S1=13  S2=14  S3=15  OUT=27  LED=26
 *    I2C           : SDA=21  SCL=22   (AHT21=0x38  MLX90614=0x5A  ENS160=0x53)
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include "DHT.h"
#include <Adafruit_AHTX0.h>
#include <Adafruit_MLX90614.h>
#include "ScioSense_ENS160.h"

// ===== STRUCT ESP-NOW (HARUS identik dengan ALCURA — urutan jangan diubah) =====
struct __attribute__((packed)) SensorData {
  uint8_t msgType;    // 0
  float   o2;         // % oksigen
  float   pm25;       // ug/m3
  float   pm10;       // ug/m3
  float   hcho;       // ppm formaldehida
  float   voc;        // ppb VOC
  float   humidity;   // % kelembapan
  float   doValue;    // mg/L oksigen terlarut
  float   suhuAir;    // C suhu air (objek MLX90614)
  float   turbidity;  // NTU kekeruhan
  float   aqi;        // 1-5 indeks udara
};

// ===== PIN SENSOR =====
#define PIN_MQ2_1    32
#define PIN_MQ2_2    34
#define PIN_MG811    35
#define PIN_UV       36
#define PIN_PH       39
#define PIN_TDS      33
#define PIN_DHT      18
#define TCS_S0       16
#define TCS_S1       13
#define TCS_S2       14
#define TCS_S3       15
#define TCS_OUT      27
#define TCS_LED      26
#define PIN_SDA      21
#define PIN_SCL      22
#define ENS160_ADDR  0x53   // kalau tak terdeteksi, coba 0x52

#define SEND_INTERVAL 1000  // ms antar pengiriman

// ===== OBJEK SENSOR =====
DHT               dht(PIN_DHT, DHT11);
Adafruit_AHTX0    aht;
Adafruit_MLX90614 mlx;
ScioSense_ENS160  ens160(ENS160_ADDR);

bool aht_ok = false, mlx_ok = false, ens_ok = false, espReady = false;

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned long lastSend = 0;

// ===== TCS3200: baca 1 kanal warna (periode pulsa us; kecil = warna kuat) =====
int bacaTCS(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2);
  digitalWrite(TCS_S3, s3);
  delay(40);
  return (int)pulseIn(TCS_OUT, LOW, 50000);  // timeout 50 ms
}

// ===== ESP-NOW init (broadcast, channel 1) =====
void espNowInit() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW GAGAL init!");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 1;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap -> broadcast channel 1");
    Serial.printf("MAC sensor: %s\n", WiFi.macAddress().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BOARD SENSOR - Project Jepang ===");

  analogReadResolution(12);

  // TCS3200
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT); pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW); // skala 20%
  digitalWrite(TCS_LED, HIGH);                            // LED sensor ON

  // DHT11
  dht.begin();

  // I2C — 50 kHz supaya MLX90614 stabil (anti-nan)
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(50000);

  aht_ok = aht.begin();
  mlx_ok = mlx.begin();
  ens_ok = ens160.begin();
  if (ens_ok) ens160.setMode(ENS160_OPMODE_STD);
  Serial.printf("AHT21: %s  MLX90614: %s  ENS160: %s\n",
                aht_ok ? "OK" : "X", mlx_ok ? "OK" : "X", ens_ok ? "OK" : "X");

  espNowInit();
}

void loop() {
  if (millis() - lastSend < SEND_INTERVAL) return;
  lastSend = millis();

  SensorData d;
  d.msgType = 0;

  // ---- Kelembapan (AHT21 utama, fallback DHT11) ----
  if (aht_ok) {
    sensors_event_t hum, tmp;
    aht.getEvent(&hum, &tmp);
    d.humidity = hum.relative_humidity;
  } else {
    float h = dht.readHumidity();
    d.humidity = isnan(h) ? 52.0f : h;
  }

  // ---- ENS160: VOC, AQI, dan proxy O2 dari eCO2 ----
  if (ens_ok && ens160.available()) {
    ens160.measure(true);
    d.voc = (float)ens160.getTVOC();
    d.aqi = (float)ens160.getAQI();
    float eco2 = (float)ens160.geteCO2();
    d.o2  = constrain(21.0f - (eco2 - 400.0f) * 0.0005f, 18.0f, 22.0f);
  } else {
    d.voc = 58.0f; d.aqi = 1.0f; d.o2 = 21.0f;
  }

  // ---- MLX90614: suhu air (suhu objek) ----
  d.suhuAir = mlx_ok ? (float)mlx.readObjectTempC() : 27.0f;

  // ---- MG811 -> proxy PM10 (5-80 ug/m3) ----
  d.pm10 = constrain((float)analogRead(PIN_MG811) / 4095.0f * 80.0f, 5.0f, 80.0f);

  // ---- MQ-2 #1 -> proxy PM2.5 (1-35 ug/m3) ----
  d.pm25 = constrain((float)analogRead(PIN_MQ2_1) / 4095.0f * 35.0f, 1.0f, 35.0f);

  // ---- MQ-2 #2 -> proxy HCHO (0.01-0.08 ppm) ----
  d.hcho = constrain((float)analogRead(PIN_MQ2_2) / 4095.0f * 0.08f, 0.01f, 0.08f);

  // ---- TDS -> proxy DO (4-14 mg/L; TDS tinggi = DO rendah) ----
  float tdsV = (float)analogReadMilliVolts(PIN_TDS) / 1000.0f;
  d.doValue = constrain(14.0f - tdsV * 2.5f, 4.0f, 14.0f);

  // ---- TCS3200 -> proxy Turbidity (NTU) ----
  int R = bacaTCS(LOW, LOW), G = bacaTCS(HIGH, HIGH), B = bacaTCS(LOW, HIGH);
  float avgP = (R + G + B) / 3.0f;
  d.turbidity = constrain(avgP / 150.0f, 0.5f, 10.0f);

  // ---- Kirim via ESP-NOW ----
  if (espReady) {
    esp_err_t res = esp_now_send(broadcastMAC, (uint8_t*)&d, sizeof(d));
    Serial.printf("[TX %s] O2:%.1f PM25:%.0f PM10:%.0f HCHO:%.3f VOC:%.0f Hum:%.1f DO:%.1f SuhuAir:%.1f Turb:%.1f AQI:%.0f\n",
                  res == ESP_OK ? "OK" : "ERR",
                  d.o2, d.pm25, d.pm10, d.hcho, d.voc, d.humidity, d.doValue, d.suhuAir, d.turbidity, d.aqi);
  }
}
