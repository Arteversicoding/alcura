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

TFT_eSPI tft = TFT_eSPI();

// ===== STRUCT ESP-NOW (identik dengan sender — urutan field jangan diubah) =====
struct __attribute__((packed)) SensorData {
  uint8_t msgType;
  float   o2, pm25, pm10, hcho, voc, humidity, doValue, suhuAir, turbidity, aqi;
};

struct __attribute__((packed)) ControlData {
  uint8_t msgType;
  bool    lampState[5];
  uint8_t brightness;
  bool    fanState[2];
  bool    pumpState[2];   // 2 pompa udara (board relay)
};

// ===== Live sensor data =====
SensorData liveData    = {0, 21.0f, 8.0f, 25.0f, 0.03f, 58.0f, 52.0f, 11.2f, 27.0f, 4.1f, 1.0f};
SensorData pendingData;
volatile bool dataFresh    = false;
bool          hasLiveData  = false; // true setelah paket ESP-NOW pertama

// ===== Chart ring buffer — 12 titik per sensor (index 1–9) =====
#define CHART_POINTS 12
float chartBuf[10][CHART_POINTS];

// Default chart init (sesuai nilai awal liveData agar chart terlihat wajar sebelum data tiba)
void initChartBufs() {
  float def[10] = {0, 11.2f, 27.0f, 4.1f, 21.0f, 8.0f, 25.0f, 0.03f, 58.0f, 52.0f};
  for (int s = 1; s <= 9; s++)
    for (int i = 0; i < CHART_POINTS; i++) chartBuf[s][i] = def[s];
}

// Push nilai baru ke buffer (geser kiri, tambah di akhir)
void pushChart(int s, float val) {
  if (s < 1 || s > 9) return;
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

// ===== Light page state =====
bool lampState[5]    = {true, true, false, true, false};
int  lightBrightness = 72;

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

void sendControl();

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

void espNowInit() {
  WiFi.mode(WIFI_STA);
  // Mulai koneksi ke AP "alcura"
  WiFi.begin("alcura", "234alcura156");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW GAGAL init!");
    return;
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 1;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt  = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap → broadcast channel 1");
  }
  Serial.printf("MAC ALCURA: %s\n", WiFi.macAddress().c_str());
}

void sendControl() {
  if (!espReady) return;
  ControlData ctrl;
  ctrl.msgType = 1;
  for (int i = 0; i < 5; i++) ctrl.lampState[i] = lampState[i];
  ctrl.brightness = (uint8_t)lightBrightness;
  ctrl.fanState[0] = fanState[0];
  ctrl.fanState[1] = fanState[1];
  ctrl.pumpState[0] = pumpState[0];
  ctrl.pumpState[1] = pumpState[1];
  esp_now_send(broadcastMAC, (uint8_t*)&ctrl, sizeof(ctrl));
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

// ===== Badge helpers untuk nilai sensor =====
const char* badgeO2(float v)   { return v >= 20.5f ? "Optimal" : v >= 19.0f ? "Normal" : "Rendah"; }
const char* badgePM25(float v) { return v <= 12.0f ? "Baik" : v <= 35.0f ? "Sedang" : "Buruk"; }
const char* badgePM10(float v) { return v <= 54.0f ? "Baik" : v <= 100.0f ? "Sedang" : "Buruk"; }
const char* badgeHCHO(float v) { return v <= 0.04f ? "Aman" : v <= 0.08f ? "Waspada" : "Bahaya"; }
const char* badgeVOC(float v)  { return v <= 65.0f ? "Baik" : v <= 220.0f ? "Sedang" : "Buruk"; }
const char* badgeHum(float v)  { return (v >= 40 && v <= 60) ? "Ideal" : v < 40 ? "Kering" : "Lembap"; }
const char* badgeDO(float v)   { return v >= 8.0f ? "Baik" : v >= 5.0f ? "Cukup" : "Rendah"; }
const char* badgeTemp(float v) { return (v >= 24 && v <= 30) ? "Normal" : v < 24 ? "Dingin" : "Panas"; }
const char* badgeTurb(float v) { return v <= 4.0f ? "Jernih" : v <= 8.0f ? "Sedang" : "Keruh"; }

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
    memcpy(&liveData, &pendingData, sizeof(SensorData));
    hasLiveData = true;
    // Push ke ring buffer setiap sensor
    pushChart(4, liveData.o2);
    pushChart(5, liveData.pm25);
    pushChart(6, liveData.pm10);
    pushChart(7, liveData.hcho);
    pushChart(8, liveData.voc);
    pushChart(9, liveData.humidity);
    pushChart(1, liveData.doValue);
    pushChart(2, liveData.suhuAir);
    pushChart(3, liveData.turbidity);
    // Paksa redraw halaman yang menampilkan data live
    if (currentState == HOME || currentState == CHART_DETAIL) {
      screenDrawn = false;
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
    if (touchX >= 240 && touchY >= 90 && touchY < 292) {
      currentState = HOME; screenDrawn = false; delay(200); return;
    }
    if (touchX < 240 && touchY >= 90 && touchY < 138) {
      currentState = SETTINGS; screenDrawn = false; delay(200); return;
    }
    if (touchX < 240 && touchY >= 140 && touchY < 188) {
      currentState = FAN; screenDrawn = false; delay(200); return;
    }
    if (touchX < 240 && touchY >= 190 && touchY < 238) {
      currentState = PUMP; screenDrawn = false; delay(200); return;
    }
    if (touchX < 240 && touchY >= 240 && touchY < 290) {
      currentState = LIGHT; screenDrawn = false; delay(200); return;
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

  tft.fillScreen(COLOR_BG_MENU);

  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("Menu", 20, 43, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Pilih salah satu menu", 20, 70, 2);

  drawHomeCard(14, 90, 220, 200, menuSelection == 0);
  drawSettingCard(242, 90,  232, 46, menuSelection == 1);
  drawFanCard(    242, 140, 232, 46, menuSelection == 2);
  drawPumpCard(   242, 190, 232, 46, menuSelection == 3);
  drawLightCard(  242, 240, 232, 46, menuSelection == 4);

  tft.fillRoundRect(210, 308, 60, 5, 2, COLOR_PILL);

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

void drawHomeCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, 18, COLOR_CARD_HOME);
  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 20, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, 21, COLOR_TEXT_GRAY);
  }
  int cx = x + w / 2, cy = y + 78;
  tft.fillCircle(cx, cy, 40, COLOR_TEXT_WHITE);
  drawHouseIcon(cx, cy, COLOR_CARD_HOME);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Home", cx, y + 155, 4);
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString("Halaman utama", cx, y + 175, 2);
}

void drawSettingCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_SETTINGS);
  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }
  int cx = x + h / 2, cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawSlidersIcon(cx, cy, COLOR_CARD_SETTINGS);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Setting", cx + h / 2 + 4, cy, 4);
}

void drawLightCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_HOME);
  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }
  int cx = x + h / 2, cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawBulbIcon(cx, cy, COLOR_CARD_HOME);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Light", cx + h / 2 + 4, cy, 4);
}

void drawFanCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_SETTINGS);
  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }
  int cx = x + h / 2, cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawFanIcon(cx, cy, COLOR_CARD_SETTINGS);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Fan", cx + h / 2 + 4, cy, 4);
}

// Ikon pompa udara = gelembung (dipakai kartu menu & baris kontrol)
void drawBubbleIcon(int cx, int cy, uint16_t c) {
  tft.drawCircle(cx + 4, cy + 1, 6, c);
  tft.drawCircle(cx - 6, cy + 4, 4, c);
  tft.drawCircle(cx - 1, cy - 7, 3, c);
}

void drawPumpCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_SETTINGS);
  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }
  int cx = x + h / 2, cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawBubbleIcon(cx, cy, COLOR_CARD_SETTINGS);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Pompa", cx + h / 2 + 4, cy, 4);
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

// ==================== HOME PAGE (real-time) ====================
void showHome() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    uint16_t dx = (touchX <= 480) ? (480 - touchX) : 0;

    if (touchY < 62) { currentState = MENU; screenDrawn = false; delay(200); return; }

    if (touchY >= 80 && touchY < 208) {
      int row = (touchY < 142) ? 0 : 1;
      int col = (dx < 165) ? 0 : (dx < 322) ? 1 : 2;
      const int airMap[2][3] = {{4, 5, 6}, {7, 8, 9}};
      chartSensor = airMap[row][col];
      currentState = CHART_DETAIL; screenDrawn = false; delay(200); return;
    }
    if (touchY >= 228 && touchY < 298) {
      int col = (dx < 165) ? 0 : (dx < 322) ? 1 : 2;
      chartSensor = col + 1;
      currentState = CHART_DETAIL; screenDrawn = false; delay(200); return;
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') { currentState = MENU; screenDrawn = false; delay(200); return; }
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);

  // Status bar
  tft.fillRect(0, 0, 480, 30, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 30, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 10, 8, 2);
  tft.fillRoundRect(440, 8, 30, 14, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 11, 3, 8, COLOR_TEXT_GRAY);

  // Header
  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< Sensor Readings", 10, 33, 2);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Dikelompokkan: Udara & Air", 10, 51, 1);

  // Live / No Signal pill
  uint16_t pillCol = hasLiveData ? COLOR_ACCENT_GREEN : COLOR_TEXT_GRAY;
  tft.fillRoundRect(388, 33, 82, 22, 11, pillCol);
  tft.fillCircle(401, 44, 4, COLOR_LIGHT_GREEN);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString(hasLiveData ? "Live" : "Wait", 409, 44, 1);

  tft.drawFastHLine(0, 62, 480, COLOR_BORDER_LIGHT);

  // Sensor Udara header
  tft.fillCircle(14, 71, 5, COLOR_ACCENT_GREEN);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("Sensor Udara", 24, 71, 2);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(COLOR_ACCENT_GREEN);
  tft.drawString("6 sensor", 474, 71, 1);

  // Format nilai sensor (char array)
  char v1[12], v2[12], v3[12], v4[12], v5[12], v6[12];
  fmtVal(v1, liveData.o2,       1);
  fmtVal(v2, liveData.pm25,     0);
  fmtVal(v3, liveData.pm10,     0);
  fmtVal(v4, liveData.hcho,     3);
  fmtVal(v5, liveData.voc,      0);
  fmtVal(v6, liveData.humidity, 1);

  // Baris 1 Udara (y=80)
  drawCultureCard(  8, 80, 149, 62, "O2",      v1, "%",      badgeO2(liveData.o2),   0, false);
  drawCultureCard(165, 80, 149, 62, "PM2.5",   v2, "ug/m3",  badgePM25(liveData.pm25),0, false);
  drawCultureCard(322, 80, 149, 62, "PM10",    v3, "ug/m3",  badgePM10(liveData.pm10),0, false);

  // Baris 2 Udara (y=146)
  drawCultureCard(  8,146, 149, 62, "HCHO",    v4, "ppm",    badgeHCHO(liveData.hcho),3, false);
  drawCultureCard(165,146, 149, 62, "VOC",     v5, "ppb",    badgeVOC(liveData.voc),  3, false);
  drawCultureCard(322,146, 149, 62, "Humidity",v6, "%",      badgeHum(liveData.humidity),1, false);

  // Sensor Air header
  tft.fillCircle(14, 220, 5, 0x3C1E);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("Sensor Air", 24, 220, 2);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(0x3C1E);
  tft.drawString("3 sensor", 474, 220, 1);

  char w1[12], w2[12], w3[12];
  fmtVal(w1, liveData.doValue,   1);
  fmtVal(w2, liveData.suhuAir,   1);
  fmtVal(w3, liveData.turbidity, 1);

  // Baris Air (y=230)
  drawCultureCard(  8,230, 149, 70, "DO",       w1,"mg/L",   badgeDO(liveData.doValue),  1, true);
  drawCultureCard(165,230, 149, 70, "Suhu Air", w2,"Celsius", badgeTemp(liveData.suhuAir),2, true);
  drawCultureCard(322,230, 149, 70, "Turbidity",w3,"NTU",     badgeTurb(liveData.turbidity),1, true);

  tft.fillRoundRect(210, 309, 60, 5, 2, COLOR_PILL);
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
    line1  = "alcura (Terhubung)";
    snprintf(ipLine, sizeof(ipLine), "IP: %s", WiFi.localIP().toString().c_str());
    line2  = ipLine;
  } else if (!wifiState) {
    dotCol = COLOR_TEXT_GRAY;
    line1  = "WiFi Mati";
    line2  = "Aktifkan WiFi terlebih dahulu";
  } else {
    dotCol = 0xFD20;   // orange = sedang mencoba
    line1  = "Menghubungkan ke alcura...";
    line2  = "SSID: alcura | PW: 234alcura156";
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
        WiFi.begin("alcura", "234alcura156");
      } else {
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
  tft.drawString("Kecerahan", 58, 108, 4);
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

  char name[10]; sprintf(name, "Lampu %d", idx + 1);
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
    for (int i = 0; i < 5; i++) {
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
  tft.drawString("Kontrol lampu", 36, 68, 2);

  drawBrightnessCard();

  tft.setTextDatum(TL_DATUM); tft.setTextColor(COLOR_TEXT_GRAY);
  tft.drawString("Lampu", 15, 183, 2);

  for (int i = 0; i < 5; i++) drawLampCard(i);

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
  if (screenDrawn) return;
  drawChartDetail(chartSensor);
  screenDrawn = true;
}

void drawChartDetail(int s) {
  // Metadata per sensor (title, subtitle, unit, yMin, yMax, yLabels, iconType, isWater, isDegree)
  const char *title, *subtitle, *unit;
  const char* yLabels[5];
  float yMin, yMax;
  bool trendUp, isDegree = false, isWater = false;
  uint8_t iconType;

  // Nilai real-time dari liveData untuk bigValue + stats dari chartBuf[s]
  float curVal = 0;

  switch (s) {
    case 1:
      title="DO Monitor"; subtitle="Oksigen Terlarut"; unit="mg/L";
      yMin=9; yMax=13; iconType=1; isWater=true;
      yLabels[0]="9.0"; yLabels[1]="10.0"; yLabels[2]="11.0"; yLabels[3]="12.0"; yLabels[4]="13.0";
      curVal=liveData.doValue; trendUp=true; break;
    case 2:
      title="Suhu Air Monitor"; subtitle="Temperatur Air"; unit="C";
      yMin=24; yMax=32; iconType=2; isWater=true; isDegree=true;
      yLabels[0]="24"; yLabels[1]="26"; yLabels[2]="28"; yLabels[3]="30"; yLabels[4]="32";
      curVal=liveData.suhuAir; trendUp=false; break;
    case 3:
      title="Turbidity Monitor"; subtitle="Kekeruhan Air"; unit="NTU";
      yMin=2; yMax=6; iconType=7; isWater=true;
      yLabels[0]="2.0"; yLabels[1]="3.0"; yLabels[2]="4.0"; yLabels[3]="5.0"; yLabels[4]="6.0";
      curVal=liveData.turbidity; trendUp=false; break;
    case 5:
      title="PM2.5 Monitor"; subtitle="Partikulat Halus"; unit="ug/m3";
      yMin=0; yMax=40; iconType=0;
      yLabels[0]="0"; yLabels[1]="10"; yLabels[2]="20"; yLabels[3]="30"; yLabels[4]="40";
      curVal=liveData.pm25; trendUp=false; break;
    case 6:
      title="PM10 Monitor"; subtitle="Partikulat Kasar"; unit="ug/m3";
      yMin=0; yMax=100; iconType=5;
      yLabels[0]="0"; yLabels[1]="25"; yLabels[2]="50"; yLabels[3]="75"; yLabels[4]="100";
      curVal=liveData.pm10; trendUp=true; break;
    case 7:
      title="HCHO Monitor"; subtitle="Formaldehida"; unit="ppm";
      yMin=0.01f; yMax=0.09f; iconType=3;
      yLabels[0]="0.01"; yLabels[1]="0.03"; yLabels[2]="0.05"; yLabels[3]="0.07"; yLabels[4]="0.09";
      curVal=liveData.hcho; trendUp=false; break;
    case 8:
      title="VOC Monitor"; subtitle="Senyawa Organik"; unit="ppb";
      yMin=0; yMax=300; iconType=6;
      yLabels[0]="0"; yLabels[1]="75"; yLabels[2]="150"; yLabels[3]="225"; yLabels[4]="300";
      curVal=liveData.voc; trendUp=true; break;
    case 9:
      title="Humidity Monitor"; subtitle="Kelembapan Udara"; unit="%";
      yMin=30; yMax=80; iconType=1;
      yLabels[0]="30"; yLabels[1]="42"; yLabels[2]="55"; yLabels[3]="67"; yLabels[4]="80";
      curVal=liveData.humidity; trendUp=true; break;
    default: // 4 = O2
      title="O2 Monitor"; subtitle="Oksigen"; unit="%";
      yMin=18; yMax=22; iconType=4;
      yLabels[0]="18.0"; yLabels[1]="19.0"; yLabels[2]="20.0"; yLabels[3]="21.0"; yLabels[4]="22.0";
      curVal=liveData.o2; trendUp=true; break;
  }

  // Hitung min/avg/max dari ring buffer
  float bMin = chartBuf[s][0], bMax = chartBuf[s][0], bSum = 0;
  for (int i = 0; i < CHART_POINTS; i++) {
    if (chartBuf[s][i] < bMin) bMin = chartBuf[s][i];
    if (chartBuf[s][i] > bMax) bMax = chartBuf[s][i];
    bSum += chartBuf[s][i];
  }
  trendUp = (chartBuf[s][CHART_POINTS-1] >= chartBuf[s][0]);

  char statMin[12], statAvg[12], statMax[12], bigValue[12];
  dtostrf(bMin, -1, (s==7)?3:1, statMin);
  dtostrf(bSum / CHART_POINTS, -1, (s==7)?3:1, statAvg);
  dtostrf(bMax, -1, (s==7)?3:1, statMax);
  dtostrf(curVal, -1, (s==7)?3:(isDegree?1:1), bigValue);

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

  // ===== PANEL KIRI =====
  tft.fillRect(0, 0, 184, 320, thAccent);

  // Tombol kembali
  tft.drawLine(20,19,12,26,0xFFFF); tft.drawLine(12,26,20,33,0xFFFF);
  tft.drawLine(21,19,13,26,0xFFFF); tft.drawLine(13,26,21,33,0xFFFF);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(0xFFFF);
  tft.drawString("Kembali", 30, 26, 2);

  // Icon
  tft.fillCircle(92, 92, 32, COLOR_ICON_CIRCLE);
  drawBigSensorIcon(92, 92, iconType, thAccent);

  // Judul & nilai
  tft.setTextDatum(MC_DATUM); tft.setTextColor(0xFFFF);
  tft.drawString(title, 92, 144, 4);
  tft.setTextColor(thMutedLt);
  tft.drawString(subtitle, 92, 168, 2);
  tft.setTextColor(0xFFFF);
  tft.drawString(bigValue, 92, 204, 6);

  if (isDegree) {
    int w = tft.textWidth(bigValue, 6);
    tft.drawCircle(92 + w/2 + 9, 190, 5, 0xFFFF);
    tft.drawCircle(92 + w/2 + 9, 190, 4, 0xFFFF);
  }

  // Unit
  tft.setTextColor(thMutedLt);
  if (isDegree) {
    tft.setTextDatum(ML_DATUM);
    tft.drawCircle(80, 230, 3, thMutedLt);
    tft.drawString("C", 87, 232, 2);
  } else {
    tft.setTextDatum(MC_DATUM);
    tft.drawString(unit, 92, 232, 2);
  }

  // Pill tren
  int pw = 88, ph = 24, px = 92 - pw/2, py = 252;
  tft.fillRoundRect(px, py, pw, ph, ph/2, thLight);
  int tx2 = px + 22, ty2 = py + ph/2;
  if (trendUp) tft.fillTriangle(tx2, ty2-5, tx2-5, ty2+4, tx2+5, ty2+4, thAccent);
  else         tft.fillTriangle(tx2, ty2+5, tx2-5, ty2-4, tx2+5, ty2-4, thAccent);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(thAccent);
  tft.drawString(trendStr, tx2 + 12, ty2, 2);

  // Real-time dot
  tft.fillCircle(40, 296, 4, thMutedLt);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(0xFFFF);
  tft.drawString(hasLiveData ? "Real-time" : "Demo data", 52, 296, 2);

  // ===== PANEL KANAN =====
  tft.fillRect(184, 0, 296, 320, thBgRight);
  drawRoundedCard(190, 8, 284, 248, 14, 0xFFFF);

  tft.setTextDatum(TL_DATUM); tft.setTextColor(thDark);
  tft.drawString("Tren 12 Titik Terakhir", 202, 18, 2);
  tft.setTextDatum(TR_DATUM); tft.setTextColor(thMuted);
  tft.drawString("~12 detik", 462, 18, 2);
  tft.drawFastHLine(202, 40, 260, COLOR_BORDER_LIGHT);

  // Area plot
  int pL = 232, pR = 462, pT = 56, pB = 222;

  // Gridline + label Y
  for (int i = 0; i < 5; i++) {
    int gy = pB - (pB - pT) * i / 4;
    for (int gx = pL; gx < pR; gx += 8) tft.drawFastHLine(gx, gy, 4, COLOR_BORDER_LIGHT);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(thMuted);
    tft.drawString(yLabels[i], pL - 6, gy, 1);
  }

  // Konversi data ke pixel
  int xp[CHART_POINTS], yp[CHART_POINTS];
  float rng = yMax - yMin;
  for (int i = 0; i < CHART_POINTS; i++) {
    xp[i] = pL + (pR - pL) * i / (CHART_POINTS - 1);
    float t = (rng > 0) ? constrain((chartBuf[s][i] - yMin) / rng, 0.0f, 1.0f) : 0.5f;
    yp[i] = pB - (int)(t * (pB - pT));
  }

  // Area fill
  for (int i = 0; i < CHART_POINTS - 1; i++) {
    int x0 = xp[i], x1 = xp[i + 1];
    for (int x = x0; x <= x1; x++) {
      float f = (x1 == x0) ? 0 : (float)(x - x0) / (x1 - x0);
      int y = yp[i] + (int)((yp[i + 1] - yp[i]) * f);
      if (y < pB) tft.drawFastVLine(x, y, pB - y, thFill);
    }
  }

  // Garis kurva (3px)
  for (int i = 0; i < CHART_POINTS - 1; i++) {
    tft.drawLine(xp[i], yp[i],     xp[i+1], yp[i+1],     thLine);
    tft.drawLine(xp[i], yp[i] - 1, xp[i+1], yp[i+1] - 1, thLine);
    tft.drawLine(xp[i], yp[i] + 1, xp[i+1], yp[i+1] + 1, thLine);
  }

  // Marker titik (titik terakhir lebih besar)
  for (int i = 0; i < CHART_POINTS; i++) {
    if (i == CHART_POINTS - 1) {
      tft.fillCircle(xp[i], yp[i], 7, thLight);
      tft.fillCircle(xp[i], yp[i], 4, thLine);
    } else {
      tft.fillCircle(xp[i], yp[i], 3, 0xFFFF);
      tft.drawCircle(xp[i], yp[i], 3, thLine);
    }
  }

  // Label X
  const char* xl[4] = {"T-11", "T-7", "T-4", "T-0"};
  int xi[4] = {0, 4, 8, 11};
  tft.setTextDatum(TC_DATUM); tft.setTextColor(thMuted);
  for (int k = 0; k < 4; k++) tft.drawString(xl[k], xp[xi[k]], pB + 8, 1);

  // ===== Kartu statistik =====
  int sy = 260, sh = 54;
  const char* slab[3] = {"Min", "Avg", "Max"};
  const char* sval[3] = {statMin, statAvg, statMax};
  int sx[3] = {190, 287, 384};
  for (int k = 0; k < 3; k++) {
    drawRoundedCard(sx[k], sy, 90, sh, 10, thLight);
    tft.setTextDatum(TC_DATUM); tft.setTextColor(thMuted);
    tft.drawString(slab[k], sx[k] + 45, sy + 8, 2);
    tft.setTextColor(thDark);
    tft.drawString(sval[k], sx[k] + 45, sy + 26, 4);
  }
}
