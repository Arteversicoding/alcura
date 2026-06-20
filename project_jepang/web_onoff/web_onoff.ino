/*
 * PROJECT JEPANG - Kontrol ON/OFF via Website (WiFi AP)
 * ESP32 + WS2812B Ring (GPIO 4) + Krisbow strip addressable (GPIO 5)
 *
 * ESP32 bikin hotspot sendiri:
 *   WiFi    : ProjectJepang
 *   Password: 12345678
 *   Buka    : http://192.168.4.1
 *
 * Ada tombol ON / OFF di halaman. ON = kedua lampu nyala putih, OFF = mati.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// ===== LED =====
#define RING_PIN     4
#define RING_COUNT   12
#define STRIP_PIN    5
#define STRIP_COUNT  30
#define BRIGHTNESS   60

// Ring urutan GRB, strip Krisbow urutan RBG (sudah dikoreksi)
Adafruit_NeoPixel ring(RING_COUNT, RING_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip(STRIP_COUNT, STRIP_PIN, NEO_RBG + NEO_KHZ800);

// ===== WiFi AP =====
const char* AP_SSID = "ProjectJepang";
const char* AP_PASS = "12345678";

WebServer server(80);
bool lampuOn = false;

void terapkanLampu() {
  uint8_t v = lampuOn ? 255 : 0;
  for (int i = 0; i < ring.numPixels(); i++)  ring.setPixelColor(i, ring.Color(v, v, v));
  for (int i = 0; i < strip.numPixels(); i++) strip.setPixelColor(i, strip.Color(v, v, v));
  ring.show();
  strip.show();
}

String halaman() {
  String warna = lampuOn ? "#22c55e" : "#9ca3af";
  String status = lampuOn ? "NYALA" : "MATI";
  String html =
    "<!DOCTYPE html><html lang='id'><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Project Jepang - Kontrol Lampu</title><style>"
    "body{font-family:sans-serif;text-align:center;background:#0f172a;color:#fff;margin:0;padding:40px 16px}"
    "h1{font-size:1.5rem}.status{font-size:2rem;font-weight:bold;margin:20px;color:" + warna + "}"
    "a{display:block;max-width:320px;margin:16px auto;padding:24px;border-radius:16px;"
    "text-decoration:none;color:#fff;font-size:1.6rem;font-weight:bold}"
    ".on{background:#16a34a}.off{background:#dc2626}.sub{color:#94a3b8;font-size:.9rem}"
    "</style></head><body>"
    "<h1>💡 Kontrol Lampu - Project Jepang</h1>"
    "<div class='status'>Status: " + status + "</div>"
    "<a class='on' href='/on'>NYALAKAN</a>"
    "<a class='off' href='/off'>MATIKAN</a>"
    "<p class='sub'>Ring WS2812B + Strip Krisbow</p>"
    "</body></html>";
  return html;
}

void handleRoot() { server.send(200, "text/html", halaman()); }
void handleOn()   { lampuOn = true;  terapkanLampu(); server.send(200, "text/html", halaman()); }
void handleOff()  { lampuOn = false; terapkanLampu(); server.send(200, "text/html", halaman()); }

void setup() {
  Serial.begin(115200);
  delay(300);

  ring.begin();  ring.setBrightness(BRIGHTNESS);  ring.clear();  ring.show();
  strip.begin(); strip.setBrightness(BRIGHTNESS); strip.clear(); strip.show();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("\n=== Web Kontrol Lampu siap ===");
  Serial.print("WiFi  : "); Serial.println(AP_SSID);
  Serial.print("Buka  : http://"); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.begin();
}

void loop() {
  server.handleClient();
}
