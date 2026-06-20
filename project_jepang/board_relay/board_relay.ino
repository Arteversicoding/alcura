/*
 * ===========================================================================
 *  BOARD 2 — RELAY  (Project Jepang / ALCURA)
 * ===========================================================================
 *  Tugas board ini: TERIMA perintah dari ALCURA via ESP-NOW, lalu hidup/matikan
 *  2 KIPAS + 2 POMPA UDARA lewat modul RELAY 4-CHANNEL.
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF), channel 1.
 *               msgType = 1  -> ControlData (ALCURA -> board ini)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  WIRING RELAY 4-CHANNEL
 *    IN1 -> GPIO 19  (Kipas 1)        VCC modul -> 5V (Vin)
 *    IN2 -> GPIO 23  (Kipas 2)        GND modul -> GND ESP32
 *    IN3 -> GPIO 18  (Pompa udara 1)
 *    IN4 -> GPIO 21  (Pompa udara 2)
 *
 *  PENTING - polaritas relay:
 *    Mayoritas modul relay murah bersifat ACTIVE-LOW (IN diberi LOW = relay ON).
 *    Kode ini default RELAY_ACTIVE_LOW = true. Kalau ternyata logikanya kebalik
 *    (saat "OFF" malah nyala), cukup ubah jadi false di bawah.
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>

// ===== STRUCT ESP-NOW (HARUS identik dengan ALCURA — urutan jangan diubah) =====
struct __attribute__((packed)) ControlData {
  uint8_t msgType;        // 1
  bool    lampState[5];   // dipakai board lampu (diabaikan di sini)
  uint8_t brightness;     // dipakai board lampu (diabaikan di sini)
  bool    fanState[2];    // <- board ini: 2 kipas
  bool    pumpState[2];   // <- board ini: 2 pompa udara
};

// ===== KONFIG RELAY =====
#define RELAY_ACTIVE_LOW  true   // true = IN LOW menyalakan relay (modul umum)

#define PIN_FAN1   19
#define PIN_FAN2   23
#define PIN_PUMP1  18
#define PIN_PUMP2  21

const int relayPins[4] = { PIN_FAN1, PIN_FAN2, PIN_PUMP1, PIN_PUMP2 };

// ===== STATE (diupdate saat terima ControlData) =====
volatile bool fanState[2]  = { false, false };
volatile bool pumpState[2] = { false, false };
volatile bool ctrlPending  = false;
ControlData   ctrlBuf;

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool espReady = false;

// ===== Tulis 1 relay sesuai polaritas =====
void relayWrite(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;   // active-low -> balik logika
  digitalWrite(pin, level ? HIGH : LOW);
}

// ===== Terapkan semua state ke hardware =====
void applyHardware() {
  relayWrite(PIN_FAN1,  fanState[0]);
  relayWrite(PIN_FAN2,  fanState[1]);
  relayWrite(PIN_PUMP1, pumpState[0]);
  relayWrite(PIN_PUMP2, pumpState[1]);
}

// ===== ESP-NOW callback: terima ControlData =====
// Signature beda antara core ESP32 v2.x dan v3.x -> dijaga dengan guard versi.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
#else
void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len != (int)sizeof(ControlData) || data[0] != 1) return;  // bukan untuk kita
  memcpy(&ctrlBuf, data, sizeof(ControlData));
  ctrlPending = true;
}

void espNowInit() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW GAGAL init!");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 1;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap -> dengar broadcast channel 1");
    Serial.printf("MAC relay: %s\n", WiFi.macAddress().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BOARD RELAY (2 kipas + 2 pompa) - Project Jepang ===");

  // Set ke OFF SEBELUM jadi OUTPUT supaya relay tidak "nyentak" saat boot.
  for (int i = 0; i < 4; i++) {
    relayWrite(relayPins[i], false);
    pinMode(relayPins[i], OUTPUT);
    relayWrite(relayPins[i], false);
  }

  espNowInit();
  Serial.println("Menunggu perintah dari ALCURA...");
}

void loop() {
  if (!ctrlPending) return;
  ctrlPending = false;

  fanState[0]  = ctrlBuf.fanState[0];
  fanState[1]  = ctrlBuf.fanState[1];
  pumpState[0] = ctrlBuf.pumpState[0];
  pumpState[1] = ctrlBuf.pumpState[1];

  applyHardware();

  Serial.printf("[CTRL] Kipas:%d%d  Pompa:%d%d\n",
                fanState[0], fanState[1], pumpState[0], pumpState[1]);
}
