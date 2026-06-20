/*
 * ESPNOW_SENDER — Project Jepang
 * Kirim data 9 sensor ke ALCURA TFT via ESP-NOW (broadcast)
 * Terima perintah kontrol lampu 1-5, brightness, kipas 1-2 dari ALCURA
 *
 * Keduanya pakai broadcast MAC → tidak perlu konfigurasi MAC sama sekali.
 * msgType = 0 → SensorData (sender → ALCURA)
 * msgType = 1 → ControlData (ALCURA → sender)
 *
 * Pin map:
 *   WS2812B ring  : GPIO 4  (12 LED, dibagi 5 grup lamp)
 *   DHT11         : GPIO 18
 *   TCS3200       : S0=16, S1=13, S2=14, S3=15, OUT=27, LED=26
 *   I2C           : SDA=21, SCL=22
 *   ADC           : MQ2_1=32, MQ2_2=34, MG811=35, UV=36, pH=39, TDS=33
 *   Kipas relay   : GPIO 19 (kipas 1), GPIO 23 (kipas 2)
 */

#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include "DHT.h"
#include <Adafruit_AHTX0.h>
#include <Adafruit_MLX90614.h>
#include "ScioSense_ENS160.h"
#include <Adafruit_NeoPixel.h>

// ===== STRUCT (identik dengan ALCURA — jangan ubah urutan field) =====
struct __attribute__((packed)) SensorData {
  uint8_t msgType;    // 0
  float   o2;         // % oksigen
  float   pm25;       // ug/m3
  float   pm10;       // ug/m3
  float   hcho;       // ppm formaldehida
  float   voc;        // ppb VOC
  float   humidity;   // % kelembapan
  float   doValue;    // mg/L oksigen terlarut
  float   suhuAir;    // °C objek MLX90614
  float   turbidity;  // NTU kekeruhan
  float   aqi;        // 1-5 indeks udara
};

struct __attribute__((packed)) ControlData {
  uint8_t msgType;        // 1
  bool    lampState[5];
  uint8_t brightness;     // 0–100
  bool    fanState[2];
};

// ===== Pin =====
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
#define ENS160_ADDR  0x53
#define PIN_FAN1     19
#define PIN_FAN2     23
#define LED_PIN       4
#define LED_COUNT    12

// ===== Objek sensor =====
DHT              dht(PIN_DHT, DHT11);
Adafruit_AHTX0   aht;
Adafruit_MLX90614 mlx;
ScioSense_ENS160 ens160(ENS160_ADDR);
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

bool aht_ok = false, mlx_ok = false, ens_ok = false, espReady = false;

// State kontrol (diupdate saat terima ControlData dari ALCURA)
volatile bool    lampState[5]  = {false};
volatile uint8_t brightness    = 50;
volatile bool    fanState[2]   = {false};
volatile bool    ctrlPending   = false;
ControlData      ctrlBuf;

// Grup LED per lampu: [start..end] inklusif
const int lampStart[5] = {0, 2, 4, 6,  9};
const int lampEnd[5]   = {1, 3, 5, 8, 11};

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ===== Terapkan state hardware =====
void applyHardware() {
  ring.setBrightness((uint8_t)((brightness * 255UL) / 100));
  for (int i = 0; i < 5; i++) {
    uint32_t col = lampState[i] ? ring.Color(255, 230, 180) : 0;
    for (int j = lampStart[i]; j <= lampEnd[i]; j++) ring.setPixelColor(j, col);
  }
  ring.show();
  digitalWrite(PIN_FAN1, fanState[0] ? HIGH : LOW);
  digitalWrite(PIN_FAN2, fanState[1] ? HIGH : LOW);
}

// ===== ESP-NOW callbacks =====
void onSent(const uint8_t*, esp_now_send_status_t) {}

void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len < 1 || data[0] != 1 || len != (int)sizeof(ControlData)) return;
  memcpy(&ctrlBuf, data, sizeof(ControlData));
  ctrlPending = true;
}

// ===== TCS3200 =====
int bacaTCS(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2); digitalWrite(TCS_S3, s3);
  delay(40);
  return (int)pulseIn(TCS_OUT, LOW, 50000);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESPNOW SENDER - Project Jepang ===");

  analogReadResolution(12);

  // Fan relay
  pinMode(PIN_FAN1, OUTPUT); digitalWrite(PIN_FAN1, LOW);
  pinMode(PIN_FAN2, OUTPUT); digitalWrite(PIN_FAN2, LOW);

  // TCS3200
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT); pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_S0, HIGH); digitalWrite(TCS_S1, LOW); // skala 20%
  digitalWrite(TCS_LED, HIGH);

  // WS2812B
  ring.begin();
  ring.setBrightness(50);
  ring.clear(); ring.show();

  // DHT11
  dht.begin();

  // I2C — 50kHz agar MLX90614 stabil
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(50000);

  aht_ok = aht.begin();
  mlx_ok = mlx.begin();
  ens_ok = ens160.begin();
  if (ens_ok) ens160.setMode(ENS160_OPMODE_STD);
  Serial.printf("AHT21: %s  MLX90614: %s  ENS160: %s\n",
    aht_ok ? "OK" : "X", mlx_ok ? "OK" : "X", ens_ok ? "OK" : "X");

  // WiFi STA mode (wajib untuk ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW GAGAL init!");
    return;
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  // Daftarkan broadcast sebagai peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 1;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt  = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap → broadcast channel 1");
    Serial.printf("MAC sender: %s\n", WiFi.macAddress().c_str());
  }
}

unsigned long lastSend = 0;

void loop() {
  // Terapkan perintah kontrol yang datang dari ALCURA
  if (ctrlPending) {
    ctrlPending = false;
    for (int i = 0; i < 5; i++) lampState[i] = ctrlBuf.lampState[i];
    brightness = ctrlBuf.brightness;
    fanState[0] = ctrlBuf.fanState[0];
    fanState[1] = ctrlBuf.fanState[1];
    applyHardware();
    Serial.printf("[CTRL] Lamp:%d%d%d%d%d Bright:%d Fan:%d%d\n",
      lampState[0],lampState[1],lampState[2],lampState[3],lampState[4],
      brightness, fanState[0], fanState[1]);
  }

  if (millis() - lastSend < 1000) return;
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

  // ---- ENS160: VOC, AQI, O2 proxy ----
  if (ens_ok && ens160.available()) {
    ens160.measure(true);
    d.voc = (float)ens160.getTVOC();
    d.aqi = (float)ens160.getAQI();
    float eco2 = (float)ens160.geteCO2();
    d.o2  = constrain(21.0f - (eco2 - 400.0f) * 0.0005f, 18.0f, 22.0f);
  } else {
    d.voc = 58.0f; d.aqi = 1.0f; d.o2 = 21.0f;
  }

  // ---- MLX90614: suhu air (objek) ----
  d.suhuAir = mlx_ok ? (float)mlx.readObjectTempC() : 27.0f;

  // ---- MG811 → PM10 proxy (5–80 ug/m3) ----
  d.pm10 = constrain((float)analogRead(PIN_MG811) / 4095.0f * 80.0f, 5.0f, 80.0f);

  // ---- MQ-2 #1 → PM2.5 proxy (1–35 ug/m3) ----
  d.pm25 = constrain((float)analogRead(PIN_MQ2_1) / 4095.0f * 35.0f, 1.0f, 35.0f);

  // ---- MQ-2 #2 → HCHO proxy (0.01–0.08 ppm) ----
  d.hcho = constrain((float)analogRead(PIN_MQ2_2) / 4095.0f * 0.08f, 0.01f, 0.08f);

  // ---- TDS → DO proxy (4–14 mg/L, TDS tinggi = DO rendah) ----
  float tdsV = (float)analogReadMilliVolts(PIN_TDS) / 1000.0f;
  d.doValue = constrain(14.0f - tdsV * 2.5f, 4.0f, 14.0f);

  // ---- TCS3200 → Turbidity (NTU proxy) ----
  int R = bacaTCS(LOW, LOW), G = bacaTCS(HIGH, HIGH), B = bacaTCS(LOW, HIGH);
  float avgP = (R + G + B) / 3.0f;
  d.turbidity = constrain(avgP / 150.0f, 0.5f, 10.0f);

  // ---- Kirim via ESP-NOW ----
  if (espReady) {
    esp_err_t res = esp_now_send(broadcastMAC, (uint8_t*)&d, sizeof(d));
    Serial.printf("[TX %s] O2:%.1f PM25:%.0f PM10:%.0f HCHO:%.3f VOC:%.0f Hum:%.1f DO:%.1f SuhuAir:%.1f Turb:%.1f\n",
      res == ESP_OK ? "OK" : "ERR",
      d.o2, d.pm25, d.pm10, d.hcho, d.voc, d.humidity, d.doValue, d.suhuAir, d.turbidity);
  }
}
