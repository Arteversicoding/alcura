/*
 * ===========================================================================
 *  BOARD 1 — SENSOR  (Project Jepang / ALCURA)
 * ===========================================================================
 *  Tugas board ini:
 *    1) BACA semua sensor asli yang terpasang
 *    2) KIRIM ke ALCURA TFT via ESP-NOW  (msgType 0 -> SensorData)  -> tampil di LVGL
 *    3) PUSH semua data ke Firebase Realtime Database (HTTP PUT)
 *
 *  Board ini TIDAK mengontrol apa-apa (relay & lampu ada di board lain).
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF).
 *               msgType = 0  -> SensorData (board ini -> ALCURA)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  PIN MAP SENSOR (sesuai yang sudah dites)
 *    Analog (ADC1) : pH=39(VN)  TDS=33  UV=36(VP)
 *                    Gas FF=34  Gas MP-02=35  Gas MQ-135=32
 *    Ultrasonik    : TRIG=5  ECHO=18
 *    DHT11         : GPIO 4
 *    MLX90614 (IR) : I2C SDA=21  SCL=22   (alamat 0x5A, WAJIB pull-up 4.7k)
 *    TCS3200 warna : S0=16  S1=13  S2=14  S3=15  OUT=27  LED=26
 *
 *  ----------------------------------------------------------------------------
 *  CHANNEL (1 radio dipakai ESP-NOW + WiFi sekaligus):
 *    SEMUA board (sensor, ALCURA, relay, lampu) login ke hotspot SAMA -> otomatis
 *    se-channel -> ESP-NOW saling nyambung tanpa atur channel manual. Tinggal
 *    nyalakan hotspot HP bernama "Alcura". (Lihat memori arsitektur project.)
 *  ----------------------------------------------------------------------------
 *
 *  LIBRARY: DHT sensor library + Adafruit Unified Sensor, Adafruit MLX90614.
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_MLX90614.h>

// ===== STRUCT ESP-NOW (HARUS identik byte-for-byte dengan ALCURA) =====
// Urutan field JANGAN diubah. 13 nilai sensor = 13 kartu/grafik di ALCURA.
struct __attribute__((packed)) SensorData {
  uint8_t msgType;     // 0
  // --- Water Quality (kartu biru) ---
  float waterPH;       // pH
  float tds;           // ppm
  float waterTemp;     // C  (MLX90614 objek)
  float waterLevel;    // cm (ultrasonik: jarak ke permukaan air)
  float turbidity;     // NTU (proxy dari TCS3200)
  // --- Air & Climate (kartu hijau) ---
  float uvIndex;       // index 0-12 (UV)
  float airTemp;       // C  (DHT11)
  float humidity;      // %  (DHT11)
  // --- Gas (kartu amber) ---
  float gasH2;         // ppm (simulasi)
  float gasCH4;        // ppm (simulasi)
  float gasCO;         // ppm (REAL dari sensor FF)
  float gasCO2;        // ppm (simulasi)
  float gasO2;         // ppm (simulasi)
};

// ===== PIN SENSOR =====
#define PIN_PH        39
#define PIN_TDS       33
#define PIN_UV        36
#define PIN_GAS_FF    34
#define PIN_GAS_MP02  35
#define PIN_GAS_MQ135 32
#define PIN_TRIG      5
#define PIN_ECHO      18
#define PIN_DHT       4
#define TIPE_DHT      DHT11
#define TCS_S0        16
#define TCS_S1        13
#define TCS_S2        14
#define TCS_S3        15
#define TCS_OUT       27
#define TCS_LED       26

// ===== KALIBRASI (samakan dengan sketch tes-mu) =====
#define PH_V_NETRAL  0.97f    // voltase elektroda saat di air netral (~pH7)
#define PH_SLOPE     -5.70f   // pH per Volt
#define UV_BASELINE  1.166f   // voltase UV saat gelap

// ===== WIFI / FIREBASE =====
// Semua board (sensor, ALCURA, relay, lampu) login ke hotspot SAMA ini.
// Karena semua nyambung 1 hotspot -> otomatis se-channel -> ESP-NOW & Firebase jalan.
const char* STA_SSID = "Alcura";            // <- nama hotspot HP (samakan persis)
const char* STA_PASS = "234alcura156";      // <- password hotspot HP

const char* FB_HOST = "alcura-id-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* FB_PATH = "/sensor.json";       // node tujuan (PUT = timpa data terbaru)
const char* FB_AUTH = "";                    // kosong = rules test-mode

// ===== OBJEK =====
DHT               dht(PIN_DHT, TIPE_DHT);
Adafruit_MLX90614 mlx;

bool     mlx_ok = false, espReady = false;
int      fbCode = 0;                          // kode HTTP push Firebase terakhir
uint8_t  broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

unsigned long lastSend = 0, lastFB = 0, lastProbe = 0;
#define SEND_INTERVAL 1000   // ms antar kirim ESP-NOW
#define FB_INTERVAL   2000   // ms antar push Firebase (2 dtk = lebih segar tapi stabil)

// Buffer data terbaru (diisi tiap baca, dikirim & dipush)
SensorData D;
// Status string untuk Firebase
const char *st_ph, *st_tds, *st_uv, *st_level, *st_turb, *st_air, *st_co;

// ================================================================
//  PEMBACAAN SENSOR
// ================================================================
int adcAvg(int pin) {
  long s = 0;
  for (int i = 0; i < 8; i++) s += analogRead(pin);
  return (int)(s / 8);
}

float bacaPH() {
  float v = adcAvg(PIN_PH) * 3.3f / 4095.0f;
  return constrain(7.0f + PH_SLOPE * (v - PH_V_NETRAL), 0.0f, 14.0f);
}

float bacaTDS(float suhuC) {
  float v  = adcAvg(PIN_TDS) * 3.3f / 4095.0f;
  float vk = v / (1.0f + 0.02f * (suhuC - 25.0f));
  float t  = (133.42f*vk*vk*vk - 255.86f*vk*vk + 857.39f*vk) * 0.5f;
  return constrain(t, 0.0f, 5000.0f);
}

// UV index 0-12 dari voltase (samakan dgn sketch tes)
float bacaUVIndex() {
  float volt = adcAvg(PIN_UV) * 3.3f / 4095.0f;
  float b = volt - UV_BASELINE; if (b <= 0) return 0;
  int mV = (int)(b * 1000);
  if (mV<50)return 0;   if (mV<227)return 1;  if (mV<318)return 2;  if (mV<408)return 3;
  if (mV<503)return 4;  if (mV<606)return 5;  if (mV<696)return 6;  if (mV<795)return 7;
  if (mV<881)return 8;  if (mV<976)return 9;  if (mV<1079)return 10; if (mV<1170)return 11;
  return 12;
}

// Ultrasonik -> jarak cm (level air). -1 kalau gagal.
float bacaLevel() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(3);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long d = pulseIn(PIN_ECHO, HIGH, 25000UL);
    if (d > 0) { float cm = d * 0.034f / 2.0f; if (cm >= 2.0f && cm <= 400.0f) return cm; }
  }
  return -1.0f;
}

// TCS3200 -> periode pulsa 1 kanal (us). Kecil = warna kuat.
int bacaTCS(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2);
  digitalWrite(TCS_S3, s3);
  delay(14);
  return (int)pulseIn(TCS_OUT, LOW, 8000UL);
}

// Turbidity proxy (NTU) dari rata-rata periode RGB.
float bacaTurbidity() {
  digitalWrite(TCS_LED, HIGH); delay(8);
  int R = bacaTCS(LOW, LOW), G = bacaTCS(HIGH, HIGH), B = bacaTCS(LOW, HIGH);
  digitalWrite(TCS_LED, LOW);
  float avg = (R + G + B) / 3.0f;
  return constrain(avg / 60.0f, 0.5f, 50.0f);
}

// Gas dummy yang masuk akal (base +- variasi).
float dummyGas(int base, int variasi) {
  return (float)constrain(base + (int)random(-variasi, variasi + 1), 0, 4095);
}

// ===== STATUS (Inggris, dipakai utk Firebase) =====
const char* stPH(float v)   { return v < 6.5f ? "Acidic"  : v <= 8.5f ? "Optimal" : "Alkaline"; }
const char* stTDS(float v)  { return v < 300  ? "Fresh"   : v < 600   ? "Fair"    : "High"; }
const char* stUV(float v)   { return v <= 2   ? "Low"     : v <= 5     ? "Moderate": v <= 7 ? "High" : "Extreme"; }
const char* stLevel(float v){ return v < 0    ? "Error"   : v < 5      ? "Full"    : v < 15 ? "Normal" : "Low"; }
const char* stTurb(float v) { return v <= 5   ? "Clear"   : v <= 15    ? "Cloudy"  : "Turbid"; }
const char* stAir(float v)  { return (v>=24&&v<=30) ? "Normal" : v<24 ? "Cold" : "Hot"; }
const char* stCO(float v)   { return v < 1000 ? "Safe"    : v < 2500   ? "Warning" : "Hazard"; }

// ================================================================
//  ESP-NOW INIT  (board ini PENGIRIM)
// ================================================================
void espNowInit() {
  // Peer channel = 0 -> ikut channel WiFi saat ini (board ini butuh WiFi utk Firebase).
  // Supaya sampai ke ALCURA (channel 1), pastikan router/hotspot di CHANNEL 1.
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW GAGAL init!");
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 0;            // 0 = pakai channel WiFi sekarang
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap -> broadcast (ikut channel WiFi)");
    Serial.printf("MAC sensor: %s\n", WiFi.macAddress().c_str());
  }
}

// ================================================================
//  KIRIM SEMUA SENSOR -> FIREBASE RTDB (HTTP PUT)  -> SEMUA sensor masuk DB
// ================================================================
void kirimFirebase() {
  if (WiFi.status() != WL_CONNECTED) { fbCode = -1; return; }

  char body[900];
  snprintf(body, sizeof(body),
    "{"
    "\"water\":{"
      "\"ph\":%.2f,\"ph_status\":\"%s\","
      "\"tds\":%.0f,\"tds_status\":\"%s\","
      "\"temp\":%.1f,"
      "\"level\":%.1f,\"level_status\":\"%s\","
      "\"turbidity\":%.1f,\"turbidity_status\":\"%s\"},"
    "\"air\":{"
      "\"uv_index\":%.0f,\"uv_status\":\"%s\","
      "\"temp\":%.1f,\"temp_status\":\"%s\","
      "\"humidity\":%.1f},"
    "\"gas\":{"
      "\"h2\":%.0f,\"ch4\":%.0f,\"co\":%.0f,\"co_status\":\"%s\",\"co2\":%.0f,\"o2\":%.0f},"
    "\"uptime\":%lu"
    "}",
    D.waterPH, st_ph, D.tds, st_tds, D.waterTemp,
    D.waterLevel, st_level, D.turbidity, st_turb,
    D.uvIndex, st_uv, D.airTemp, st_air, D.humidity,
    D.gasH2, D.gasCH4, D.gasCO, st_co, D.gasCO2, D.gasO2,
    millis() / 1000UL);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  String url = String("https://") + FB_HOST + FB_PATH;
  if (strlen(FB_AUTH) > 0) url += String("?auth=") + FB_AUTH;
  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");
    fbCode = https.PUT((uint8_t*)body, strlen(body));
    https.end();
  } else fbCode = -2;
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BOARD SENSOR (ESP-NOW + Firebase) - Project Jepang ===");

  analogReadResolution(12);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT); pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_S0, LOW); digitalWrite(TCS_S1, HIGH);   // skala 2%
  digitalWrite(TCS_LED, LOW);

  dht.begin();

  Wire.begin(21, 22);
  Wire.setClock(50000);          // 50kHz -> MLX90614 stabil
  mlx.begin();                   // aman dipanggil walau sensor belum ada (balik NaN)
  double t = mlx.readObjectTempC();
  mlx_ok = (!isnan(t));
  Serial.printf("MLX90614: %s\n", mlx_ok ? "OK" : "X (cek pull-up 4.7k SDA/SCL)");

  // WiFi STA untuk Firebase (channel ESP-NOW ikut channel router -> set router CH1)
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.printf("Konek WiFi %s ...\n", STA_SSID);

  espNowInit();
  randomSeed(analogRead(PIN_GAS_MP02));   // seed acak utk gas simulasi
  Serial.println("Mulai baca & kirim sensor...");
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  unsigned long now = millis();

  // ---- Baca + kirim ESP-NOW tiap 1 dtk ----
  if (now - lastSend >= SEND_INTERVAL) {
    lastSend = now;
    D.msgType = 0;

    // Suhu air (MLX); fallback 27C
    double tm = mlx_ok ? mlx.readObjectTempC() : NAN;
    if (isnan(tm)) { double r = mlx.readObjectTempC(); if (!isnan(r)) { tm = r; mlx_ok = true; } }
    D.waterTemp = isnan(tm) ? 27.0f : (float)tm;

    // Water Quality
    D.waterPH    = bacaPH();
    D.tds        = bacaTDS(D.waterTemp);
    float lvl    = bacaLevel();
    D.waterLevel = (lvl < 0) ? 0.0f : lvl;
    D.turbidity  = bacaTurbidity();

    // Air & Climate
    D.uvIndex  = bacaUVIndex();
    float h = dht.readHumidity();
    float ta = dht.readTemperature();
    D.humidity = (isnan(h) ? 52.0f : h);
    D.airTemp  = (isnan(ta) ? 28.0f : ta);

    // Gas: CO = sensor FF asli (responsif asap/korek), sisanya simulasi
    int ffRaw = analogRead(PIN_GAS_FF);
    D.gasCO  = (float)ffRaw;
    D.gasH2  = dummyGas(400, 30);
    D.gasCH4 = dummyGas(450, 25);
    D.gasCO2 = dummyGas(600, 40);
    D.gasO2  = dummyGas(800, 35);

    // Status (utk Firebase)
    st_ph = stPH(D.waterPH); st_tds = stTDS(D.tds); st_uv = stUV(D.uvIndex);
    st_level = stLevel(lvl); st_turb = stTurb(D.turbidity);
    st_air = stAir(D.airTemp); st_co = stCO(D.gasCO);

    if (espReady) {
      esp_err_t res = esp_now_send(broadcastMAC, (uint8_t*)&D, sizeof(D));
      Serial.printf("[TX %s] pH:%.2f TDS:%.0f Tw:%.1f Lvl:%.1f Turb:%.1f | UV:%.0f Ta:%.1f Hum:%.0f | CO:%.0f\n",
        res == ESP_OK ? "OK" : "ERR",
        D.waterPH, D.tds, D.waterTemp, D.waterLevel, D.turbidity,
        D.uvIndex, D.airTemp, D.humidity, D.gasCO);
    }
  }

  // ---- Push Firebase tiap 5 dtk (SEMUA sensor masuk DB) ----
  if (now - lastFB >= FB_INTERVAL) {
    lastFB = now;
    kirimFirebase();
    Serial.printf("[FB] code=%d  WiFi=%s  ch=%d\n",
      fbCode, WiFi.status()==WL_CONNECTED ? "OK":"-", WiFi.channel());
  }
}
