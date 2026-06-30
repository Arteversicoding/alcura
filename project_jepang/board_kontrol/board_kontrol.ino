/*
 * ===========================================================================
 *  BOARD KONTROL — Relay (Kipas + Pompa Udara)
 *  Project Jepang / ALCURA
 * ===========================================================================
 *  Tugas: TERIMA perintah dari ALCURA via ESP-NOW, lalu:
 *    - hidup/matikan 4 relay (2 kipas + 2 pompa udara)
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF)
 *               msgType = 1  -> ControlData (ALCURA -> board ini)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  PIN RELAY (ACTIVE-LOW: IN=LOW -> relay ON ; IN=HIGH -> relay OFF)
 *    Kipas 1        -> GPIO 16   (fanState[0])
 *    Kipas 2        -> GPIO 22   (fanState[1])
 *    Pompa Udara 1  -> GPIO 21   (pumpState[0])
 *    Pompa Udara 2  -> GPIO 17   (pumpState[1])
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

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

// ===== RELAY CONFIG =====
#define RELAY_ACTIVE_LOW  true

#define PIN_KIPAS1       16
#define PIN_KIPAS2       22
#define PIN_POMPA_UDARA1 21
#define PIN_POMPA_UDARA2 17

const int relayPins[4] = { PIN_KIPAS1, PIN_KIPAS2, PIN_POMPA_UDARA1, PIN_POMPA_UDARA2 };

// ===== STATE =====
volatile bool fanState[2]  = { false, false };
volatile bool pumpState[2] = { false, false };
volatile bool ctrlPending  = false;
ControlData   ctrlBuf;

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool espReady = false;

// ===== RELAY =====
void relayWrite(int pin, bool on) {
  digitalWrite(pin, (RELAY_ACTIVE_LOW ? !on : on) ? HIGH : LOW);
}

void initRelay(int pin) {
  relayWrite(pin, false);
  pinMode(pin, OUTPUT);
  relayWrite(pin, false);
}

void applyRelays() {
  relayWrite(PIN_KIPAS1,       fanState[0]);
  relayWrite(PIN_KIPAS2,       fanState[1]);
  relayWrite(PIN_POMPA_UDARA1, pumpState[0]);
  relayWrite(PIN_POMPA_UDARA2, pumpState[1]);
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
  Serial.println("\n=== BOARD KONTROL (Kipas + Pompa Udara) ===");
  for (int i = 0; i < 4; i++) initRelay(relayPins[i]);
  espNowInit();
  Serial.println("Menunggu perintah dari ALCURA...");
}

// ===== LOOP =====
void loop() {
  if (!ctrlPending) return;
  ctrlPending = false;

  static bool        firstCtrl = true;
  static ControlData applied;
  if (!firstCtrl && memcmp(&applied, &ctrlBuf, sizeof(ControlData)) == 0) return;
  firstCtrl = false;
  memcpy(&applied, &ctrlBuf, sizeof(ControlData));

  fanState[0]  = ctrlBuf.fanState[0];
  fanState[1]  = ctrlBuf.fanState[1];
  pumpState[0] = ctrlBuf.pumpState[0];
  pumpState[1] = ctrlBuf.pumpState[1];

  applyRelays();

  Serial.printf("[CTRL] Kipas:%d%d | PompaUdara:%d%d\n",
    fanState[0], fanState[1], pumpState[0], pumpState[1]);
}
