/*
 * ===========================================================================
 *  BOARD 1 — SENSOR  (Project Jepang / ALCURA)
 * ===========================================================================
 *  Tugas board ini:
 *    1) BACA semua sensor asli yang terpasang
 *    2) KIRIM ke ALCURA TFT via ESP-NOW  (msgType 0 -> SensorData)
 *    3) PUSH semua data ke Firebase Realtime Database (HTTP PUT)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  ── PIN MAP SENSOR ─────────────────────────────────────────────────────
 *  Analog (ADC1) : pH=39(VN)  TDS=33  UV=36(VP)
 *                  Gas FF=34  Gas MP-02=35  Gas MQ-135=32
 *  Ultrasonik    : TRIG=5  ECHO=18
 *  DHT11         : GPIO 4   (fallback jika AHT21 tidak ada)
 *  MLX90614 (IR) : I2C SDA=21  SCL=22  (0x5A, pull-up 4.7k)
 *  TCS3200 warna : S0=16  S1=13  S2=14  S3=15  OUT=27  LED=26
 *  ENS160+AHT21  : SDA=21  SCL=22  INT=19  (bus I2C sama dgn MLX)
 *  Gravity SEN0460: SDA=21  SCL=22  I2C 0x19  VCC=5V  (bus sama dgn MLX/ENS160)
 *                   Output: PM1.0, PM2.5, PM10 µg/m³
 *
 *  ── WIRING ENS160+AHT21 ─────────────────────────────────────────────
 *   Pin modul │ Hubungkan ke          │ Keterangan
 *  ───────────┼───────────────────────┼───────────────────────────────
 *   VIN       │ 3.3V                  │ Power input modul
 *   3V3       │ (tidak perlu)         │ Output LDO modul, biarkan NC
 *   GND       │ GND                   │
 *   SCL       │ GPIO 22               │ Bus I2C bersama MLX90614
 *   SDA       │ GPIO 21               │ Bus I2C bersama MLX90614
 *   INT       │ GPIO 19               │ Opsional (bisa dicabut)
 *   ADD       │ GND                   │ → ENS160 I2C addr = 0x53
 *   CS        │ 3.3V                  │ Pilih mode I2C (bukan SPI)
 *
 *  ── WIRING Gravity SEN0460 (DFRobot PM2.5/PM10 I2C) ─────────────────
 *   Pin modul │ Hubungkan ke          │ Keterangan
 *  ───────────┼───────────────────────┼───────────────────────────────
 *   VCC       │ 5V                    │ WAJIB 5V (bukan 3.3V!)
 *   GND       │ GND                   │
 *   SCL       │ GPIO 22               │ Bus I2C bersama MLX90614 & ENS160
 *   SDA       │ GPIO 21               │ Bus I2C bersama MLX90614 & ENS160
 *   I2C addr  │ 0x19                  │ Fixed, tidak bisa diubah
 *
 *  ── LIBRARY YG PERLU DIINSTALL (Library Manager) ────────────────────
 *   • DHT sensor library          (Adafruit)
 *   • Adafruit Unified Sensor
 *   • Adafruit MLX90614 library
 *   • ScioSense ENS160            (ScioSense)   ← ENS160
 *   • Adafruit AHTX0              (Adafruit)    ← AHT21
 *   • DFRobot_AirQualitySensor    (DFRobot)     ← Gravity SEN0460 PM2.5/PM10
 *
 *  ⚠  STRUCT SensorData ditambah field pm1_0 & pm10 di AKHIR.
 *     Board ALCURA TFT WAJIB diupdate struct-nya agar ESP-NOW tidak corrupt.
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
#include <ScioSense_ENS160.h>
#include <Adafruit_AHTX0.h>
#include <DFRobot_AirQualitySensor.h>

// ===== STRUCT ESP-NOW (HARUS identik byte-for-byte dengan ALCURA & board lain) =====
// Urutan field JANGAN diubah. Field baru HANYA boleh ditambah di paling bawah.
struct __attribute__((packed)) SensorData {
  uint8_t msgType;   // 0
  // --- Water Quality ---
  float waterPH;     // pH
  float tds;         // ppm
  float waterTemp;   // C (MLX90614)
  float waterLevel;  // cm (ultrasonik)
  float turbidity;   // NTU (TCS3200)
  // --- Air & Climate ---
  float uvIndex;     // 0-12
  float airTemp;     // C (AHT21 / DHT11 fallback)
  float humidity;    // % (AHT21 / DHT11 fallback)
  // --- Gas Sensors (5 gas, semua dari sensor nyata) ---
  float gasH2;    // ppm H2   — MP-02 (GPIO 35), kurva MQ-2
  float gasCH4;   // ppm CH4  — MQ-135 (GPIO 32), kurva CH4
  float gasCO;    // raw ADC  — FF sensor (GPIO 34)
  float gasCO2;   // ppm CO2  — ENS160 eCO2 (lebih akurat dari MQ)
  float gasO2;    // % vol O2 — MP-02 (GPIO 35), estimasi cross-sensitivity
  // --- Kualitas Udara (Gravity SEN0460) ---
  float pm25;    // µg/m³ PM2.5
  float pm10;    // µg/m³ PM10
  float pm1_0;   // µg/m³ PM1.0
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
// ENS160+AHT21
#define ENS160_ADDR   0x53   // ADD→GND; ganti 0x52 jika ADD→VCC
#define ENS160_INT    19     // opsional, tidak dipakai di code ini
// Gravity SEN0460 PM2.5/PM10
#define AIR_SENSOR_ADDR 0x19

// ===== KALIBRASI =====
#define PH_V_NETRAL   0.97f
#define PH_SLOPE      -5.70f
#define UV_BASELINE   1.166f

// ===== WIFI / FIREBASE =====
const char* STA_SSID = "Alcura";
const char* STA_PASS = "234alcura156";
const char* FB_HOST  = "alcura-id-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* FB_PATH  = "/sensor.json";
const char* FB_AUTH  = "";

// ===== OBJEK =====
DHT               dht(PIN_DHT, TIPE_DHT);
Adafruit_MLX90614 mlx;
ScioSense_ENS160  ens160(ENS160_ADDR);
Adafruit_AHTX0    aht;
DFRobot_AirQualitySensor airSensor(&Wire, AIR_SENSOR_ADDR);

bool     mlx_ok = false, ens160_ok = false, aht_ok = false;
bool     airSensor_ok = false, espReady = false;
int      fbCode = 0;
uint8_t  broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

unsigned long lastSend = 0, lastFB = 0;
#define SEND_INTERVAL 1000
#define FB_INTERVAL   2000

SensorData D;

// Status strings untuk Firebase
const char *st_ph, *st_tds, *st_uv, *st_level, *st_turb;
const char *st_air, *st_h2, *st_ch4, *st_co, *st_co2, *st_o2;
const char *st_pm25, *st_pm10;

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

float bacaUVIndex() {
  float volt = adcAvg(PIN_UV) * 3.3f / 4095.0f;
  float b = volt - UV_BASELINE; if (b <= 0) return 0;
  int mV = (int)(b * 1000);
  if (mV<50)return 0;    if (mV<227)return 1;   if (mV<318)return 2;
  if (mV<408)return 3;   if (mV<503)return 4;   if (mV<606)return 5;
  if (mV<696)return 6;   if (mV<795)return 7;   if (mV<881)return 8;
  if (mV<976)return 9;   if (mV<1079)return 10; if (mV<1170)return 11;
  return 12;
}

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

int bacaTCS(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2);
  digitalWrite(TCS_S3, s3);
  delay(14);
  return (int)pulseIn(TCS_OUT, LOW, 8000UL);
}

float bacaTurbidity() {
  digitalWrite(TCS_LED, HIGH); delay(8);
  int R = bacaTCS(LOW, LOW), G = bacaTCS(HIGH, HIGH), B = bacaTCS(LOW, HIGH);
  digitalWrite(TCS_LED, LOW);
  float avg = (R + G + B) / 3.0f;
  return constrain(avg / 60.0f, 0.5f, 50.0f);
}

// ──────────────────────────────────────────────────────────────
//  GAS SENSOR HELPER (MQ-type, ADC 12-bit, Vref=3.3V, VCC=5V)
//  RL  = 10 kΩ dipasang seri antara sensor dan GND
//  Ro  = nilai resistansi sensor di udara bersih (kalibrasi sekali)
//  Ubah MP02_RO / MQ135_RO sesuai hasil kalibrasi sensor kalian.
// ──────────────────────────────────────────────────────────────
#define GAS_RL_K    10.0f   // Load resistor kΩ
#define GAS_VCC      5.0f   // Supply voltage sensor gas
#define MP02_RO      9.83f  // Ro MP-02 di udara bersih (kΩ)
#define MQ135_RO    76.63f  // Ro MQ-135 di udara bersih (kΩ)

static float sensorRs(int raw) {
  float vout = raw * 3.3f / 4095.0f;
  if (vout < 0.01f) return 9999.0f;
  return GAS_RL_K * (GAS_VCC - vout) / vout;
}
static float gasPPM(float rs, float ro, float a, float b) {
  float ratio = rs / ro;
  if (ratio <= 0) return 0;
  return a * powf(ratio, b);
}

// H2 — MP-02 sensor (kurva MQ-2, H2 range 100-10000 ppm)
float bacaH2() {
  float rs = sensorRs(adcAvg(PIN_GAS_MP02));
  return constrain(gasPPM(rs, MP02_RO, 987.99f, -2.162f), 0, 5000);
}
// CH4 — MQ-135 kurva CH4 (1000-25000 ppm range)
float bacaCH4() {
  float rs = sensorRs(adcAvg(PIN_GAS_MQ135));
  return constrain(gasPPM(rs, MQ135_RO, 177.99f, -2.227f), 0, 5000);
}
// O2 — MP-02 estimasi via cross-sensitivity: Rs naik = O2 rendah (gas reduksi berkurang)
// Udara normal = 20.9% O2. Persentase turun saat reducing gas dominan.
float bacaO2() {
  float rs = sensorRs(adcAvg(PIN_GAS_MP02));
  return constrain(20.9f * (rs / MP02_RO), 5.0f, 25.0f);
}

// ===== STATUS (English, dipakai Firebase) =====
const char* stPH(float v)    { return v < 6.5f ? "Acidic"    : v <= 8.5f ? "Optimal"   : "Alkaline"; }
const char* stTDS(float v)   { return v < 300   ? "Fresh"     : v < 600   ? "Fair"       : "High"; }
const char* stUV(float v)    { return v <= 2    ? "Low"       : v <= 5    ? "Moderate"   : v <= 7 ? "High"      : "Extreme"; }
const char* stLevel(float v) { return v < 0     ? "Error"     : v < 5     ? "Full"       : v < 15 ? "Normal"    : "Low"; }
const char* stTurb(float v)  { return v <= 5    ? "Clear"     : v <= 15   ? "Cloudy"     : "Turbid"; }
const char* stAir(float v)   { return (v>=24&&v<=30) ? "Normal" : v<24   ? "Cold"        : "Hot"; }
const char* stCO(float v)    { return v < 1000  ? "Safe"      : v < 2500  ? "Warning"    : "Hazard"; }
const char* stH2(float v)    { return v < 500   ? "Safe"      : v < 1000  ? "Warning"    : "Hazard"; }
const char* stCH4(float v)   { return v < 1000  ? "Safe"      : v < 5000  ? "Warning"    : "Hazard"; }
const char* stCO2(float v)   { return v < 600   ? "Good"      : v < 1000  ? "Moderate"   : "Poor"; }
const char* stO2(float v)    { return v > 19.5f ? "Normal"    : v > 16.0f ? "Low"        : "Critical"; }
// PM2.5 & PM10: WHO 2021 / EPA AQI (µg/m³, 24-hr)
const char* stPM25(float v)  { return v < 12.1f ? "Clean" : v < 35.5f ? "Moderate" : v < 55.5f ? "Sensitive" : v < 150.5f ? "Unhealthy" : "Hazardous"; }
const char* stPM10(float v)  { return v < 54.0f ? "Clean" : v < 154.0f ? "Moderate" : v < 254.0f ? "Sensitive" : v < 354.0f ? "Unhealthy" : "Hazardous"; }

// ================================================================
//  ESP-NOW INIT
// ================================================================
void espNowInit() {
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW GAGAL init!"); return; }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 0;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap -> broadcast (ikut channel WiFi)");
    Serial.printf("MAC sensor: %s\n", WiFi.macAddress().c_str());
  }
}

// ================================================================
//  FIREBASE PUSH  — semua sensor masuk satu node /sensor.json
// ================================================================
void kirimFirebase() {
  if (WiFi.status() != WL_CONNECTED) { fbCode = -1; return; }

  char body[1700];
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
      "\"h2\":%.0f,\"h2_status\":\"%s\","
      "\"ch4\":%.0f,\"ch4_status\":\"%s\","
      "\"co\":%.0f,\"co_status\":\"%s\","
      "\"co2\":%.0f,\"co2_status\":\"%s\","
      "\"o2\":%.1f,\"o2_status\":\"%s\"},"
    "\"uptime\":%lu"
    "}",
    D.waterPH,  st_ph,   D.tds,     st_tds,
    D.waterTemp, D.waterLevel, st_level, D.turbidity, st_turb,
    D.uvIndex,  st_uv,   D.airTemp, st_air,  D.humidity,
    D.gasH2,  st_h2,  D.gasCH4, st_ch4,
    D.gasCO,  st_co,  D.gasCO2, st_co2,
    D.gasO2,  st_o2,
    millis() / 1000UL);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5);   // 5 detik max, tidak nge-hang loop
  HTTPClient https;
  https.setTimeout(5000);
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

  // Ultrasonik
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // TCS3200
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT); pinMode(TCS_LED, OUTPUT);
  digitalWrite(TCS_S0, LOW); digitalWrite(TCS_S1, HIGH);  // skala 2%
  digitalWrite(TCS_LED, LOW);

  // DHT11 (fallback jika AHT21 tidak terpasang)
  dht.begin();

  // I2C bus 50kHz — dipakai MLX90614, ENS160, AHT21
  Wire.begin(21, 22);
  Wire.setClock(50000);

  // MLX90614
  mlx.begin();
  double tmp = mlx.readObjectTempC();
  mlx_ok = (!isnan(tmp));
  Serial.printf("MLX90614 (0x5A): %s\n", mlx_ok ? "OK" : "X (cek pull-up 4.7k SDA/SCL)");

  // AHT21
  aht_ok = aht.begin();
  Serial.printf("AHT21    (0x38): %s\n", aht_ok ? "OK" : "X (cek wiring SDA/SCL)");

  // ENS160
  ens160_ok = ens160.begin();
  if (ens160_ok) {
    ens160.setMode(ENS160_OPMODE_STD);
    Serial.printf("ENS160   (0x%02X): OK (warm-up ±1 menit)\n", ENS160_ADDR);
  } else {
    Serial.printf("ENS160   (0x%02X): X (cek CS→3V3 & ADD→GND)\n", ENS160_ADDR);
  }

  // Gravity SEN0460 PM2.5/PM10 — SEMENTARA DUMMY (sensor belum konek)
  airSensor_ok = false;   // ganti ke: airSensor_ok = airSensor.begin(); saat sudah terpasang
  Serial.println("SEN0460: DUMMY mode (airSensor_ok=false)");

  // WiFi STA
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  WiFi.setSleep(false);   // wajib: tanpa ini modem sleep drop semua paket ESP-NOW TX
  Serial.printf("Konek WiFi %s ...\n", STA_SSID);

  espNowInit();
  Serial.println("Mulai baca & kirim sensor...");
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  unsigned long now = millis();

  // ── Baca + kirim ESP-NOW tiap 1 dtk ──────────────────────────────────
  if (now - lastSend >= SEND_INTERVAL) {
    lastSend = now;
    D.msgType = 0;

    // Suhu air (MLX90614); fallback 27°C
    double tm = mlx_ok ? mlx.readObjectTempC() : NAN;
    if (isnan(tm)) { double r = mlx.readObjectTempC(); if (!isnan(r)) { tm = r; mlx_ok = true; } }
    D.waterTemp = isnan(tm) ? 27.0f : (float)tm;

    // DEBUG (hapus setelah selesai diagnosa)
    Serial.printf("[ADC] pH(39)=%d TDS(33)=%d UV(36)=%d FF(34)=%d MP02(35)=%d MQ135(32)=%d\n",
      adcAvg(39), adcAvg(33), adcAvg(36), adcAvg(34), adcAvg(35), adcAvg(32));
    digitalWrite(PIN_TRIG, LOW); delayMicroseconds(3);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long rawEcho = pulseIn(PIN_ECHO, HIGH, 25000UL);
    Serial.printf("[USON] echo=%ld us → %.1f cm\n", rawEcho, rawEcho * 0.034f / 2.0f);

    // Water Quality
    D.waterPH    = bacaPH();
    D.tds        = bacaTDS(D.waterTemp);
    float lvl    = bacaLevel();
    D.waterLevel = (lvl < 0) ? 0.0f : lvl;
    D.turbidity  = bacaTurbidity();

    // UV
    D.uvIndex = bacaUVIndex();

    // Suhu & RH: AHT21 diutamakan, DHT11 jadi fallback
    if (aht_ok) {
      sensors_event_t ev_h, ev_t;
      aht.getEvent(&ev_h, &ev_t);
      D.airTemp  = isnan(ev_t.temperature)       ? 28.0f : ev_t.temperature;
      D.humidity = isnan(ev_h.relative_humidity) ? 52.0f : ev_h.relative_humidity;
    } else {
      float h = dht.readHumidity(), ta = dht.readTemperature();
      D.airTemp  = isnan(ta) ? 28.0f : ta;
      D.humidity = isnan(h)  ? 52.0f : h;
    }

    // ENS160 — CO2 saja (eCO2, kompensasi T+RH dari AHT21)
    if (ens160_ok) {
      ens160.set_envdata(D.airTemp, D.humidity);
      if (ens160.measure(false)) {
        D.gasCO2 = (float)ens160.geteCO2();
      }
    } else {
      D.gasCO2 = 0.0f;
    }

    // Gravity SEN0460 — PM1.0, PM2.5, PM10 (mode atmosphere = kondisi udara nyata)
    if (airSensor_ok) {
      D.pm1_0 = (float)airSensor.gainParticleConcentration_ugm3(PARTICLE_PM1_0_ATMOSPHERE);
      D.pm25  = (float)airSensor.gainParticleConcentration_ugm3(PARTICLE_PM2_5_ATMOSPHERE);
      D.pm10  = (float)airSensor.gainParticleConcentration_ugm3(PARTICLE_PM10_ATMOSPHERE);
    } else {
      D.pm1_0 = 5.0f; D.pm25 = 8.0f; D.pm10 = 15.0f;  // dummy sementara
    }

    // Gas: semua dari sensor nyata
    D.gasH2  = bacaH2();
    D.gasCH4 = bacaCH4();
    D.gasCO  = (float)adcAvg(PIN_GAS_FF);
    D.gasO2  = bacaO2();

    // Status strings
    st_ph    = stPH(D.waterPH);    st_tds   = stTDS(D.tds);
    st_uv    = stUV(D.uvIndex);    st_level  = stLevel(lvl);
    st_turb  = stTurb(D.turbidity); st_air  = stAir(D.airTemp);
    st_h2    = stH2(D.gasH2);     st_ch4   = stCH4(D.gasCH4);
    st_co    = stCO(D.gasCO);     st_co2   = stCO2(D.gasCO2);
    st_o2    = stO2(D.gasO2);
    st_pm25  = stPM25(D.pm25);
    st_pm10  = stPM10(D.pm10);

    // Kirim ESP-NOW
    if (espReady) {
      esp_err_t res = esp_now_send(broadcastMAC, (uint8_t*)&D, sizeof(D));
      Serial.printf(
        "[TX %s] pH:%.2f TDS:%.0f Tw:%.1f Lvl:%.1f Turb:%.1f "
        "| UV:%.0f Ta:%.1f H:%.0f%% "
        "| H2:%.0f CH4:%.0f CO:%.0f CO2:%.0f O2:%.1f%% "
        "| UltraFine:%.0f FineDust:%.0f(%s) CoarseDust:%.0f(%s)\n",
        res == ESP_OK ? "OK" : "ERR",
        D.waterPH, D.tds, D.waterTemp, D.waterLevel, D.turbidity,
        D.uvIndex, D.airTemp, D.humidity,
        D.gasH2, D.gasCH4, D.gasCO, D.gasCO2, D.gasO2,
        D.pm1_0, D.pm25, st_pm25, D.pm10, st_pm10);
    }
  }

  // ── Push Firebase tiap 2 dtk ──────────────────────────────────────────
  // lastFB diset SETELAH kirimFirebase() selesai (bukan sebelum) supaya kalau
  // HTTPS blocking >2 dtk, Firebase tidak langsung dipanggil ulang dan loop
  // tidak terus-menerus terjebak di Firebase sehingga ESP-NOW tidak sempat jalan.
  if (now - lastFB >= FB_INTERVAL) {
    kirimFirebase();
    lastFB = millis();
    Serial.printf("[FB] code=%d  WiFi=%s  ch=%d\n",
      fbCode, WiFi.status() == WL_CONNECTED ? "OK" : "-", WiFi.channel());
  }
}
