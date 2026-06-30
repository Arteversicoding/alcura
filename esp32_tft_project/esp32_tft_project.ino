/*
 * ALCURA ESP32 TFT — Receiver + Controller
 * 480×320 TFT dengan touch XPT2046
 *
 * Fitur:
 *  - Terima data 9 sensor dari Project Jepang via ESP-NOW (broadcast)
 *  - Tampilkan data real-time di Home + Chart Detail
 *  - Kirim perintah kontrol lampu 1-5 (ON/OFF + brightness) & kipas 1-2 ke sender
 *  - WiFi terhubung ke AP "alcura" password "234alcura156"
 *  - Halaman Settings menampilkan status koneksi WiFi aktual
 *
 * msgType 0 = SensorData (diterima dari sender)
 * msgType 1 = ControlData (dikirim ke sender)
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Channel tetap untuk ESP-NOW (fallback bila WiFi belum konek).
#define ESPNOW_CHANNEL 1

// Semua board (ALCURA, sensor, relay, lampu) login hotspot SAMA ini -> otomatis
// se-channel -> ESP-NOW saling nyambung & Firebase jalan tanpa atur channel manual.
const char* WIFI_SSID = "Alcura";        // <- nama hotspot HP (samakan persis)
const char* WIFI_PASS = "234alcura156";  // <- password hotspot HP

// Firebase RTDB: ALCURA menulis status on/off (kipas/pompa/lampu) ke /control.json.
// (Data sensor ditulis board sensor ke /sensor.json.) Teman ambil dua node ini utk web.
const char* FB_HOST = "alcura-id-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* FB_CTRL_PATH = "/control.json";

TFT_eSPI tft = TFT_eSPI();

// ===== STRUCT ESP-NOW (identik byte-for-byte dengan board sensor) =====
// 13 sensor nyata semua. Urutan field JANGAN diubah tanpa update board sensor.
struct __attribute__((packed)) SensorData {
  uint8_t msgType;
  // Water Quality (chart 1-5, tema biru)
  float waterPH, tds, waterTemp, waterLevel, turbidity;
  // Air & Climate (chart 6-8, tema hijau)
  float uvIndex, airTemp, humidity;
  // Gas Sensors (chart 9-13, tema amber — semua dari sensor nyata)
  float gasH2;    // ppm H2  — MP-02    (chart 9)
  float gasCH4;   // ppm CH4 — MQ-135   (chart 10)
  float gasCO;    // raw ADC — FF sensor (chart 11)
  float gasCO2;   // ppm CO2 — ENS160   (chart 12)
  float gasO2;    // % O2   — MP-02     (chart 13)
  // Kualitas Udara — Gravity SEN0460 (chart 14-16)
  float pm25;    // µg/m³ Fine Dust        (chart 14)
  float pm10;    // µg/m³ Coarse Dust      (chart 15)
  float pm1_0;   // µg/m³ Ultra Fine Dust  (chart 16)
};

struct __attribute__((packed)) ControlData {
  uint8_t msgType;
  bool    lampState[4];
  uint8_t brightness;
  bool    fanState[2];
  bool    pumpState[2];   // 2 pompa udara (board relay)
};

// ===== Live sensor data ===== (default wajar sebelum paket pertama tiba)
SensorData liveData = {0,
  7.2f, 250.0f, 27.0f, 12.0f, 3.0f,              // water
  2.0f, 28.0f, 52.0f,                             // air
  300.0f, 500.0f, 350.0f, 600.0f, 20.9f,         // gas: H2, CH4, CO, CO2(ENS), O2
  8.0f, 15.0f, 5.0f};                             // Fine Dust, Coarse Dust, Ultra Fine Dust
SensorData pendingData;
volatile bool dataFresh    = false;
bool          hasLiveData  = false; // true setelah paket ESP-NOW pertama

// ===== Chart ring buffer — 12 titik per sensor (index 1–13) =====
#define CHART_POINTS 12
#define CHART_COUNT  16
float chartBuf[CHART_COUNT + 1][CHART_POINTS];

// Default chart init (urut index 1..13 sesuai chartSensor)
void initChartBufs() {
  float def[CHART_COUNT + 1] = {0,
    7.2f, 250.0f, 27.0f, 12.0f, 3.0f,          // water
    2.0f, 28.0f, 52.0f,                          // air
    300.0f, 500.0f, 350.0f, 600.0f, 20.9f,      // gas: H2, CH4, CO, CO2(ENS), O2
    8.0f, 15.0f, 5.0f};                          // Fine Dust(14), Coarse Dust(15), Ultra Fine(16)
  for (int s = 1; s <= CHART_COUNT; s++)
    for (int i = 0; i < CHART_POINTS; i++) chartBuf[s][i] = def[s];
}

// Push nilai baru ke buffer (geser kiri, tambah di akhir)
void pushChart(int s, float val) {
  if (s < 1 || s > CHART_COUNT) return;
  for (int i = 0; i < CHART_POINTS - 1; i++) chartBuf[s][i] = chartBuf[s][i + 1];
  chartBuf[s][CHART_POINTS - 1] = val;
}

// ===== Touch =====
uint16_t touchX = 0, touchY = 0;
bool     touchPressed = false;
uint16_t calData[5] = {275, 3620, 264, 3532, 1};

// ===== State machine =====
enum AppState { SPLASH_SCREEN, MENU, HOME, SETTINGS, INFO, ABOUT, CHART_DETAIL, LIGHT, FAN, PUMP };
AppState currentState     = SPLASH_SCREEN;
AppState previousState    = SPLASH_SCREEN;
unsigned long splashStartTime = 0;
int menuSelection         = 0;
int chartSensor           = 0;
int previousMenuSelection = -1;
bool screenDrawn          = false;

// Firebase /control: tandai perlu tulis ulang + waktu tulis terakhir (heartbeat)
bool          controlDirty     = true;
unsigned long lastCtrlPush     = 0;
unsigned long lastCtrlChange   = 0;   // waktu toggle/slider terakhir (utk debounce Firebase)
unsigned long lastCtrlBroadcast= 0;   // waktu resend ESP-NOW terakhir (heartbeat kontrol)

// HOME: true = cukup update angka kotak yg berubah (tanpa wipe layar -> anti-kedip)
bool          homeNeedsValueUpdate = false;
// CHART_DETAIL: true = cukup update angka+grafik (tanpa gambar ulang seluruh halaman)
bool          chartNeedsUpdate = false;

// ===== Light page state =====
bool lampState[4]    = {false, false, false, false};
int  lightBrightness = 100;

// ===== Fan page state =====
bool fanState[2] = {true, false};

// ===== Pompa udara state (2 pompa, board relay) =====
// Default ON untuk aerasi terus-menerus. Tambahkan tombol di UI bila ingin
// dikontrol manual, lalu set nilainya seperti fanState.
bool pumpState[2] = {true, true};

// ===== Settings: WiFi toggle =====
bool wifiState = true;

// ===== ESP-NOW =====
uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool espReady = false;

// ===== Web command polling =====
unsigned long lastWebPoll   = 0;

void sendControl();
void sendControlEspNow();
void pollControlFirebase();

// Forward declaration (parameter function-pointer -> bantu auto-prototype Arduino)
void drawMenuGridCard(int x, int y, int w, int h,
                      void (*iconFn)(int, int, uint16_t),
                      const char* title, const char* subtitle,
                      bool hasStatus, bool statusOn, bool selected);

// Signature beda antara core ESP32 v2.x dan v3.x -> dijaga dengan guard versi.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
#else
void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len == (int)sizeof(SensorData) && data[0] == 0) {
    memcpy(&pendingData, data, sizeof(SensorData));
    dataFresh = true;
  }
}

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onSent(const wifi_tx_info_t*, esp_now_send_status_t) {}
#else
void onSent(const uint8_t*, esp_now_send_status_t) {}
#endif

// Kunci radio ke channel ESP-NOW supaya TX/RX tidak ikut pindah channel WiFi.
// Tanpa ini, paket kontrol sering meleset = toggle terasa delay / kadang gagal.
void lockEspNowChannel() {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void espNowInit() {
  // Login hotspot "Alcura" -> channel radio ALCURA ikut hotspot, sama dgn semua
  // board lain. ESP-NOW (peer.channel=0) otomatis nyambung tanpa atur channel.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  // MATIKAN WiFi modem-sleep biar TX/RX ESP-NOW tidak ke-drop saat radio "tidur".
  // Ini kunci kontrol terasa real-time & tidak "kadang gagal on/off".
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW GAGAL init!");
    return;
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 0;            // 0 = ikut channel WiFi sekarang
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt  = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap -> broadcast (ikut channel hotspot)");
  }
  Serial.printf("MAC ALCURA: %s\n", WiFi.macAddress().c_str());
}

// Broadcast status kontrol via ESP-NOW SAJA (tanpa Firebase) -> instan, jalur utama.
// Dipanggil saat user menekan/menggeser DAN sebagai heartbeat tiap 1 dtk di loop.
void sendControlEspNow() {
  if (!espReady) return;
  ControlData ctrl;
  ctrl.msgType = 1;
  for (int i = 0; i < 4; i++) ctrl.lampState[i] = lampState[i];
  ctrl.brightness = (uint8_t)lightBrightness;
  ctrl.fanState[0] = fanState[0];
  ctrl.fanState[1] = fanState[1];
  ctrl.pumpState[0] = pumpState[0];
  ctrl.pumpState[1] = pumpState[1];
  // Kirim 2x: broadcast bisa drop sesekali; kirim ganda murah & memastikan sampai.
  esp_now_send(broadcastMAC, (uint8_t*)&ctrl, sizeof(ctrl));
  esp_now_send(broadcastMAC, (uint8_t*)&ctrl, sizeof(ctrl));
}

// Dipanggil tiap aksi user (toggle lampu/kipas/pompa, geser brightness).
void sendControl() {
  sendControlEspNow();          // kontrol langsung sampai (tak nunggu Firebase)
  controlDirty   = true;        // tandai utk ditulis ke Firebase (di-debounce di loop)
  lastCtrlChange = millis();    // reset timer "diam" utk debounce HTTPS
}

// Tulis status on/off (kipas/pompa/lampu + brightness) ke Firebase /control.json.
// Dipanggil saat ada perubahan & heartbeat berkala. Teman ambil node ini utk web.
int fbCtrlCode = 0;
void pushControlFirebase() {
  if (WiFi.status() != WL_CONNECTED) { fbCtrlCode = -1; return; }

  char body[400];
  snprintf(body, sizeof(body),
    "{"
    "\"lamp\":{\"l1\":%s,\"l2\":%s,\"l3\":%s,\"l4\":%s,\"brightness\":%d},"
    "\"fan\":{\"fan1\":%s,\"fan2\":%s},"
    "\"pump\":{\"pump1\":%s,\"pump2\":%s},"
    "\"uptime\":%lu"
    "}",
    lampState[0]?"true":"false", lampState[1]?"true":"false",
    lampState[2]?"true":"false", lampState[3]?"true":"false", lightBrightness,
    fanState[0]?"true":"false", fanState[1]?"true":"false",
    pumpState[0]?"true":"false", pumpState[1]?"true":"false",
    millis() / 1000UL);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  String url = String("https://") + FB_HOST + FB_CTRL_PATH;
  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");
    fbCtrlCode = https.PUT((uint8_t*)body, strlen(body));
    https.end();
  } else fbCtrlCode = -2;
}

// ===== WEB COMMAND =====
// Parse field integer dari flat JSON (misal: "seq":12345).
static int jsonInt(const String& j, const char* key) {
  String k = String("\"") + key + "\":";
  int idx = j.indexOf(k);
  if (idx < 0) return -32768;
  return j.substring(idx + k.length()).toInt();
}
// Parse field boolean dari flat JSON (misal: "l1":true).
static bool jsonBool(const String& j, const char* key) {
  String k = String("\"") + key + "\":";
  int idx = j.indexOf(k);
  if (idx < 0) return false;
  idx += k.length();
  while (idx < (int)j.length() && j[idx] == ' ') idx++;
  return j.substring(idx, idx + 4) == "true";
}

// Baca /control.json dari Firebase. Kalau ada field yang BERBEDA dari state lokal
// (artinya web app mengubah sesuatu), terapkan ke state lokal lalu kirim ESP-NOW
// dan redraw layar. Ini membuat web app bisa mengontrol display + relay + LED
// secara realtime — dua arah: display->Firebase dan Firebase->display+boards.
void pollControlFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5);
  HTTPClient https;
  https.setTimeout(5000);
  String url = String("https://") + FB_HOST + FB_CTRL_PATH;
  if (!https.begin(client, url)) return;
  int code = https.GET();
  String body = (code == 200) ? https.getString() : "";
  https.end();
  if (body.length() < 5 || body == "null") return;

  // Parse state dari Firebase (key sama persis dgn yg ditulis pushControlFirebase)
  bool newLamp[4] = {
    jsonBool(body, "l1"), jsonBool(body, "l2"),
    jsonBool(body, "l3"), jsonBool(body, "l4")
  };
  int  newBright   = jsonInt(body, "brightness");
  bool newFan[2]   = { jsonBool(body, "fan1"), jsonBool(body, "fan2") };
  bool newPump[2]  = { jsonBool(body, "pump1"), jsonBool(body, "pump2") };

  // Cek apakah ada perubahan vs state lokal
  bool changed = false;
  for (int i = 0; i < 4; i++) if (newLamp[i] != lampState[i]) { changed = true; break; }
  if (!changed && newBright >= 0 && newBright <= 100 && newBright != lightBrightness) changed = true;
  if (!changed && (newFan[0] != fanState[0] || newFan[1] != fanState[1]))  changed = true;
  if (!changed && (newPump[0] != pumpState[0] || newPump[1] != pumpState[1])) changed = true;

  if (!changed) return;   // Firebase sama dgn lokal, tidak ada yg perlu dilakukan

  // Terapkan perubahan dari Firebase ke state lokal
  for (int i = 0; i < 4; i++) lampState[i] = newLamp[i];
  if (newBright >= 0 && newBright <= 100) lightBrightness = newBright;
  fanState[0]  = newFan[0];  fanState[1]  = newFan[1];
  pumpState[0] = newPump[0]; pumpState[1] = newPump[1];

  sendControlEspNow();   // langsung kirim ke board kontrol + LED via ESP-NOW
  screenDrawn = false;   // paksa redraw halaman aktif (display ikut update)
  Serial.printf("[FB->ESP] lamp:%d%d%d%d bright:%d fan:%d%d pump:%d%d\n",
    lampState[0], lampState[1], lampState[2], lampState[3],
    lightBrightness, fanState[0], fanState[1], pumpState[0], pumpState[1]);
}

// ===== PALET WARNA HIJAU MEWAH =====
#define COLOR_BG_SPLASH      0xF79E
#define COLOR_BG_MENU        0xF79E
#define COLOR_ACCENT_GREEN   0x34CB
#define COLOR_DARK_GREEN     0x1684
#define COLOR_MUTED_GREEN    0x67AD
#define COLOR_LIGHT_GREEN    0xD7BA
#define COLOR_GLOW_CYAN      0x34CB
#define COLOR_TEXT_WHITE     0xFFFF
#define COLOR_TEXT_BLACK     0x0000
#define COLOR_TEXT_GRAY      0x67AD
#define COLOR_TEXT_ORANGE    0x34CB
#define COLOR_CARD_HOME      0x34CB
#define COLOR_CARD_SETTINGS  0x34CB
#define COLOR_CARD_INFO      0x34CB
#define COLOR_BORDER_LIGHT   0xD7BA
#define COLOR_PILL           0x67AD
#define COLOR_BG_HOME_RIGHT  0xF7FE
#define COLOR_GAUGE_TRACK    0x2C49

// ===== Palet khusus halaman MENU (Control Center) =====
#define COLOR_MENU_BG        0xEF7D   // abu-abu sangat muda (latar)
#define COLOR_HOME_DEEP      0x1A66   // hijau tua kartu Home / ikon
#define COLOR_MENU_RING      0x2B48   // cincin dekoratif samar di kartu Home

// Palet tile Home per-grup (water=biru, air=hijau, gas=amber)
#define TILE_WATER_BG  0xDF5F   // light blue
#define TILE_WATER_FG  0x19CD   // dark blue
#define TILE_AIR_BG    0xCEFF   // light green
#define TILE_AIR_FG    0x1684   // dark green
#define TILE_GAS_BG    0xFF10   // light amber
#define TILE_GAS_FG    0xCC40   // dark amber

// ===== Badge helpers (English) per sensor =====
const char* badgePH(float v)    { return v < 6.5f ? "Acidic"    : v <= 8.5f ? "Optimal"   : "Alkaline"; }
const char* badgeTDS(float v)   { return v < 300   ? "Fresh"     : v < 600   ? "Fair"       : "High"; }
const char* badgeTemp(float v)  { return (v>=24&&v<=30) ? "Normal" : v<24    ? "Cold"        : "Hot"; }
const char* badgeLevel(float v) { return v < 5     ? "Full"      : v < 15    ? "Normal"      : "Low"; }
const char* badgeTurb(float v)  { return v <= 5.0f ? "Clear"     : v <= 15.0f ? "Cloudy"    : "Turbid"; }
const char* badgeUV(float v)    { return v <= 2    ? "Low"       : v <= 5    ? "Moderate"    : v <= 7 ? "High" : "Extreme"; }
const char* badgeHum(float v)   { return (v>=40&&v<=60) ? "Ideal": v < 40   ? "Dry"         : "Humid"; }
const char* badgeH2(float v)    { return v < 500   ? "Safe"      : v < 1000  ? "Warning"     : "Hazard"; }
const char* badgeCH4(float v)   { return v < 1000  ? "Safe"      : v < 5000  ? "Warning"     : "Hazard"; }
const char* badgeCO(float v)    { return v < 1000  ? "Safe"      : v < 2500  ? "Warning"     : "Hazard"; }
const char* badgeCO2(float v)   { return v < 600   ? "Good"      : v < 1000  ? "Moderate"    : "Poor"; }
const char* badgeO2(float v)    { return v > 19.5f ? "Normal"    : v > 16.0f ? "Low"         : "Critical"; }
const char* badgePM25(float v)  { return v < 12.1f ? "Clean" : v < 35.5f ? "Moderate" : v < 55.5f ? "Sensitive" : v < 150.5f ? "Unhealthy" : "Hazardous"; }
const char* badgePM10(float v)  { return v < 54.0f ? "Clean" : v < 154.0f ? "Moderate" : v < 254.0f ? "Sensitive" : v < 354.0f ? "Unhealthy" : "Hazardous"; }

// Format float ke string
void fmtVal(char* buf, float v, uint8_t dec) { dtostrf(v, -1, dec, buf); }

// ===== setup / loop =====
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ALCURA (480x320) + ESP-NOW + WiFi ===");

  initChartBufs();
  espNowInit();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  delay(100);

  tft.init();
  tft.setRotation(1);
  tft.setTouch(calData);
  tft.fillScreen(0x050F);
  delay(100);

  splashStartTime = millis();
  Serial.println("Setup complete!");
}

void loop() {
  // Proses data ESP-NOW yang masuk (di luar ISR/callback context)
  if (dataFresh) {
    dataFresh = false;
    bool wasLive = hasLiveData;
    memcpy(&liveData, &pendingData, sizeof(SensorData));
    hasLiveData = true;
    // Push ke ring buffer setiap sensor (1-13)
    pushChart(1,  liveData.waterPH);
    pushChart(2,  liveData.tds);
    pushChart(3,  liveData.waterTemp);
    pushChart(4,  liveData.waterLevel);
    pushChart(5,  liveData.turbidity);
    pushChart(6,  liveData.uvIndex);
    pushChart(7,  liveData.airTemp);
    pushChart(8,  liveData.humidity);
    pushChart(9,  liveData.gasH2);
    pushChart(10, liveData.gasCH4);
    pushChart(11, liveData.gasCO);
    pushChart(12, liveData.gasCO2);
    pushChart(13, liveData.gasO2);
    pushChart(14, liveData.pm25);
    pushChart(15, liveData.pm10);
    pushChart(16, liveData.pm1_0);
    // Paksa redraw HANYA bagian yang perlu (anti-kedip):
    //  - HOME: paket pertama -> full draw (munculkan "Live"); berikutnya update angka saja.
    //  - CHART_DETAIL: gambar ulang grafik.
    if (currentState == HOME) {
      if (!wasLive) screenDrawn = false;
      else          homeNeedsValueUpdate = true;
    } else if (currentState == CHART_DETAIL) {
      chartNeedsUpdate = true;
    }
  }

  if (currentState != previousState) {
    tft.fillScreen(COLOR_BG_SPLASH);
    previousState   = currentState;
    screenDrawn     = false;
    previousMenuSelection = -1;
  }

  switch (currentState) {
    case SPLASH_SCREEN:  showSplashScreen(); break;
    case MENU:           showMainMenu();     break;
    case HOME:           showHome();         break;
    case SETTINGS:       showSettings();     break;
    case INFO:           showInfo();         break;
    case ABOUT:          showAbout();        break;
    case CHART_DETAIL:   showChartDetail();  break;
    case LIGHT:          showLight();        break;
    case FAN:            showFan();          break;
    case PUMP:           showPump();         break;
  }

  // HEARTBEAT ESP-NOW tiap 200ms: kirim ulang status kontrol supaya board kontrol
  // tetap sinkron walau ada paket toggle yang drop (anti "kadang ga on/off").
  if (millis() - lastCtrlBroadcast >= 200UL) {
    lastCtrlBroadcast = millis();
    sendControlEspNow();
  }

  // Firebase /control DEBOUNCED: HTTPS itu blocking (bisa ~1-2 dtk) -> kalau ditulis
  // tiap klik, layar nge-freeze & touch ketelan. Tulis HANYA setelah user berhenti
  // ~700ms, atau heartbeat tiap 10 dtk. Kontrol real-time tetap jalan via ESP-NOW.
  bool ctrlQuiet = millis() - lastCtrlChange >= 700UL;
  if ((controlDirty && ctrlQuiet) || millis() - lastCtrlPush >= 10000UL) {
    controlDirty = false;
    lastCtrlPush = millis();
    pushControlFirebase();
  }

  // Poll /control.json tiap 5 dtk untuk sinkronisasi dua arah dengan web app.
  // Skip saat user aktif di halaman kontrol atau baru saja mengubah sesuatu
  // (3 dtk grace period) supaya sentuhan lokal tidak di-overwrite Firebase.
  bool onCtrlPage  = (currentState == LIGHT || currentState == FAN || currentState == PUMP);
  bool localRecent = millis() - lastCtrlChange < 3000UL;
  if (!onCtrlPage && !localRecent && millis() - lastWebPoll >= 5000UL) {
    lastWebPoll = millis();
    pollControlFirebase();
  }

  delay(50);
}

// ==================== SPLASH SCREEN ====================
void showSplashScreen() {
  unsigned long elapsedTime = millis() - splashStartTime;
  int splashDuration = 3500;
  float progress = min((float)elapsedTime / splashDuration, 1.0f);

  static bool initialized = false;
  if (!initialized) {
    for (int y = 0; y < 320; y++) {
      float yFactor = (float)y / 320.0f;
      uint16_t gradColor = blendGradient(0x07FF, 0x0000, yFactor);
      tft.drawFastHLine(0, y, 480, gradColor);
    }
    delay(100);
    initialized = true;
  }

  if (progress > 0.20f && progress < 0.80f) {
    int outerGlow = 80;
    tft.drawCircle(240, 160, outerGlow, 0x07FF);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xFFFF);
    tft.drawString("ALCURA", 240, 160, 4);
  }

  if (elapsedTime >= (unsigned long)splashDuration) {
    currentState  = MENU;
    menuSelection = 0;
    screenDrawn   = false;
    initialized   = false;
    delay(200);
  }
}

// ===== Color helpers =====
uint16_t blendGradient(uint16_t colorA, uint16_t colorB, float factor) {
  uint8_t ar = (colorA >> 11) & 0x1F, ag = (colorA >> 5) & 0x3F, ab = colorA & 0x1F;
  uint8_t br = (colorB >> 11) & 0x1F, bg = (colorB >> 5) & 0x3F, bb = colorB & 0x1F;
  uint8_t r = ar + (int)((br - ar) * factor);
  uint8_t g = ag + (int)((bg - ag) * factor);
  uint8_t b = ab + (int)((bb - ab) * factor);
  return (r << 11) | (g << 5) | b;
}

uint16_t blend565(uint16_t a, uint16_t b, uint8_t factor) {
  uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint8_t r  = ar + ((br - ar) * factor) / 255;
  uint8_t g  = ag + ((bg - ag) * factor) / 255;
  uint8_t bl = ab + ((bb - ab) * factor) / 255;
  return (r << 11) | (g << 5) | bl;
}

uint16_t alpha565(uint16_t fg, uint16_t bg, float alpha) {
  if (alpha <= 0) return bg;
  if (alpha >= 1) return fg;
  return blend565(bg, fg, (uint8_t)(alpha * 255));
}

void drawRadialGlow(int cx, int cy, int maxRadius, uint16_t centerColor, uint16_t edgeColor) {
  int steps = 24;
  for (int i = steps; i >= 1; i--) {
    int r = (maxRadius * i) / steps;
    uint8_t factor = ((steps - i) * 255) / steps;
    tft.drawCircle(cx, cy, r, blend565(centerColor, edgeColor, factor));
  }
  for (int r = maxRadius; r >= 0; r -= 4) {
    uint8_t factor = ((maxRadius - r) * 200) / maxRadius;
    tft.drawCircle(cx, cy, r, blend565(edgeColor, centerColor, 255 - factor));
  }
}

// ==================== BACK BUTTON ====================
void drawBackButton(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, h / 2, COLOR_DARK_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xFFFF);
  tft.drawString("< Menu", x + w / 2, y + h / 2, 2);
}

bool touchInBackBtn(uint16_t tx, uint16_t ty, int x, int y, int w, int h) {
  return tx >= (uint16_t)x && tx <= (uint16_t)(x + w) && ty >= (uint16_t)y && ty <= (uint16_t)(y + h);
}

// ==================== MAIN MENU ====================
void showMainMenu() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    // touchX dipantulkan (sama seperti halaman lain) -> dx = koordinat visual
    uint16_t dx = (touchX <= 480) ? (480 - touchX) : 0;

    // Kartu Home (kiri)
    if (dx >= 8 && dx <= 196 && touchY >= 88 && touchY < 305) {
      currentState = HOME; screenDrawn = false; delay(200); return;
    }
    // Grid kanan 2x2
    if (dx >= 197 && dx <= 472 && touchY >= 88 && touchY < 305) {
      int col = (dx < 336) ? 0 : 1;          // 0 = kiri (Settings/Lighting), 1 = kanan (Vent/Pump)
      int row = (touchY < 197) ? 0 : 1;      // 0 = atas, 1 = bawah
      AppState grid[2][2] = {{SETTINGS, FAN}, {LIGHT, PUMP}};
      currentState = grid[row][col]; screenDrawn = false; delay(200); return;
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'U' || cmd == 'u' || cmd == 'L' || cmd == 'l') menuSelection = (menuSelection - 1 + 5) % 5;
    else if (cmd == 'D' || cmd == 'd' || cmd == 'R' || cmd == 'r') menuSelection = (menuSelection + 1) % 5;
    else if (cmd == 'E' || cmd == 'e') {
      AppState states[] = {HOME, SETTINGS, FAN, PUMP, LIGHT};
      currentState = states[menuSelection]; screenDrawn = false; delay(200); return;
    }
  }

  if (menuSelection == previousMenuSelection && screenDrawn) return;

  tft.fillScreen(COLOR_MENU_BG);

  // ===== Status bar (waktu + baterai) =====
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 10, 2);
  tft.fillRoundRect(442, 12, 28, 14, 4, COLOR_TEXT_BLACK);
  tft.fillRect(470, 16, 3, 6, COLOR_TEXT_BLACK);

  // ===== Header =====
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("Control Center", 14, 30, 4);
  tft.setTextColor(COLOR_TEXT_GRAY);
  tft.drawString("Greenhouse", 16, 64, 2);
  int gw = tft.textWidth("Greenhouse", 2);
  tft.fillCircle(16 + gw + 8, 72, 2, COLOR_TEXT_GRAY);
  tft.drawString("Node A1", 16 + gw + 16, 64, 2);

  // ===== Pill status koneksi =====
  bool online = (WiFi.status() == WL_CONNECTED) || hasLiveData;
  tft.fillRoundRect(360, 40, 110, 32, 16, COLOR_LIGHT_GREEN);
  tft.fillCircle(380, 56, 5, online ? COLOR_ACCENT_GREEN : COLOR_TEXT_GRAY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_HOME_DEEP);
  tft.drawString(online ? "Online" : "Offline", 392, 56, 2);

  // ===== Kartu Home (kiri) =====
  drawMenuHomeCard(14, 92, 178, 210, menuSelection == 0);

  // ===== Grid kanan 2x2 (subtitle/badge memantulkan state asli) =====
  int fans = (fanState[0] ? 1 : 0) + (fanState[1] ? 1 : 0);
  char fanSub[16];   sprintf(fanSub, "%d fans active", fans);
  char lightSub[18]; sprintf(lightSub, "Brightness %d%%", lightBrightness);
  bool anyLamp = lampState[0] || lampState[1] || lampState[2] || lampState[3] || lampState[4];
  bool anyPump = pumpState[0] || pumpState[1];

  int cw = 130, ch = 100, rx1 = 200, rx2 = 340, ry1 = 92, ry2 = 202;
  drawMenuGridCard(rx1, ry1, cw, ch, drawSlidersIcon, "Settings",    "Preferences",                 false, false,   menuSelection == 1);
  drawMenuGridCard(rx2, ry1, cw, ch, drawFanIcon,     "Ventilation", fanSub,                        true,  fans > 0, menuSelection == 2);
  drawMenuGridCard(rx1, ry2, cw, ch, drawBulbIcon,    "Lighting",    lightSub,                      true,  anyLamp,  menuSelection == 4);
  drawMenuGridCard(rx2, ry2, cw, ch, drawBubbleIcon,  "Air Pump",    anyPump ? "Aktif" : "Standby", true,  anyPump,  menuSelection == 3);

  // Home indicator
  tft.fillRoundRect(210, 309, 60, 5, 2, COLOR_PILL);

  previousMenuSelection = menuSelection;
  screenDrawn = true;
}

void drawRoundedCard(int x, int y, int w, int h, int r, uint16_t color) {
  tft.fillRect(x + r, y, w - 2*r, h, color);
  tft.fillRect(x, y + r, w, h - 2*r, color);
  tft.fillCircle(x + r,     y + r,     r, color);
  tft.fillCircle(x + w - r, y + r,     r, color);
  tft.fillCircle(x + r,     y + h - r, r, color);
  tft.fillCircle(x + w - r, y + h - r, r, color);
}

void drawHouseIcon(int cx, int cy, uint16_t c) {
  tft.fillTriangle(cx, cy - 18, cx - 18, cy + 2, cx + 18, cy + 2, c);
  tft.fillRect(cx - 13, cy + 1, 26, 20, c);
  tft.fillRect(cx - 5,  cy + 9, 10, 12, 0xFFFF);
}

void drawSlidersIcon(int cx, int cy, uint16_t c) {
  tft.fillRect(cx - 13, cy - 9, 26, 2, c);
  tft.fillRect(cx - 13, cy - 1, 26, 2, c);
  tft.fillRect(cx - 13, cy + 7, 26, 2, c);
  tft.fillCircle(cx - 4, cy - 8, 5, c);
  tft.fillCircle(cx + 5, cy,     5, c);
  tft.fillCircle(cx - 2, cy + 8, 5, c);
}

void drawInfoIconPill(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx, cy - 9, 4, c);
  tft.fillRoundRect(cx - 3, cy - 3, 7, 16, 2, c);
}

void drawBulbIcon(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx, cy - 4, 11, c);
  tft.fillRect(cx - 7, cy + 6,  14, 4, c);
  tft.fillRect(cx - 5, cy + 10, 10, 4, c);
  tft.fillRect(cx - 3, cy + 14,  6, 3, c);
}

void drawFanIcon(int cx, int cy, uint16_t c) {
  tft.fillTriangle(cx,   cy-3,  cx-8,  cy-16, cx+6,  cy-14, c);
  tft.fillTriangle(cx+3, cy,    cx+16, cy-6,  cx+14, cy+8,  c);
  tft.fillTriangle(cx,   cy+3,  cx+8,  cy+16, cx-6,  cy+14, c);
  tft.fillTriangle(cx-3, cy,    cx-16, cy+6,  cx-14, cy-8,  c);
  tft.fillCircle(cx, cy, 4, c);
}

// Kartu Home besar (panel kiri Control Center)
void drawMenuHomeCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, 20, COLOR_HOME_DEEP);
  if (selected) tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 22, COLOR_ACCENT_GREEN);

  // Cincin dekoratif samar di pojok kanan atas
  tft.drawCircle(x + w - 24, y + 46, 54, COLOR_MENU_RING);
  tft.drawCircle(x + w - 24, y + 46, 53, COLOR_MENU_RING);

  int cx = x + w / 2, cy = y + 84;
  tft.fillCircle(cx, cy, 46, COLOR_TEXT_WHITE);
  drawHouseIcon(cx, cy, COLOR_HOME_DEEP);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Home", cx, y + 150, 4);
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString("Halaman utama", cx, y + 180, 2);
}

// Kartu kecil grid kanan: kotak ikon + judul + subjudul + badge status opsional.
// iconFn = salah satu drawSlidersIcon / drawFanIcon / drawBulbIcon / drawBubbleIcon
void drawMenuGridCard(int x, int y, int w, int h,
                      void (*iconFn)(int, int, uint16_t),
                      const char* title, const char* subtitle,
                      bool hasStatus, bool statusOn, bool selected) {
  drawRoundedCard(x, y, w, h, 16, COLOR_TEXT_WHITE);
  if (selected) tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 18, COLOR_ACCENT_GREEN);

  // Kotak ikon hijau muda
  int isz = 36, ix = x + 12, iy = y + 14;
  tft.fillRoundRect(ix, iy, isz, isz, 11, COLOR_LIGHT_GREEN);
  iconFn(ix + isz / 2, iy + isz / 2, COLOR_HOME_DEEP);

  // Judul + subjudul
  int tx = ix + isz + 10;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString(title, tx, y + 16, 2);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString(subtitle, tx, y + 42, 1);

  // Badge status ON/OFF
  if (hasStatus) {
    uint16_t sc = statusOn ? COLOR_ACCENT_GREEN : COLOR_TEXT_GRAY;
    const char* st = statusOn ? "ON" : "OFF";
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(sc);
    tft.drawString(st, tx, y + 72, 2);
    int sw = tft.textWidth(st, 2);
    tft.fillCircle(tx + sw + 9, y + 72, 5, sc);
  }
}

// Ikon pompa udara = gelembung (dipakai kartu menu & baris kontrol)
void drawBubbleIcon(int cx, int cy, uint16_t c) {
  tft.drawCircle(cx + 4, cy + 1, 6, c);
  tft.drawCircle(cx - 6, cy + 4, 4, c);
  tft.drawCircle(cx - 1, cy - 7, 3, c);
}

// ===== Sensor icon helpers =====
void drawCloudIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillCircle(cx - 5, cy + 1, 5, c); tft.fillCircle(cx + 5, cy + 1, 5, c);
  tft.fillCircle(cx, cy - 3, 6, c);     tft.fillRect(cx - 9, cy + 1, 19, 5, c);
}
void drawDropIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillTriangle(cx, cy - 9, cx - 6, cy + 1, cx + 6, cy + 1, c);
  tft.fillCircle(cx, cy + 2, 6, c);
}
void drawThermIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillRoundRect(cx - 3, cy - 10, 6, 14, 3, c); tft.fillCircle(cx, cy + 6, 6, c);
}
void drawFlaskIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillRect(cx - 3, cy - 10, 6, 8, c);
  tft.fillRect(cx - 7, cy - 2, 14, 2, c);
  tft.fillRoundRect(cx - 8, cy, 16, 11, 4, c);
}

void drawSensorCard(int x, int y, int w, int h, const char* label, const char* value, const char* unit, uint8_t iconType) {
  drawRoundedCard(x, y, w, h, 12, COLOR_CARD_HOME);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString(label, x + 10, y + 10, 2);
  int icx = x + w - 25, icy = y + 23;
  tft.fillCircle(icx, icy, 16, COLOR_TEXT_WHITE);
  switch (iconType) {
    case 0: drawCloudIcon(icx, icy); break;
    case 1: drawDropIcon(icx, icy);  break;
    case 2: drawThermIcon(icx, icy); break;
    case 3: drawFlaskIcon(icx, icy); break;
  }
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString(value, x + 10, y + 52, 4);
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString(unit, x + 10, y + h - 24, 2);
}

// ===== Small sensor icons for culture card =====
void drawCloudIconSm(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx-3,cy+1,3,c); tft.fillCircle(cx+3,cy+1,3,c);
  tft.fillCircle(cx,cy-2,4,c);   tft.fillRect(cx-5,cy+1,11,3,c);
}
void drawDropIconSm(int cx, int cy, uint16_t c) {
  tft.fillTriangle(cx,cy-6,cx-4,cy,cx+4,cy,c); tft.fillCircle(cx,cy+1,4,c);
}
void drawThermIconSm(int cx, int cy, uint16_t c) {
  tft.fillRoundRect(cx-2,cy-7,4,9,2,c); tft.fillCircle(cx,cy+4,4,c);
}
void drawFlaskIconSm(int cx, int cy, uint16_t c) {
  tft.fillRect(cx-2,cy-7,4,5,c); tft.fillRect(cx-5,cy-2,10,2,c);
  tft.fillRoundRect(cx-5,cy,10,7,3,c);
}

void drawCultureCard(int x, int y, int w, int h,
                     const char* name, const char* val, const char* unit,
                     const char* badge, uint8_t iconType, bool isAir) {
  uint16_t iconBg = isAir ? 0xCEFF : COLOR_LIGHT_GREEN;
  uint16_t iconFg = isAir ? 0x3C1E : COLOR_DARK_GREEN;

  tft.fillRoundRect(x, y, w, h, 10, COLOR_TEXT_WHITE);
  tft.drawRoundRect(x, y, w, h, 10, COLOR_BORDER_LIGHT);

  tft.fillRoundRect(x+5, y+5, 22, 22, 6, iconBg);
  int icx = x + 16, icy = y + 16;
  switch (iconType) {
    case 0: drawCloudIconSm(icx, icy, iconFg); break;
    case 1: drawDropIconSm(icx, icy, iconFg);  break;
    case 2: drawThermIconSm(icx, icy, iconFg); break;
    default: drawFlaskIconSm(icx, icy, iconFg); break;
  }

  int bl = strlen(badge), bw2 = bl * 6 + 10;
  int bx2 = x + w - bw2 - 4;
  tft.fillRoundRect(bx2, y+7, bw2, 16, 8, iconBg);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(iconFg);
  tft.drawString(badge, bx2 + bw2/2, y+15, 1);

  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString(name, x+6, y+31, 1);

  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(val, x+6, y+42, 2);
  int vw = tft.textWidth(val, 2);
  tft.setTextColor(COLOR_TEXT_GRAY);
  tft.drawString(unit, x + 8 + vw, y+48, 1);
}

// Tile sensor kompak dipecah 3 bagian supaya bisa update SEBAGIAN (anti-kedip):
//  - tileCardStatic : kartu + border + strip + NAMA  (digambar SEKALI)
//  - tileValue      : hapus area angka lalu tulis angka+satuan (hanya saat angka berubah)
//  - tileBadge      : hapus area badge lalu tulis badge      (hanya saat status berubah)
void tileCardStatic(int x, int y, int w, int h, const char* name, uint16_t fg) {
  tft.fillRoundRect(x, y, w, h, 8, COLOR_TEXT_WHITE);
  tft.drawRoundRect(x, y, w, h, 8, COLOR_BORDER_LIGHT);
  tft.fillRoundRect(x, y + 6, 4, h - 12, 2, fg);     // strip aksen kiri
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString(name, x + 10, y + 5, 1);
}
void tileValue(int x, int y, int w, const char* val, const char* unit) {
  tft.fillRect(x + 6, y + 18, w - 10, 17, COLOR_TEXT_WHITE);   // hapus HANYA area angka
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(val, x + 10, y + 19, 2);
  int vw = tft.textWidth(val, 2);
  tft.setTextColor(COLOR_TEXT_GRAY);
  tft.drawString(unit, x + 13 + vw, y + 24, 1);
}
void tileBadge(int x, int y, int w, const char* badge, uint16_t bg, uint16_t fg) {
  tft.fillRect(x + w - 66, y + 3, 62, 15, COLOR_TEXT_WHITE);   // hapus HANYA area badge
  int bl = strlen(badge), bw = bl * 6 + 8, bx = x + w - bw - 4;
  tft.fillRoundRect(bx, y + 4, bw, 13, 6, bg);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(fg);
  tft.drawString(badge, bx + bw / 2, y + 11, 1);
}

// Header seksi kecil (titik berwarna + judul + jumlah)
void drawSectionHead(int y, const char* title, const char* count, uint16_t dot, uint16_t txt) {
  tft.fillCircle(13, y, 4, dot);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(title, 22, y, 2);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(txt);
  tft.drawString(count, 474, y, 1);
}

// ==================== HOME PAGE (real-time) ====================
// Layout 3 kolom (x=6/166/326, w=148). chartSensor 1-16. Tile tinggi 28px.
//  Water row1 y=69 | Water row2 y=99
//  Air row1 y=134 (UV/AirTemp/Humidity) | Air row2 y=164 (Fine/Coarse/UltraFine Dust)
//  Gas row1 y=202 | Gas row2 y=232
static const int HOME_TX[16] = {
  6,166,326,  6,166,           // Water 1-5
  6,166,326,                   // Air   6-8
  6,166,326,  6,166,           // Gas   9-13
  6, 166, 326                  // Dust  14-16
};
static const int HOME_TY[16] = {
  69,69,69,   99,99,           // Water rows
  134,134,134,                  // Air row1
  202,202,202, 232,232,         // Gas rows
  164, 164, 164                 // Dust row (Air row2)
};
#define HOME_TW  148
#define HOME_TH   28

void showHome() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    uint16_t dx = (touchX <= 480) ? (480 - touchX) : 0;
    int col = (dx < 160) ? 0 : (dx < 320) ? 1 : 2;

    if (touchY < 62) { currentState = MENU; screenDrawn = false; delay(200); return; }

    int sel = 0;
    if      (touchY >= 69  && touchY < 99)                sel = 1 + col;   // Water row1
    else if (touchY >= 99  && touchY < 128 && col < 2)   sel = 4 + col;   // Water row2
    else if (touchY >= 134 && touchY < 164)               sel = 6 + col;   // Air row1
    else if (touchY >= 164 && touchY < 196)               sel = 14 + col;  // Air row2 (Dust)
    else if (touchY >= 202 && touchY < 232)               sel = 9 + col;   // Gas row1
    else if (touchY >= 232 && touchY < 262 && col < 2)   sel = 12 + col;  // Gas row2

    if (sel >= 1 && sel <= CHART_COUNT) {
      chartSensor = sel;
      currentState = CHART_DETAIL; screenDrawn = false; delay(200); return;
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; screenDrawn = false; delay(200); return; }
  }

  bool fullDraw = !screenDrawn;
  if (!fullDraw && !homeNeedsValueUpdate) return;   // tidak ada yg perlu digambar
  homeNeedsValueUpdate = false;

  // Susun 16 kotak urut chartSensor 1..16 (semua data nyata)
  float vv[16] = {
    liveData.waterPH, liveData.tds, liveData.waterTemp, liveData.waterLevel, liveData.turbidity,
    liveData.uvIndex, liveData.airTemp, liveData.humidity,
    liveData.gasH2, liveData.gasCH4, liveData.gasCO, liveData.gasCO2, liveData.gasO2,
    liveData.pm25, liveData.pm10, liveData.pm1_0 };
  const uint8_t dd[16] = {2,0,1,1,1, 0,1,0, 0,0,0,0,1, 1,1,1};
  const char* nn[16] = {
    "pH Level","TDS","Water Temp","Level","Turbidity",
    "UV Index","Air Temp","Humidity",
    "H2","CH4","CO","CO2","O2",
    "Fine Dust","Coarse Dust","Ultra Fine"};
  const char* uu[16] = {
    "pH","ppm","C","cm","NTU",
    "idx","C","%",
    "ppm","ppm","ADC","ppm","%",
    "µg/m³","µg/m³","µg/m³"};
  const char* bb[16] = {
    badgePH(vv[0]),  badgeTDS(vv[1]),  badgeTemp(vv[2]), badgeLevel(vv[3]), badgeTurb(vv[4]),
    badgeUV(vv[5]),  badgeTemp(vv[6]), badgeHum(vv[7]),
    badgeH2(vv[8]),  badgeCH4(vv[9]), badgeCO(vv[10]),  badgeCO2(vv[11]), badgeO2(vv[12]),
    badgePM25(vv[13]), badgePM10(vv[14]), badgePM25(vv[15]) };
  static char prevVal[16][14], prevBadge[16][14];

  // Bagian STATIS (status bar, header, judul seksi, NAMA kotak) digambar SEKALI -> tak kedip
  if (fullDraw) {
    tft.fillScreen(COLOR_BG_MENU);

    tft.fillRect(0, 0, 480, 30, COLOR_TEXT_WHITE);
    tft.drawFastHLine(0, 30, 480, COLOR_BORDER_LIGHT);
    tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_BLACK);
    tft.drawString("9:41", 10, 8, 2);
    tft.fillRoundRect(440, 8, 30, 14, 3, COLOR_ACCENT_GREEN);
    tft.fillRect(470, 11, 3, 8, COLOR_TEXT_GRAY);

    tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
    tft.drawString("< Sensor Readings", 10, 33, 2);
    tft.setTextColor(COLOR_MUTED_GREEN);
    tft.drawString("Water  -  Air  -  Gas", 10, 51, 1);

    uint16_t pillCol = hasLiveData ? COLOR_ACCENT_GREEN : COLOR_TEXT_GRAY;
    tft.fillRoundRect(388, 33, 82, 22, 11, pillCol);
    tft.fillCircle(401, 44, 4, COLOR_LIGHT_GREEN);
    tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString(hasLiveData ? "Live" : "Wait", 409, 44, 1);
    tft.setTextDatum(TL_DATUM);

    drawSectionHead(63,  "Water Quality", "5 sensors", TILE_WATER_FG, TILE_WATER_FG);
    drawSectionHead(128, "Air & Climate", "6 sensors", TILE_AIR_FG,   COLOR_ACCENT_GREEN);
    drawSectionHead(196, "Gas Sensors",   "5 sensors", TILE_GAS_FG,   TILE_GAS_FG);
    tft.fillRoundRect(210, 300, 60, 5, 2, COLOR_PILL);

    for (int i = 0; i < 16; i++) {
      // i=0-4: Water | i=5-7: Air row1 | i=8-12: Gas | i=13-15: Dust (Air row2, warna hijau)
      uint16_t fg = (i <= 4) ? TILE_WATER_FG : (i <= 7 || i >= 13) ? TILE_AIR_FG : TILE_GAS_FG;
      tileCardStatic(HOME_TX[i], HOME_TY[i], HOME_TW, HOME_TH, nn[i], fg);
      prevVal[i][0] = '\0'; prevBadge[i][0] = '\0';
    }
  }

  // DINAMIS: angka & badge -> update HANYA kalau berubah
  for (int i = 0; i < 16; i++) {
    uint16_t bg = (i <= 4) ? TILE_WATER_BG : (i <= 7 || i >= 13) ? TILE_AIR_BG : TILE_GAS_BG;
    uint16_t fg = (i <= 4) ? TILE_WATER_FG : (i <= 7 || i >= 13) ? TILE_AIR_FG : TILE_GAS_FG;
    char vs[14]; fmtVal(vs, vv[i], dd[i]);
    if (strcmp(vs, prevVal[i]) != 0) {
      tileValue(HOME_TX[i], HOME_TY[i], HOME_TW, vs, uu[i]);
      strncpy(prevVal[i], vs, 13); prevVal[i][13] = '\0';
    }
    if (strcmp(bb[i], prevBadge[i]) != 0) {
      tileBadge(HOME_TX[i], HOME_TY[i], HOME_TW, bb[i], bg, fg);
      strncpy(prevBadge[i], bb[i], 13); prevBadge[i][13] = '\0';
    }
  }
  screenDrawn = true;
}

// ==================== SETTINGS PAGE (WiFi aktual) ====================
#define SUB_BTN_X    0
#define SUB_BTN_Y  278
#define SUB_BTN_W  480
#define SUB_BTN_H   40

void drawWifiIcon(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx, cy, 22, c);  tft.fillCircle(cx, cy, 18, COLOR_TEXT_WHITE);
  tft.fillCircle(cx, cy, 15, c);  tft.fillCircle(cx, cy, 11, COLOR_TEXT_WHITE);
  tft.fillCircle(cx, cy,  8, c);  tft.fillCircle(cx, cy,  4, COLOR_TEXT_WHITE);
  tft.fillRect(cx - 25, cy, 51, 28, COLOR_TEXT_WHITE);
  tft.fillCircle(cx, cy + 12, 5, c);
}

void drawWifiCard() {
  wl_status_t ws  = WiFi.status();
  bool connected  = (ws == WL_CONNECTED);
  uint16_t bg = (!wifiState) ? 0xC618 : connected ? COLOR_ACCENT_GREEN : 0x9CE0;

  tft.fillRoundRect(10, 92, 460, 86, 14, bg);
  tft.fillCircle(65, 135, 33, COLOR_TEXT_WHITE);
  drawWifiIcon(65, 135, bg);

  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("WiFi", 112, 120, 4);
  tft.setTextColor(connected ? COLOR_LIGHT_GREEN : 0xEF7D);

  const char* statusStr = !wifiState ? "Tidak aktif" : connected ? "Terhubung" : "Menghubungkan...";
  tft.drawString(statusStr, 112, 150, 2);

  int tx = 382, ty = 117;
  tft.fillRoundRect(tx, ty, 68, 36, 18, wifiState ? COLOR_DARK_GREEN : 0x8410);
  tft.fillCircle(wifiState ? tx + 50 : tx + 18, 135, 14, COLOR_TEXT_WHITE);
}

void drawWifiStatusCard() {
  tft.fillRoundRect(10, 186, 460, 66, 14, COLOR_TEXT_WHITE);
  tft.drawRoundRect(10, 186, 460, 66, 14, COLOR_BORDER_LIGHT);

  wl_status_t ws = WiFi.status();
  uint16_t dotCol;
  const char *line1, *line2;
  static char ipLine[32];

  if (ws == WL_CONNECTED) {
    dotCol = COLOR_ACCENT_GREEN;
    line1  = "Alcura (Terhubung)";
    snprintf(ipLine, sizeof(ipLine), "IP: %s", WiFi.localIP().toString().c_str());
    line2  = ipLine;
  } else if (!wifiState) {
    dotCol = COLOR_TEXT_GRAY;
    line1  = "WiFi Mati";
    line2  = "Aktifkan WiFi terlebih dahulu";
  } else {
    dotCol = 0xFD20;   // orange = sedang mencoba
    line1  = "Menghubungkan ke Alcura...";
    line2  = "SSID: Alcura | PW: 234alcura156";
  }

  tft.fillCircle(40, 219, 7, dotCol);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(line1, 60, 207, 2);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString(line2, 60, 232, 1);
}

void showSettings() {
  static wl_status_t lastWs = WL_DISCONNECTED;
  wl_status_t ws = WiFi.status();

  if (tft.getTouch(&touchX, &touchY, 200)) {
    // Kembali: tap header
    if (touchY >= 36 && touchY < 90) {
      currentState = MENU; screenDrawn = false; delay(200); return;
    }
    // Toggle WiFi
    if (touchY >= 92 && touchY < 178) {
      wifiState = !wifiState;
      if (wifiState) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);   // konek hotspot "Alcura" (semua board se-channel)
        WiFi.setSleep(false);               // jaga ESP-NOW tetap responsif (tanpa modem-sleep)
      } else {
        // Catatan: matikan WiFi = ALCURA lepas dari hotspot -> data sensor & kontrol
        // bisa terganggu (semua board sinkron lewat hotspot). Biasakan WiFi tetap ON.
        WiFi.disconnect();
      }
      drawWifiCard();
      drawWifiStatusCard();
      lastWs = WiFi.status();
      delay(100);
      return;
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; screenDrawn = false; delay(200); return; }
  }

  // Update status card jika WiFi status berubah (tanpa full redraw)
  if (ws != lastWs && screenDrawn) {
    lastWs = ws;
    drawWifiCard();
    drawWifiStatusCard();
    return;
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);

  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< WiFi", 15, 40, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Koneksi jaringan", 36, 68, 2);

  drawWifiCard();
  drawWifiStatusCard();

  tft.fillRoundRect(210, 307, 60, 5, 2, COLOR_PILL);
  lastWs     = ws;
  screenDrawn = true;
}

// ==================== INFO PAGE ====================
void showInfo() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    if (touchInBackBtn(touchX, touchY, SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H)) {
      currentState = MENU; screenDrawn = false; delay(200); return;
    }
  }
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; delay(200); return; }
  }
  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);
  drawRadialGlow(440, 300, 160, COLOR_GLOW_CYAN, COLOR_BG_MENU);

  tft.fillRect(0, 0, 480, 40, COLOR_TEXT_WHITE);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_CARD_INFO);
  tft.drawString("Info", 20, 10, 4);

  tft.setTextColor(COLOR_TEXT_BLACK);
  int y = 80;
  tft.drawString("Device: ESP32-035", 20, y, 2);           y += 40;
  tft.drawString("Resolution: 480x320", 20, y, 2);         y += 40;
  tft.drawString("Uptime: " + String(millis()/1000) + "s", 20, y, 2); y += 40;
  tft.drawString("ESP-NOW: " + String(espReady ? "aktif" : "off"), 20, y, 2); y += 40;
  tft.drawString("WiFi: " + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "tidak terhubung"), 20, y, 2);

  drawBackButton(SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H);
  screenDrawn = true;
}

// ==================== ABOUT PAGE ====================
void showAbout() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    if (touchInBackBtn(touchX, touchY, SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H)) {
      currentState = MENU; screenDrawn = false; delay(200); return;
    }
  }
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; delay(200); return; }
  }
  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);
  drawRadialGlow(440, 300, 160, COLOR_GLOW_CYAN, COLOR_BG_MENU);
  tft.fillRect(0, 0, 480, 40, COLOR_TEXT_WHITE);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_CARD_HOME);
  tft.drawString("About", 20, 10, 4);
  tft.setTextColor(COLOR_TEXT_BLACK);
  int y = 80;
  tft.drawString("ALCURA", 20, y, 2); y += 40;
  tft.drawString("Version 1.0", 20, y, 2); y += 40;
  tft.drawString("ESP32-035 TFT", 20, y, 2); y += 40;
  tft.drawString("Made with ESP-NOW", 20, y, 2);

  drawBackButton(SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H);
  screenDrawn = true;
}

// ==================== LIGHT PAGE (real-time kontrol) ====================
void drawBrightnessCard() {
  tft.fillRoundRect(10, 90, 460, 90, 14, COLOR_ACCENT_GREEN);
  drawBulbIcon(40, 108, COLOR_TEXT_WHITE);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Brightness", 58, 108, 4);
  char val[8]; sprintf(val, "%d%%", lightBrightness);
  tft.setTextDatum(MR_DATUM); tft.drawString(val, 462, 108, 4);

  int fillW = (430 * lightBrightness) / 100;
  int knobX = 25 + fillW;
  tft.fillRoundRect(25, 143, 430, 14, 7, COLOR_LIGHT_GREEN);
  if (fillW > 0) tft.fillRoundRect(25, 143, fillW, 14, 7, COLOR_DARK_GREEN);
  tft.fillCircle(knobX, 150, 13, COLOR_TEXT_WHITE);
}

void drawLampCard(int idx) {
  int col = idx % 3, row = idx / 3;
  int gx  = 10 + col * 157, gy = 196 + row * 65;
  bool on = lampState[idx];

  if (on) tft.fillRoundRect(gx, gy, 146, 55, 14, COLOR_ACCENT_GREEN);
  else    { tft.fillRoundRect(gx, gy, 146, 55, 14, COLOR_TEXT_WHITE);
            tft.drawRoundRect(gx, gy, 146, 55, 14, COLOR_BORDER_LIGHT); }

  int icx = gx + 23, icy = gy + 28;
  tft.fillCircle(icx, icy, 16, on ? COLOR_DARK_GREEN : 0xC618);
  drawBulbIcon(icx, icy, on ? COLOR_TEXT_WHITE : COLOR_TEXT_GRAY);

  static const char* lampLabels[4] = { "Strip 1", "Strip 2", "Strip 3", "Strip 4" };
  const char* name = lampLabels[idx];
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(on ? COLOR_TEXT_WHITE : COLOR_DARK_GREEN);
  tft.drawString(name, gx + 43, gy + 16, 2);
  tft.setTextColor(on ? COLOR_LIGHT_GREEN : COLOR_TEXT_GRAY);
  tft.drawString(on ? "ON" : "OFF", gx + 43, gy + 39, 2);
}

void showLight() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    uint16_t dx = touchX <= 480 ? 480 - touchX : 0;

    if (touchY >= 36 && touchY < 90) {
      currentState = MENU; screenDrawn = false; delay(200); return;
    }

    // Slider brightness
    if (touchY >= 137 && touchY <= 167 && dx >= 25 && dx <= 455) {
      do {
        dx = touchX <= 480 ? 480 - touchX : 0;
        int nb = (int)(dx - 25) * 100 / 430;
        if (nb < 0) nb = 0; if (nb > 100) nb = 100;
        if (nb != lightBrightness) {
          lightBrightness = nb;
          // Partial redraw nilai + slider
          tft.fillRect(370, 95, 94, 27, COLOR_ACCENT_GREEN);
          char val[8]; sprintf(val, "%d%%", lightBrightness);
          tft.setTextDatum(MR_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
          tft.drawString(val, 462, 108, 4);
          tft.fillRect(22, 137, 440, 30, COLOR_ACCENT_GREEN);
          int fillW = (430 * lightBrightness) / 100;
          int knobX = 25 + fillW;
          tft.fillRoundRect(25, 143, 430, 14, 7, COLOR_LIGHT_GREEN);
          if (fillW > 0) tft.fillRoundRect(25, 143, fillW, 14, 7, COLOR_DARK_GREEN);
          tft.fillCircle(knobX, 150, 13, COLOR_TEXT_WHITE);
          sendControl();   // kirim ke sender secara real-time
        }
      } while (tft.getTouch(&touchX, &touchY, 200));
      return;
    }

    // Toggle lampu
    for (int i = 0; i < 4; i++) {
      int col = i % 3, row = i / 3;
      int gx = 10 + col * 157, gy = 196 + row * 65;
      if (dx >= (uint16_t)gx && dx < (uint16_t)(gx + 146) && touchY >= (uint16_t)gy && touchY < (uint16_t)(gy + 55)) {
        lampState[i] = !lampState[i];
        drawLampCard(i);
        sendControl();   // kirim ke sender
        delay(100); return;
      }
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; screenDrawn = false; delay(200); return; }
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);
  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< Light", 15, 40, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Light control", 36, 68, 2);

  drawBrightnessCard();

  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_GRAY);
  tft.drawString("Lights", 15, 183, 2);

  for (int i = 0; i < 4; i++) drawLampCard(i);

  tft.fillRoundRect(210, 309, 60, 5, 2, COLOR_PILL);
  screenDrawn = true;
}

// ==================== FAN PAGE (real-time kontrol) ====================
void drawBigFanIcon(int cx, int cy, uint16_t c) {
  tft.fillTriangle(cx,   cy-6,  cx-14, cy-28, cx+11, cy-24, c);
  tft.fillTriangle(cx+6, cy,    cx+28, cy-11, cx+24, cy+14, c);
  tft.fillTriangle(cx,   cy+6,  cx+14, cy+28, cx-11, cy+24, c);
  tft.fillTriangle(cx-6, cy,    cx-28, cy+11, cx-24, cy-14, c);
  tft.fillCircle(cx, cy, 7, c);
  tft.fillCircle(cx, cy, 3, COLOR_TEXT_WHITE);
}

void drawFanHeroCard() {
  tft.fillRoundRect(10, 92, 460, 78, 14, COLOR_ACCENT_GREEN);
  tft.fillCircle(65, 131, 33, COLOR_TEXT_WHITE);
  drawBigFanIcon(65, 131, COLOR_ACCENT_GREEN);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Kipas Angin", 112, 116, 4);
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString("Kelola 2 kipas", 112, 148, 2);
}

void drawFanRowCard(int idx) {
  int ry  = 178 + idx * 60, rcy = ry + 26;
  bool on = fanState[idx];

  tft.fillRoundRect(10, ry, 460, 52, 26, COLOR_TEXT_WHITE);
  tft.drawRoundRect(10, ry, 460, 52, 26, COLOR_BORDER_LIGHT);

  tft.fillCircle(44, rcy, 22, on ? COLOR_ACCENT_GREEN : 0xC618);
  drawFanIcon(44, rcy, COLOR_TEXT_WHITE);

  char name[10]; sprintf(name, "Kipas %d", idx + 1);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(name, 76, ry + 16, 2);
  tft.setTextColor(on ? COLOR_ACCENT_GREEN : COLOR_TEXT_GRAY);
  tft.drawString(on ? "Menyala" : "Mati", 76, ry + 37, 2);

  int tx = 390, ty = ry + 10;
  tft.fillRoundRect(tx, ty, 70, 32, 16, on ? COLOR_ACCENT_GREEN : 0xC618);
  if (on) {
    tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("ON", tx + 8, ty + 16, 2);
    tft.fillCircle(tx + 54, ty + 16, 12, COLOR_TEXT_WHITE);
  } else {
    tft.fillCircle(tx + 16, ty + 16, 12, COLOR_TEXT_WHITE);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("OFF", tx + 62, ty + 16, 2);
  }
}

void showFan() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    if (touchY >= 36 && touchY < 90) {
      currentState = MENU; screenDrawn = false; delay(200); return;
    }
    for (int i = 0; i < 2; i++) {
      int ry = 178 + i * 60;
      if (touchY >= (uint16_t)ry && touchY < (uint16_t)(ry + 54)) {
        fanState[i] = !fanState[i];
        drawFanRowCard(i);
        sendControl();   // kirim ke sender
        delay(100); return;
      }
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; screenDrawn = false; delay(200); return; }
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);
  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< Fan", 15, 40, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Kontrol kipas", 36, 68, 2);

  drawFanHeroCard();
  for (int i = 0; i < 2; i++) drawFanRowCard(i);

  tft.fillRoundRect(210, 307, 60, 5, 2, COLOR_PILL);
  screenDrawn = true;
}

// ==================== PUMP PAGE (real-time kontrol) ====================
// Strukturnya sengaja dibuat identik dengan Fan page agar konsisten.
void drawBigBubbleIcon(int cx, int cy, uint16_t c) {
  tft.drawCircle(cx + 8, cy + 2, 12, c); tft.drawCircle(cx + 8, cy + 2, 11, c);
  tft.drawCircle(cx - 11, cy + 8, 8, c); tft.drawCircle(cx - 11, cy + 8, 7, c);
  tft.drawCircle(cx - 3, cy - 14, 6, c); tft.drawCircle(cx - 3, cy - 14, 5, c);
}

void drawPumpHeroCard() {
  tft.fillRoundRect(10, 92, 460, 78, 14, COLOR_ACCENT_GREEN);
  tft.fillCircle(65, 131, 33, COLOR_TEXT_WHITE);
  drawBigBubbleIcon(65, 131, COLOR_ACCENT_GREEN);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Pompa Udara", 112, 116, 4);
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString("Kelola 2 pompa", 112, 148, 2);
}

void drawPumpRowCard(int idx) {
  int ry  = 178 + idx * 60, rcy = ry + 26;
  bool on = pumpState[idx];

  tft.fillRoundRect(10, ry, 460, 52, 26, COLOR_TEXT_WHITE);
  tft.drawRoundRect(10, ry, 460, 52, 26, COLOR_BORDER_LIGHT);

  tft.fillCircle(44, rcy, 22, on ? COLOR_ACCENT_GREEN : 0xC618);
  drawBubbleIcon(44, rcy, COLOR_TEXT_WHITE);

  char name[10]; sprintf(name, "Pompa %d", idx + 1);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(name, 76, ry + 16, 2);
  tft.setTextColor(on ? COLOR_ACCENT_GREEN : COLOR_TEXT_GRAY);
  tft.drawString(on ? "Menyala" : "Mati", 76, ry + 37, 2);

  int tx = 390, ty = ry + 10;
  tft.fillRoundRect(tx, ty, 70, 32, 16, on ? COLOR_ACCENT_GREEN : 0xC618);
  if (on) {
    tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("ON", tx + 8, ty + 16, 2);
    tft.fillCircle(tx + 54, ty + 16, 12, COLOR_TEXT_WHITE);
  } else {
    tft.fillCircle(tx + 16, ty + 16, 12, COLOR_TEXT_WHITE);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("OFF", tx + 62, ty + 16, 2);
  }
}

void showPump() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    if (touchY >= 36 && touchY < 90) {
      currentState = MENU; screenDrawn = false; delay(200); return;
    }
    for (int i = 0; i < 2; i++) {
      int ry = 178 + i * 60;
      if (touchY >= (uint16_t)ry && touchY < (uint16_t)(ry + 54)) {
        pumpState[i] = !pumpState[i];
        drawPumpRowCard(i);
        sendControl();   // kirim ke board relay
        delay(100); return;
      }
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; screenDrawn = false; delay(200); return; }
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);
  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< Pompa", 15, 40, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Kontrol pompa udara", 36, 68, 2);

  drawPumpHeroCard();
  for (int i = 0; i < 2; i++) drawPumpRowCard(i);

  tft.fillRoundRect(210, 307, 60, 5, 2, COLOR_PILL);
  screenDrawn = true;
}

// ==================== CHART DETAIL (real-time dari chartBuf) ====================
#define COLOR_ICON_CIRCLE   0xFFFF
#define COLOR_MUTED_LIGHT   0xD7BA
#define COLOR_CHART_LINE    0x1B48
#define COLOR_CHART_FILL    0xDF5B
#define COLOR_RT_DOT        0xD7BA
#define COLOR_ACCENT_BLUE      0x2C7C
#define COLOR_DARK_BLUE        0x19CD
#define COLOR_MUTED_LIGHT_BLUE 0xBEBE
#define COLOR_MUTED_BLUE       0x6CDB
#define COLOR_LIGHT_BLUE       0xDF5F
#define COLOR_BG_BLUE          0xEF9F
#define COLOR_CHART_LINE_BLUE  0x22B1
#define COLOR_CHART_FILL_BLUE  0xD73E

void drawBigSensorIcon(int cx, int cy, uint8_t type, uint16_t col) {
  switch (type) {
    case 0:
      tft.fillCircle(cx-12,cy+3,11,col); tft.fillCircle(cx+12,cy+3,11,col);
      tft.fillCircle(cx-1,cy-7,13,col);  tft.fillRect(cx-20,cy+3,41,11,col); break;
    case 1:
      tft.fillTriangle(cx,cy-18,cx-13,cy+5,cx+13,cy+5,col);
      tft.fillCircle(cx,cy+6,13,col); break;
    case 2:
      tft.fillRoundRect(cx-5,cy-16,10,22,5,col); tft.fillCircle(cx,cy+10,9,col); break;
    case 3:
      tft.fillRoundRect(cx-6,cy-16,12,8,3,col);
      tft.fillTriangle(cx-17,cy+14,cx-9,cy-7,cx+9,cy-7,col);
      tft.fillTriangle(cx-17,cy+14,cx+17,cy+14,cx+9,cy-7,col); break;
    case 4:
      tft.fillRoundRect(cx-16,cy-11,24,5,2,col);
      tft.fillCircle(cx+11,cy-8,5,col); tft.fillCircle(cx+11,cy-8,2,COLOR_ICON_CIRCLE);
      tft.fillRoundRect(cx-16,cy-2,30,5,2,col);
      tft.fillRoundRect(cx-16,cy+7,20,5,2,col);
      tft.fillCircle(cx+5,cy+10,5,col); tft.fillCircle(cx+5,cy+10,2,COLOR_ICON_CIRCLE); break;
    case 5:
      tft.fillTriangle(cx,cy-16,cx-18,cy-6,cx+18,cy-6,col);
      tft.fillTriangle(cx,cy+4, cx-18,cy-6,cx+18,cy-6,col);
      tft.fillTriangle(cx,cy-4, cx-18,cy+4,cx+18,cy+4,col);
      tft.fillTriangle(cx,cy+14,cx-18,cy+4,cx+18,cy+4,col);
      tft.drawLine(cx-18,cy-6,cx,cy+3,COLOR_ICON_CIRCLE);
      tft.drawLine(cx+18,cy-6,cx,cy+3,COLOR_ICON_CIRCLE); break;
    case 6:
      tft.drawLine(cx-12,cy-10,cx+11,cy+8,col);
      tft.drawLine(cx-12,cy-10,cx+11,cy-9,col);
      tft.fillCircle(cx-13,cy-10,5,col);
      tft.fillCircle(cx+12,cy-9, 5,col);
      tft.fillCircle(cx+12,cy+9, 5,col); break;
    case 7:
      for (int wy=-10;wy<=10;wy+=9) for (int wx=-16;wx<=6;wx+=11) {
        tft.drawLine(wx+cx,cy+wy,wx+cx+5,cy+wy-4,col);
        tft.drawLine(wx+cx+5,cy+wy-4,wx+cx+11,cy+wy,col);
        tft.drawLine(wx+cx,cy+wy+1,wx+cx+5,cy+wy-3,col);
        tft.drawLine(wx+cx+5,cy+wy-3,wx+cx+11,cy+wy+1,col);
      } break;
  }
}

void showChartDetail() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    uint16_t dx = (touchX <= 480) ? (480 - touchX) : 0;
    if (dx <= 170 && touchY <= 55) {
      currentState = HOME; screenDrawn = false; delay(200); return;
    }
  }
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = HOME; delay(200); return; }
  }
  bool fullDraw = !screenDrawn;
  if (!fullDraw && !chartNeedsUpdate) return;   // tidak ada yg perlu digambar
  chartNeedsUpdate = false;
  drawChartDetail(chartSensor, fullDraw);
  screenDrawn = true;
}

void drawChartDetail(int s, bool fullDraw) {
  // Metadata per sensor (title, subtitle, unit, yMin, yMax, yLabels, iconType, isWater, isDegree)
  const char *title, *subtitle, *unit;
  const char* yLabels[5];
  float yMin, yMax;
  bool trendUp, isDegree = false, isWater = false;
  uint8_t iconType;

  // Nilai real-time dari liveData untuk bigValue + stats dari chartBuf[s]
  float curVal = 0;

  switch (s) {
    case 1: // pH
      title="pH Level"; subtitle="Water Acidity"; unit="pH";
      yMin=0; yMax=14; iconType=3; isWater=true;
      yLabels[0]="0"; yLabels[1]="3.5"; yLabels[2]="7.0"; yLabels[3]="10.5"; yLabels[4]="14";
      curVal=liveData.waterPH; break;
    case 2: // TDS
      title="TDS"; subtitle="Dissolved Solids"; unit="ppm";
      yMin=0; yMax=1000; iconType=1; isWater=true;
      yLabels[0]="0"; yLabels[1]="250"; yLabels[2]="500"; yLabels[3]="750"; yLabels[4]="1000";
      curVal=liveData.tds; break;
    case 3: // Water Temp
      title="Water Temp"; subtitle="Water Temperature"; unit="C";
      yMin=20; yMax=35; iconType=2; isWater=true; isDegree=true;
      yLabels[0]="20"; yLabels[1]="24"; yLabels[2]="28"; yLabels[3]="31"; yLabels[4]="35";
      curVal=liveData.waterTemp; break;
    case 4: // Water Level
      title="Water Level"; subtitle="Tank Distance"; unit="cm";
      yMin=0; yMax=30; iconType=1; isWater=true;
      yLabels[0]="0"; yLabels[1]="8"; yLabels[2]="15"; yLabels[3]="23"; yLabels[4]="30";
      curVal=liveData.waterLevel; break;
    case 5: // Turbidity
      title="Turbidity"; subtitle="Water Clarity"; unit="NTU";
      yMin=0; yMax=50; iconType=3; isWater=true;
      yLabels[0]="0"; yLabels[1]="12"; yLabels[2]="25"; yLabels[3]="37"; yLabels[4]="50";
      curVal=liveData.turbidity; break;
    case 6: // UV Index
      title="UV Index"; subtitle="UV Radiation"; unit="idx";
      yMin=0; yMax=12; iconType=0;
      yLabels[0]="0"; yLabels[1]="3"; yLabels[2]="6"; yLabels[3]="9"; yLabels[4]="12";
      curVal=liveData.uvIndex; break;
    case 7: // Air Temp
      title="Air Temp"; subtitle="Ambient Temperature"; unit="C";
      yMin=20; yMax=40; iconType=2; isDegree=true;
      yLabels[0]="20"; yLabels[1]="25"; yLabels[2]="30"; yLabels[3]="35"; yLabels[4]="40";
      curVal=liveData.airTemp; break;
    case 8: // Humidity
      title="Humidity"; subtitle="Air Humidity"; unit="%";
      yMin=30; yMax=90; iconType=1;
      yLabels[0]="30"; yLabels[1]="45"; yLabels[2]="60"; yLabels[3]="75"; yLabels[4]="90";
      curVal=liveData.humidity; break;
    case 9: // H2 — MP-02
      title="H2"; subtitle="Hydrogen (MP-02)"; unit="ppm";
      yMin=0; yMax=2000; iconType=0;
      yLabels[0]="0"; yLabels[1]="500"; yLabels[2]="1000"; yLabels[3]="1500"; yLabels[4]="2000";
      curVal=liveData.gasH2; break;
    case 10: // CH4 — MQ-135
      title="CH4"; subtitle="Methane (MQ-135)"; unit="ppm";
      yMin=0; yMax=5000; iconType=0;
      yLabels[0]="0"; yLabels[1]="1250"; yLabels[2]="2500"; yLabels[3]="3750"; yLabels[4]="5000";
      curVal=liveData.gasCH4; break;
    case 11: // CO — FF sensor
      title="CO"; subtitle="Carbon Monoxide (FF)"; unit="ADC";
      yMin=0; yMax=4095; iconType=0;
      yLabels[0]="0"; yLabels[1]="1024"; yLabels[2]="2048"; yLabels[3]="3072"; yLabels[4]="4095";
      curVal=liveData.gasCO; break;
    case 12: // CO2 — ENS160 eCO2
      title="CO2"; subtitle="CO2 (ENS160)"; unit="ppm";
      yMin=400; yMax=3000; iconType=0;
      yLabels[0]="400"; yLabels[1]="900"; yLabels[2]="1600"; yLabels[3]="2300"; yLabels[4]="3000";
      curVal=liveData.gasCO2; break;
    case 13: // O2 — MP-02 proxy
      title="O2"; subtitle="Oxygen % (MP-02)"; unit="%";
      yMin=5; yMax=25; iconType=0;
      yLabels[0]="5"; yLabels[1]="10"; yLabels[2]="15"; yLabels[3]="20"; yLabels[4]="25";
      curVal=liveData.gasO2; break;
    case 14: // Fine Dust PM2.5 — SEN0460
      title="Fine Dust"; subtitle="PM2.5 (SEN0460)"; unit="ug/m3";
      yMin=0; yMax=150; iconType=7;
      yLabels[0]="0"; yLabels[1]="12"; yLabels[2]="55"; yLabels[3]="100"; yLabels[4]="150";
      curVal=liveData.pm25; break;
    case 15: // Coarse Dust PM10 — SEN0460
      title="Coarse Dust"; subtitle="PM10 (SEN0460)"; unit="ug/m3";
      yMin=0; yMax=354; iconType=7;
      yLabels[0]="0"; yLabels[1]="54"; yLabels[2]="154"; yLabels[3]="254"; yLabels[4]="354";
      curVal=liveData.pm10; break;
    default: // 16 = Ultra Fine Dust PM1.0 — SEN0460
      title="Ultra Fine"; subtitle="PM1.0 (SEN0460)"; unit="ug/m3";
      yMin=0; yMax=100; iconType=7;
      yLabels[0]="0"; yLabels[1]="12"; yLabels[2]="35"; yLabels[3]="65"; yLabels[4]="100";
      curVal=liveData.pm1_0; break;
  }

  // Hitung min/avg/max dari ring buffer
  float bMin = chartBuf[s][0], bMax = chartBuf[s][0], bSum = 0;
  for (int i = 0; i < CHART_POINTS; i++) {
    if (chartBuf[s][i] < bMin) bMin = chartBuf[s][i];
    if (chartBuf[s][i] > bMax) bMax = chartBuf[s][i];
    bSum += chartBuf[s][i];
  }
  trendUp = (chartBuf[s][CHART_POINTS-1] >= chartBuf[s][0]);

  // Desimal: pH=2; TDS/Humidity/gas(9-12)=0; O2=1; PM2.5=1; lainnya=1
  uint8_t dec = 1;
  if (s == 1) dec = 2;
  else if (s == 2 || s == 8 || (s >= 9 && s <= 12)) dec = 0;

  char statMin[12], statAvg[12], statMax[12], bigValue[12];
  dtostrf(bMin, -1, dec, statMin);
  dtostrf(bSum / CHART_POINTS, -1, dec, statAvg);
  dtostrf(bMax, -1, dec, statMax);
  dtostrf(curVal, -1, dec, bigValue);

  // Trend string: diff antara titik terakhir dan pertama
  char trendStr[10];
  float diff = chartBuf[s][CHART_POINTS-1] - chartBuf[s][0];
  float pct  = (bMax - bMin) > 0.001f ? fabsf(diff) / ((bMax + bMin) / 2.0f) * 100.0f : 0.0f;
  dtostrf(pct, -1, 1, trendStr);
  strcat(trendStr, "%");

  // Pilih palet tema
  uint16_t thAccent  = isWater ? COLOR_ACCENT_BLUE       : COLOR_ACCENT_GREEN;
  uint16_t thMutedLt = isWater ? COLOR_MUTED_LIGHT_BLUE  : COLOR_MUTED_LIGHT;
  uint16_t thDark    = isWater ? COLOR_DARK_BLUE         : COLOR_DARK_GREEN;
  uint16_t thMuted   = isWater ? COLOR_MUTED_BLUE        : COLOR_MUTED_GREEN;
  uint16_t thBgRight = isWater ? COLOR_BG_BLUE           : COLOR_BG_MENU;
  uint16_t thLine    = isWater ? COLOR_CHART_LINE_BLUE   : COLOR_CHART_LINE;
  uint16_t thFill    = isWater ? COLOR_CHART_FILL_BLUE   : COLOR_CHART_FILL;
  uint16_t thLight   = isWater ? COLOR_LIGHT_BLUE        : COLOR_LIGHT_GREEN;

  // Geometri plot & posisi X (statis, tak bergantung data)
  const int pL = 232, pR = 462, pT = 56, pB = 222;
  int xp[CHART_POINTS], yp[CHART_POINTS];
  for (int i = 0; i < CHART_POINTS; i++) xp[i] = pL + (pR - pL) * i / (CHART_POINTS - 1);
  int sy = 260, sh = 54, sx[3] = {190, 287, 384};

  // ============ BAGIAN STATIS (digambar SEKALI saat masuk halaman) ============
  if (fullDraw) {
    // ----- PANEL KIRI -----
    tft.fillRect(0, 0, 184, 320, thAccent);
    tft.drawLine(20,19,12,26,0xFFFF); tft.drawLine(12,26,20,33,0xFFFF);
    tft.drawLine(21,19,13,26,0xFFFF); tft.drawLine(13,26,21,33,0xFFFF);
    tft.setTextDatum(ML_DATUM); tft.setTextColor(0xFFFF);
    tft.drawString("Kembali", 30, 26, 2);

    tft.fillCircle(92, 92, 32, COLOR_ICON_CIRCLE);
    drawBigSensorIcon(92, 92, iconType, thAccent);

    tft.setTextDatum(MC_DATUM); tft.setTextColor(0xFFFF);
    tft.drawString(title, 92, 144, 4);
    tft.setTextColor(thMutedLt);
    tft.drawString(subtitle, 92, 168, 2);

    tft.setTextColor(thMutedLt);
    if (isDegree) {
      tft.setTextDatum(ML_DATUM);
      tft.drawCircle(80, 230, 3, thMutedLt);
      tft.drawString("C", 87, 232, 2);
    } else {
      tft.setTextDatum(MC_DATUM);
      tft.drawString(unit, 92, 232, 2);
    }

    tft.fillCircle(40, 296, 4, thMutedLt);
    tft.setTextDatum(ML_DATUM); tft.setTextColor(0xFFFF);
    tft.drawString(hasLiveData ? "Real-time" : "Demo data", 52, 296, 2);

    // ----- PANEL KANAN (kerangka) -----
    tft.fillRect(184, 0, 296, 320, thBgRight);
    drawRoundedCard(190, 8, 284, 248, 14, 0xFFFF);
    tft.setTextDatum(TL_DATUM); tft.setTextColor(thDark);
    tft.drawString("Tren 12 Titik Terakhir", 202, 18, 2);
    tft.setTextDatum(TR_DATUM); tft.setTextColor(thMuted);
    tft.drawString("~12 detik", 462, 18, 2);
    tft.drawFastHLine(202, 40, 260, COLOR_BORDER_LIGHT);

    // Label Y (di luar area plot -> statis)
    for (int i = 0; i < 5; i++) {
      int gy = pB - (pB - pT) * i / 4;
      tft.setTextDatum(MR_DATUM); tft.setTextColor(thMuted);
      tft.drawString(yLabels[i], pL - 6, gy, 1);
    }
    // Label X (statis)
    const char* xl[4] = {"T-11", "T-7", "T-4", "T-0"};
    int xi[4] = {0, 4, 8, 11};
    tft.setTextDatum(TC_DATUM); tft.setTextColor(thMuted);
    for (int k = 0; k < 4; k++) tft.drawString(xl[k], xp[xi[k]], pB + 8, 1);

    // Kartu statistik (kerangka + label, nilainya dinamis di bawah)
    const char* slab[3] = {"Min", "Avg", "Max"};
    for (int k = 0; k < 3; k++) {
      drawRoundedCard(sx[k], sy, 90, sh, 10, thLight);
      tft.setTextDatum(TC_DATUM); tft.setTextColor(thMuted);
      tft.drawString(slab[k], sx[k] + 45, sy + 8, 2);
    }
  }

  // ============ BAGIAN DINAMIS (update tiap data, area kecil saja) ============
  // --- Nilai besar (panel kiri): hapus area lalu tulis ulang ---
  tft.fillRect(10, 184, 164, 42, thAccent);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(0xFFFF);
  tft.drawString(bigValue, 92, 204, 6);
  if (isDegree) {
    int w = tft.textWidth(bigValue, 6);
    tft.drawCircle(92 + w/2 + 9, 190, 5, 0xFFFF);
    tft.drawCircle(92 + w/2 + 9, 190, 4, 0xFFFF);
  }

  // --- Pill tren: hapus area lalu tulis ulang ---
  int pw = 88, ph = 24, px = 92 - pw/2, py = 252;
  tft.fillRect(px, py, pw, ph, thAccent);
  tft.fillRoundRect(px, py, pw, ph, ph/2, thLight);
  int tx2 = px + 22, ty2 = py + ph/2;
  if (trendUp) tft.fillTriangle(tx2, ty2-5, tx2-5, ty2+4, tx2+5, ty2+4, thAccent);
  else         tft.fillTriangle(tx2, ty2+5, tx2-5, ty2-4, tx2+5, ty2-4, thAccent);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(thAccent);
  tft.drawString(trendStr, tx2 + 12, ty2, 2);

  // --- Plot: hapus interior, gambar grid + area + garis + titik ---
  tft.fillRect(pL, pT, pR - pL, pB - pT + 1, 0xFFFF);   // bersihkan HANYA area plot
  for (int i = 0; i < 5; i++) {
    int gy = pB - (pB - pT) * i / 4;
    for (int gx = pL; gx < pR; gx += 8) tft.drawFastHLine(gx, gy, 4, COLOR_BORDER_LIGHT);
  }
  float rng = yMax - yMin;
  for (int i = 0; i < CHART_POINTS; i++) {
    float t = (rng > 0) ? constrain((chartBuf[s][i] - yMin) / rng, 0.0f, 1.0f) : 0.5f;
    yp[i] = pB - (int)(t * (pB - pT));
  }
  for (int i = 0; i < CHART_POINTS - 1; i++) {
    int x0 = xp[i], x1 = xp[i + 1];
    for (int x = x0; x <= x1; x++) {
      float f = (x1 == x0) ? 0 : (float)(x - x0) / (x1 - x0);
      int y = yp[i] + (int)((yp[i + 1] - yp[i]) * f);
      if (y < pB) tft.drawFastVLine(x, y, pB - y, thFill);
    }
  }
  for (int i = 0; i < CHART_POINTS - 1; i++) {
    tft.drawLine(xp[i], yp[i],     xp[i+1], yp[i+1],     thLine);
    tft.drawLine(xp[i], yp[i] - 1, xp[i+1], yp[i+1] - 1, thLine);
    tft.drawLine(xp[i], yp[i] + 1, xp[i+1], yp[i+1] + 1, thLine);
  }
  for (int i = 0; i < CHART_POINTS; i++) {
    if (i == CHART_POINTS - 1) {
      tft.fillCircle(xp[i], yp[i], 7, thLight);
      tft.fillCircle(xp[i], yp[i], 4, thLine);
    } else {
      tft.fillCircle(xp[i], yp[i], 3, 0xFFFF);
      tft.drawCircle(xp[i], yp[i], 3, thLine);
    }
  }

  // --- Nilai statistik (Min/Avg/Max): hapus area nilai lalu tulis ulang ---
  const char* sval[3] = {statMin, statAvg, statMax};
  for (int k = 0; k < 3; k++) {
    tft.fillRect(sx[k] + 6, sy + 22, 78, 28, thLight);
    tft.setTextDatum(TC_DATUM); tft.setTextColor(thDark);
    tft.drawString(sval[k], sx[k] + 45, sy + 26, 4);
  }
}
