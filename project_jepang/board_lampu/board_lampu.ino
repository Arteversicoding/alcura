/*
 * ===========================================================================
 *  BOARD 3 — LAMPU  (Project Jepang / ALCURA)
 * ===========================================================================
 *  Tugas board ini: TERIMA perintah dari ALCURA via ESP-NOW, lalu atur LAMPU
 *  addressable: 5 grup lampu (on/off masing-masing) + brightness global.
 *
 *  HARDWARE LAMPU
 *    - 2x Ring WS2812B (12 LED/ring)  -> daya 5V dari ESP32   -> data GPIO 4
 *    - 4x Strip addressable (30 LED)  -> daya 12V dari adaptor -> data GPIO 5
 *    Ring & strip masing-masing di-CHAIN (DO -> DI sambung ke berikutnya).
 *
 *  Komunikasi : ESP-NOW broadcast (FF:FF:FF:FF:FF:FF), channel 1.
 *               msgType = 1  -> ControlData (ALCURA -> board ini)
 *
 *  Board      : ESP32 DEVKIT V1 (WROOM, 30 pin)
 *
 *  WIRING DATA (chaining)
 *    Ring : GPIO 4 -> Ring1.DI ; Ring1.DO -> Ring2.DI
 *    Strip: GPIO 5 -> Strip1.DI; Strip1.DO -> Strip2.DI -> ... -> Strip4.DI
 *
 *  WIRING DAYA
 *    Ring  : 5V & GND dari ESP32 (kalau 2 ring full putih terasa redup/restart,
 *            ambil 5V dari adaptor 5V terpisah).
 *    Strip : 12V & GND dari ADAPTOR 12V. GND adaptor WAJIB disatukan dgn GND ESP32.
 *
 *  Total LED dibagi rata jadi 5 grup mengikuti lampState[0..4] dari ALCURA.
 * ===========================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

// ===== STRUCT ESP-NOW (HARUS identik dengan ALCURA — urutan jangan diubah) =====
struct __attribute__((packed)) ControlData {
  uint8_t msgType;        // 1
  bool    lampState[5];   // <- board ini: 5 grup lampu
  uint8_t brightness;     // <- board ini: 0-100
  bool    fanState[2];    // dipakai board relay (diabaikan di sini)
  bool    pumpState[2];   // dipakai board relay (diabaikan di sini)
};

// ===== KONFIG LED =====
// Ring WS2812B (5V) di GPIO 4
#define RING_PIN          4
#define RING_COUNT_EACH   12
#define RING_NUM          2
#define RING_TOTAL        (RING_COUNT_EACH * RING_NUM)    // 24

// Strip addressable 12V di GPIO 5
#define STRIP_PIN         5
#define STRIP_COUNT_EACH  30
#define STRIP_NUM         4
#define STRIP_TOTAL       (STRIP_COUNT_EACH * STRIP_NUM)  // 120

#define TOTAL_LEDS        (RING_TOTAL + STRIP_TOTAL)       // 144
#define NUM_LAMPS         5

// Ring = urutan warna GRB. Strip Krisbow = RBG (dikoreksi dari pengetesan).
// Kalau warna strip salah (mis. minta merah jadi hijau), ganti NEO_RBG -> NEO_RGB/NEO_GRB.
// Kalau strip kedip/ngaco, ganti NEO_KHZ800 -> NEO_KHZ400.
Adafruit_NeoPixel ring(RING_TOTAL,  RING_PIN,  NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip(STRIP_TOTAL, STRIP_PIN, NEO_RBG + NEO_KHZ800);

// Warna saat lampu menyala = PUTIH. Format R,G,B.
// (Mau putih hangat? ganti jadi 255,230,180. Mau warna lain? ubah di sini.)
#define LAMP_R  255
#define LAMP_G  255
#define LAMP_B  255

// ===== STATE (diupdate saat terima ControlData) =====
volatile bool    lampState[NUM_LAMPS] = { false, false, false, false, false };
volatile uint8_t brightness = 50;          // 0-100
volatile bool    ctrlPending = false;
ControlData      ctrlBuf;

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool espReady = false;

// Batas LED (indeks logis 0..TOTAL_LEDS) untuk tiap grup lampu, dibagi rata.
int lampStart(int g) { return (g * TOTAL_LEDS) / NUM_LAMPS; }
int lampEnd(int g)   { return ((g + 1) * TOTAL_LEDS) / NUM_LAMPS; }

// Set 1 pixel berdasarkan indeks logis: <RING_TOTAL -> ring, sisanya -> strip.
// Tiap objek punya urutan warna sendiri, jadi panggil .Color() objek bersangkutan.
void setLogical(int i, bool on) {
  if (i < RING_TOTAL) {
    ring.setPixelColor(i, on ? ring.Color(LAMP_R, LAMP_G, LAMP_B) : 0);
  } else {
    strip.setPixelColor(i - RING_TOTAL, on ? strip.Color(LAMP_R, LAMP_G, LAMP_B) : 0);
  }
}

// ===== Terapkan state ke semua LED =====
void applyLamps() {
  uint8_t b = (uint8_t)((brightness * 255UL) / 100);
  ring.setBrightness(b);
  strip.setBrightness(b);
  for (int g = 0; g < NUM_LAMPS; g++) {
    for (int i = lampStart(g); i < lampEnd(g); i++) setLogical(i, lampState[g]);
  }
  ring.show();
  strip.show();
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
    Serial.printf("MAC lampu: %s\n", WiFi.macAddress().c_str());
  }
}

// Animasi singkat saat start: 1 titik berjalan menyapu ring lalu strip.
// Kalau sapuan ini terlihat, berarti LED + wiring sudah benar.
void selfTest() {
  ring.setBrightness(60);
  strip.setBrightness(60);
  for (int i = 0; i < TOTAL_LEDS; i++) {
    ring.clear();
    strip.clear();
    setLogical(i, true);
    ring.show();
    strip.show();
    delay(15);
  }
  ring.clear();  ring.show();
  strip.clear(); strip.show();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== BOARD LAMPU (2 ring + 4 strip, 5 grup) - Project Jepang ===");
  Serial.printf("Total LED: %d (ring %d + strip %d)\n", TOTAL_LEDS, RING_TOTAL, STRIP_TOTAL);

  ring.begin();  ring.clear();  ring.show();
  strip.begin(); strip.clear(); strip.show();
  selfTest();

  espNowInit();
  applyLamps();   // mulai dari semua mati
  Serial.println("Menunggu perintah dari ALCURA...");
}

void loop() {
  if (!ctrlPending) return;
  ctrlPending = false;

  for (int g = 0; g < NUM_LAMPS; g++) lampState[g] = ctrlBuf.lampState[g];
  brightness = ctrlBuf.brightness;

  applyLamps();

  Serial.printf("[CTRL] Lampu:%d%d%d%d%d  Bright:%d%%\n",
                lampState[0], lampState[1], lampState[2],
                lampState[3], lampState[4], brightness);
}
