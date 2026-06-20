/*
 * ===========================================================================
 *  BOARD RELAY — Kipas + Pompa  (Project Jepang / ALCURA)
 * ===========================================================================
 *  Tugas: TERIMA perintah dari ALCURA via ESP-NOW, lalu hidup/matikan relay.
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF), channel 1.
 *               msgType = 1  -> ControlData (ALCURA -> board ini)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  RELAY 4-CHANNEL  (ACTIVE-LOW: IN=LOW -> relay ON ; IN=HIGH -> relay OFF)
 *    Kipas        -> GPIO 21   (fanState[0]  / "Kipas 1" di ALCURA)
 *    Pompa Air    -> GPIO 22   (pumpState[0] / "Pompa 1" di ALCURA)
 *    Pompa Udara  -> GPIO 17   (pumpState[1] / "Pompa 2" di ALCURA)
 *    Kipas 2      -> GPIO 16   (fanState[1]) -> CADANGAN, belum dipasang
 *
 *  Catatan: kalau ON/OFF terbalik, ubah RELAY_ACTIVE_LOW jadi false.
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>

// ===== STRUCT ESP-NOW (HARUS identik dengan ALCURA — urutan jangan diubah) =====
struct __attribute__((packed)) ControlData {
  uint8_t msgType;        // 1
  bool    lampState[5];   // dipakai board lampu (diabaikan di sini)
  uint8_t brightness;     // dipakai board lampu (diabaikan di sini)
  bool    fanState[2];    // <- board ini: kipas
  bool    pumpState[2];   // <- board ini: pompa air + pompa udara
};

// ===== KONFIG RELAY =====
#define RELAY_ACTIVE_LOW  true   // true = IN LOW menyalakan relay (modul umum)

#define PIN_KIPAS       21   // fanState[0]
#define PIN_KIPAS2      16   // fanState[1]  (cadangan, belum dipasang)
#define PIN_POMPA_AIR   22   // pumpState[0]
#define PIN_POMPA_UDARA 17   // pumpState[1]

const int relayPins[4] = { PIN_KIPAS, PIN_KIPAS2, PIN_POMPA_AIR, PIN_POMPA_UDARA };

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

// Init 1 relay TANPA "nyentak" saat boot: set OFF dulu, baru OUTPUT, lalu OFF lagi.
void initRelay(int pin) {
  relayWrite(pin, false);
  pinMode(pin, OUTPUT);
  relayWrite(pin, false);
}

// ===== Terapkan semua state ke hardware =====
void applyHardware() {
  relayWrite(PIN_KIPAS,       fanState[0]);
  relayWrite(PIN_KIPAS2,      fanState[1]);
  relayWrite(PIN_POMPA_AIR,   pumpState[0]);
  relayWrite(PIN_POMPA_UDARA, pumpState[1]);
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
  Serial.println("\n=== BOARD RELAY (Kipas + Pompa Air + Pompa Udara) - Project Jepang ===");

  // Semua relay MATI dulu (tanpa glitch saat boot)
  for (int i = 0; i < 4; i++) initRelay(relayPins[i]);

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

  Serial.printf("[CTRL] Kipas:%d (Kipas2:%d)  PompaAir:%d  PompaUdara:%d\n",
                fanState[0], fanState[1], pumpState[0], pumpState[1]);
}
