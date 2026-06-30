/*
 * ===========================================================================
 *  BOARD LED — NeoPixel (4 Strip)
 *  Project Jepang / ALCURA
 * ===========================================================================
 *  Tugas: TERIMA perintah dari ALCURA via ESP-NOW, lalu:
 *    - hidup/matikan 4 strip lampu NeoPixel
 *    - atur brightness global (0-100 dari ALCURA)
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF)
 *               msgType = 1  -> ControlData (ALCURA -> board ini)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  PIN LAMPU:
 *    Strip 1 (30 LED WS2811 12V) -> GPIO 19   (lampState[0])
 *    Strip 2 (30 LED WS2811 12V) -> GPIO 21   (lampState[1])
 *    Strip 3 (30 LED WS2811 12V) -> GPIO 23   (lampState[2])
 *    Strip 4 (30 LED WS2811 12V) -> GPIO 24   (lampState[3])
 *
 *  WIRING WAJIB:
 *    - GND ESP32 dan adaptor strip 12V HARUS disatukan.
 *    - V+ strip dari adaptor 12V sendiri (JANGAN dari pin ESP32).
 *
 *  LIBRARY: "Adafruit NeoPixel"
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>

const char* WIFI_SSID = "Alcura";
const char* WIFI_PASS = "234alcura156";

// ===== STRUCT ESP-NOW — IDENTIK dengan ALCURA, jangan diubah =====
struct __attribute__((packed)) ControlData {
  uint8_t msgType;
  bool    lampState[4];
  uint8_t brightness;
  bool    fanState[2];
  bool    pumpState[2];
};

// ===== KONFIG LAMPU =====
#define NUM_LAMP 4
const int lampPins[NUM_LAMP] = { 19, 21, 23, 24 };

// Strip = WS2811 12V 400kHz RBG (kalau strip mati semua ganti NEO_KHZ800)
Adafruit_NeoPixel lamps[NUM_LAMP] = {
  Adafruit_NeoPixel(30, 19, NEO_RBG + NEO_KHZ400),
  Adafruit_NeoPixel(30, 21, NEO_RBG + NEO_KHZ400),
  Adafruit_NeoPixel(30, 23, NEO_RBG + NEO_KHZ400),
  Adafruit_NeoPixel(30, 24, NEO_RBG + NEO_KHZ400)
};

// Warna warm white saat ON (kurangi G/B untuk lebih kuning, tambah untuk lebih putih)
#define LAMP_R 255
#define LAMP_G 200
#define LAMP_B  60

// ===== STATE =====
volatile bool    lampState[NUM_LAMP] = { false, false, false, false };
volatile uint8_t brightness          = 50;
volatile bool    ctrlPending         = false;
ControlData      ctrlBuf;

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool espReady = false;

// ===== LAMPU =====
void applyLamp(int i) {
  uint8_t b = (uint8_t)((brightness * 255UL) / 100);
  lamps[i].setBrightness(b);
  uint32_t c = lampState[i] ? lamps[i].Color(LAMP_R, LAMP_G, LAMP_B) : 0;
  for (int p = 0; p < lamps[i].numPixels(); p++) lamps[i].setPixelColor(p, c);
  lamps[i].show();
}

void applyAllLamps() {
  for (int i = 0; i < NUM_LAMP; i++) applyLamp(i);
}

void selfTest() {
  for (int i = 0; i < NUM_LAMP; i++) {
    lamps[i].setBrightness(60);
    uint32_t c = lamps[i].Color(LAMP_R, LAMP_G, LAMP_B);
    for (int p = 0; p < lamps[i].numPixels(); p++) lamps[i].setPixelColor(p, c);
    lamps[i].show();
    delay(150);
    lamps[i].clear();
    lamps[i].show();
  }
}

// ===== ESP-NOW =====
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
#else
void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len != (int)sizeof(ControlData) || data[0] != 1) return;
  memcpy(&ctrlBuf, data, sizeof(ControlData));
  ctrlPending = true;
}

void espNowInit() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  Serial.printf("Konek hotspot %s...\n", WIFI_SSID);

  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW GAGAL!"); return; }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 0;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.printf("ESP-NOW siap | MAC: %s\n", WiFi.macAddress().c_str());
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BOARD LED (4 Strip) ===");

  for (int i = 0; i < NUM_LAMP; i++) { lamps[i].begin(); lamps[i].clear(); lamps[i].show(); }
  selfTest();

  espNowInit();
  applyAllLamps();
  Serial.println("Menunggu perintah dari ALCURA...");
}

// ===== LOOP =====
void loop() {
  // Self-heal: refresh tiap 800ms untuk perbaiki pixel noise di jalur data
  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh >= 800UL) {
    lastRefresh = millis();
    applyAllLamps();
  }

  if (!ctrlPending) return;
  ctrlPending = false;

  static bool        firstCtrl = true;
  static ControlData applied;
  if (!firstCtrl && memcmp(&applied, &ctrlBuf, sizeof(ControlData)) == 0) return;
  firstCtrl = false;
  memcpy(&applied, &ctrlBuf, sizeof(ControlData));

  for (int i = 0; i < NUM_LAMP; i++) lampState[i] = ctrlBuf.lampState[i];
  brightness = ctrlBuf.brightness;

  applyAllLamps();

  Serial.printf("[LED] S1:%d S2:%d S3:%d S4:%d | Bright:%d%%\n",
    lampState[0], lampState[1], lampState[2], lampState[3], brightness);
}
