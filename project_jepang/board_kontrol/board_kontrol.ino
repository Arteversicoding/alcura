/*
 * ===========================================================================
 *  BOARD KONTROL — Relay (Kipas + Pompa) + Lampu (Ring + Strip)  [GABUNGAN]
 *  Project Jepang / ALCURA
 * ===========================================================================
 *  Gabungan dari board_relay + board_lampu menjadi SATU ESP32.
 *  Tugas: TERIMA perintah dari ALCURA via ESP-NOW, lalu:
 *    - hidup/matikan 4 relay (2 kipas + 2 pompa udara)
 *    - hidup/matikan 5 lampu addressable (1 ring + 4 strip) + atur brightness
 *
 *  Status on/off (kipas/pompa/lampu + brightness) DITULIS KE FIREBASE oleh ALCURA
 *  (node /control.json). Board ini cukup menerima perintah & menggerakkan hardware,
 *  sama persis pola board_sensor (login hotspot "Alcura" -> se-channel ESP-NOW).
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF).
 *               msgType = 1  -> ControlData (ALCURA -> board ini)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  ---------------------------------------------------------------------------
 *  PIN RELAY (ACTIVE-LOW: IN=LOW -> relay ON ; IN=HIGH -> relay OFF)
 *    Kipas 1        -> GPIO 16   (fanState[0])
 *    Kipas 2        -> GPIO 22   (fanState[1])
 *    Pompa Udara 1  -> GPIO 21   (pumpState[0])
 *    Pompa Udara 2  -> GPIO 17   (pumpState[1])
 *
 *  PIN LAMPU (5 output addressable terpisah — sesuai sketch web yang sudah dites)
 *    Ring   (12 LED) -> GPIO 4    (lampState[0])
 *    Strip 1 (30)    -> GPIO 18   (lampState[1])
 *    Strip 2 (30)    -> GPIO 19   (lampState[2])
 *    Strip 3 (30)    -> GPIO 23   (lampState[3])
 *    Strip 4 (30)    -> GPIO 25   (lampState[4])
 *  brightness (0-100 dari ALCURA) = kecerahan GLOBAL semua lampu.
 *
 *  WIRING WAJIB
 *    - Semua GND (modul relay + adaptor LED + ESP32) DISATUKAN (common ground).
 *    - V+ strip dari adaptor 12V sendiri, JANGAN dari pin ESP32.
 *    - Ring 5V boleh dari ESP32 (kalau redup/restart, pakai adaptor 5V terpisah).
 *    - Beban kipas/pompa lewat terminal COM & NO relay + sumber dayanya sendiri.
 *
 *  LIBRARY: "Adafruit NeoPixel"
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>

// Semua board login hotspot SAMA -> otomatis se-channel (tak perlu atur channel).
const char* WIFI_SSID = "Alcura";        // <- nama hotspot HP (samakan persis)
const char* WIFI_PASS = "234alcura156";  // <- password hotspot HP

// ===== STRUCT ESP-NOW (HARUS identik dengan ALCURA — urutan jangan diubah) =====
struct __attribute__((packed)) ControlData {
  uint8_t msgType;        // 1
  bool    lampState[5];   // <- lampu: 0=Ring, 1..4=Strip1..4
  uint8_t brightness;     // <- lampu: 0-100 (global)
  bool    fanState[2];    // <- relay: kipas
  bool    pumpState[2];   // <- relay: pompa udara
};

// ===== KONFIG RELAY =====
#define RELAY_ACTIVE_LOW  true   // true = IN LOW menyalakan relay (modul umum)

#define PIN_KIPAS1       16   // fanState[0]  -> Kipas 1
#define PIN_KIPAS2       22   // fanState[1]  -> Kipas 2
#define PIN_POMPA_UDARA1 21   // pumpState[0] -> Pompa Udara 1
#define PIN_POMPA_UDARA2 17   // pumpState[1] -> Pompa Udara 2

const int relayPins[4] = { PIN_KIPAS1, PIN_KIPAS2, PIN_POMPA_UDARA1, PIN_POMPA_UDARA2 };

// ===== KONFIG LAMPU (5 fixture terpisah, 1 ring + 4 strip) =====
#define NUM_LAMP 5
const int   lampPins[NUM_LAMP]   = { 4, 18, 19, 23, 25 };
const int   lampCounts[NUM_LAMP] = { 12, 30, 30, 30, 30 };
const char* lampNames[NUM_LAMP]  = { "Ring", "Strip 1", "Strip 2", "Strip 3", "Strip 4" };

// Urutan warna: Ring WS2812B = GRB (5V, 800kHz, aman).
// STRIP 12V biasanya WS2811 @ 400kHz -> pakai NEO_KHZ400 biar data tidak desinkron
// (gejala desinkron: putih jadi warna-warni, makin parah pas brightness diturunkan).
// Kalau dgn 400kHz strip malah MATI semua -> berarti strip-mu 800kHz, balikin ke NEO_KHZ800.
// Urutan warna tidak ngaruh ke PUTIH (R=G=B); kalau nanti pakai warna & salah, ganti NEO_RBG.
Adafruit_NeoPixel lamps[NUM_LAMP] = {
  Adafruit_NeoPixel(12, 4,  NEO_GRB + NEO_KHZ800),   // Ring  (5V WS2812B)
  Adafruit_NeoPixel(30, 18, NEO_RBG + NEO_KHZ400),   // Strip 1 (12V WS2811)
  Adafruit_NeoPixel(30, 19, NEO_RBG + NEO_KHZ400),   // Strip 2
  Adafruit_NeoPixel(30, 23, NEO_RBG + NEO_KHZ400),   // Strip 3 (pin DI dilaporkan kobong)
  Adafruit_NeoPixel(30, 25, NEO_RBG + NEO_KHZ400)    // Strip 4
};

// Warna saat lampu menyala = PUTIH (default project). Ubah di sini bila mau warna lain.
#define LAMP_R  255
#define LAMP_G  255
#define LAMP_B  255

// ===== STATE (diupdate saat terima ControlData) =====
volatile bool    fanState[2]        = { false, false };
volatile bool    pumpState[2]       = { false, false };
volatile bool    lampState[NUM_LAMP]= { false, false, false, false, false };
volatile uint8_t brightness         = 50;     // 0-100 (kiri=redup, kanan=terang)
volatile bool    ctrlPending        = false;
ControlData      ctrlBuf;

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool espReady = false;

// ================================================================
//  RELAY
// ================================================================
void relayWrite(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;   // active-low -> balik logika
  digitalWrite(pin, level ? HIGH : LOW);
}

// Init 1 relay TANPA "nyentak" saat boot: OFF dulu, baru OUTPUT, lalu OFF lagi.
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

// ================================================================
//  LAMPU
// ================================================================
// brightness 0-100 (dari ALCURA) -> 0-255 (NeoPixel). Kiri=redup, kanan=terang.
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

// Sapuan singkat saat start: tiap fixture nyala putih sebentar (cek wiring).
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

// ================================================================
//  ESP-NOW
// ================================================================
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
  // Login hotspot "Alcura" -> radio board ini ikut channel hotspot, sama dgn ALCURA.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  // MATIKAN WiFi modem-sleep: kalau radio "tidur" antar beacon, paket ESP-NOW
  // sering ke-drop -> toggle kadang gagal. WIFI_PS_NONE = RX ESP-NOW selalu aktif.
  WiFi.setSleep(false);
  Serial.printf("Konek hotspot %s ...\n", WIFI_SSID);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW GAGAL init!");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 0;            // 0 = ikut channel WiFi sekarang
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espReady = true;
    Serial.println("ESP-NOW siap -> dengar broadcast (ikut channel hotspot)");
    Serial.printf("MAC kontrol: %s\n", WiFi.macAddress().c_str());
  }
}

// ================================================================
//  SETUP / LOOP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BOARD KONTROL (Relay + Lampu) - Project Jepang ===");

  // Relay MATI dulu (tanpa glitch saat boot)
  for (int i = 0; i < 4; i++) initRelay(relayPins[i]);

  // Lampu: init, mulai mati, lalu self-test cek wiring
  for (int i = 0; i < NUM_LAMP; i++) { lamps[i].begin(); lamps[i].clear(); lamps[i].show(); }
  selfTest();

  espNowInit();
  applyAllLamps();   // mulai dari semua mati
  Serial.println("Menunggu perintah dari ALCURA...");
}

void loop() {
  // SELF-HEAL lampu: tulis ulang state ke semua strip tiap ~800ms. Kalau ada pixel
  // yang sempat salah warna / nyala sendiri karena NOISE di jalur data (level 3.3V,
  // ground kurang solid, kabel data panjang), refresh ini mengembalikannya ke benar
  // dalam <1 dtk. (Pakai RMT jadi nilai sama tak bikin kedip.)
  static unsigned long lastLampRefresh = 0;
  if (millis() - lastLampRefresh >= 800UL) {
    lastLampRefresh = millis();
    applyAllLamps();
  }

  if (!ctrlPending) return;
  ctrlPending = false;

  // ALCURA mengirim ulang state tiap 1 dtk (heartbeat) agar tetap sinkron walau ada
  // paket drop. Kalau isinya SAMA dgn yang terakhir diterapkan -> abaikan (tak perlu
  // refresh relay/LED tiap detik). Hanya proses kalau benar-benar berubah.
  static bool        firstCtrl = true;
  static ControlData applied;
  if (!firstCtrl && memcmp(&applied, &ctrlBuf, sizeof(ControlData)) == 0) return;
  firstCtrl = false;
  memcpy(&applied, &ctrlBuf, sizeof(ControlData));

  // Salin perintah terbaru
  fanState[0]  = ctrlBuf.fanState[0];
  fanState[1]  = ctrlBuf.fanState[1];
  pumpState[0] = ctrlBuf.pumpState[0];
  pumpState[1] = ctrlBuf.pumpState[1];
  for (int g = 0; g < NUM_LAMP; g++) lampState[g] = ctrlBuf.lampState[g];
  brightness = ctrlBuf.brightness;

  applyRelays();
  applyAllLamps();

  Serial.printf("[CTRL] Kipas:%d%d Pompa:%d%d | Lampu(R,S1-4):%d%d%d%d%d Bright:%d%%\n",
                fanState[0], fanState[1], pumpState[0], pumpState[1],
                lampState[0], lampState[1], lampState[2], lampState[3], lampState[4],
                brightness);
}
