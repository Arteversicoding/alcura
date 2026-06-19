#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();

// Touch variables
uint16_t touchX = 0, touchY = 0;
bool touchPressed = false;

// Touch calibration values (non-const for setTouch)
uint16_t calData[5] = {275, 3620, 264, 3532, 1};  // For XPT2046

// States
enum AppState {
  SPLASH_SCREEN,
  MENU,
  HOME,
  SETTINGS,
  INFO,
  ABOUT
};

AppState currentState = SPLASH_SCREEN;
AppState previousState = SPLASH_SCREEN;
unsigned long splashStartTime = 0;
int menuSelection = 0;
int previousMenuSelection = -1;
bool screenDrawn = false;

// ==================== PALET WARNA ALCURA (diambil dari alcura.gif) ====================
// Background utama: navy sangat gelap ~#0A0F1E
// Aksen utama: cyan terang ~#00C8C8
// Aksen sekunder: teal ~#00C8AE
// Subtle glow: biru-teal medium ~#1A6A78
// Teks terang: putih kebiruan ~#E0F0F0
// Teks redup: abu kebiruan ~#7A8A95
#define COLOR_BG             0x0863   // #0A0F1E - background gelap
#define COLOR_BG_LIGHTER     0x08D5   // #0E1B2E - sedikit lebih terang untuk gradient
#define COLOR_GLOW_CENTER    0x1AD5   // ~#1A6A78 - pusat glow
#define COLOR_GLOW_RING      0x116B   // ~#12615A - cincin glow
#define COLOR_ACCENT_CYAN    0x0659   // #00C8C8 - cyan utama (text ALCURA)
#define COLOR_ACCENT_TEAL    0x0655   // #00C8AE - teal
#define COLOR_ACCENT_SOFT    0x23BD   // ~#88E8E0 - cyan lembut
#define COLOR_TEXT_LIGHT     0xC638   // ~#C8D8DD - teks terang
#define COLOR_TEXT_MUTED     0x4A69   // ~#7A8A95 - teks redup
#define COLOR_TEXT_WHITE     0xFFFF   // putih untuk kontras
#define COLOR_CARD_HOME      0x0659   // cyan
#define COLOR_CARD_SETTINGS  0x0655   // teal
#define COLOR_CARD_INFO      0x0559   // biru cyan sedikit lebih gelap
#define COLOR_CARD_ABOUT     0xD4A0   // gold (aksen hangat, satu-satunya non-cyan)
#define COLOR_BORDER_SELECT  0x07FF   // cyan sangat terang untuk border selected
#define COLOR_PILL           0x294A   // pill samar di bawah
#define COLOR_BATTERY_FILL   0x0655   // teal

void setup(void) {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ACURA Display System (480x320) WITH TOUCH ===");

  Serial.println("Setting up backlight...");
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  delay(100);

  Serial.println("Initializing TFT...");
  tft.init();
  tft.setRotation(1);
  tft.setTouch(calData);  // Set touch calibration
  tft.fillScreen(COLOR_BG);

  Serial.println("Touch enabled!");
  Serial.println("Setup complete!");
  splashStartTime = millis();
}

void loop() {
  if (currentState != previousState) {
    tft.fillScreen(COLOR_BG);
    previousState = currentState;
    screenDrawn = false;
    previousMenuSelection = -1;
  }

  switch(currentState) {
    case SPLASH_SCREEN: showSplashScreen(); break;
    case MENU: showMainMenu(); break;
    case HOME: showHome(); break;
    case SETTINGS: showSettings(); break;
    case INFO: showInfo(); break;
    case ABOUT: showAbout(); break;
  }
  delay(50);
}

// ==================== SPLASH SCREEN - ALCURA (palet dari alcura.gif) ====================
void showSplashScreen() {
  unsigned long elapsedTime = millis() - splashStartTime;
  int splashDuration = 3500;
  float progress = min((float)elapsedTime / splashDuration, 1.0f);

  static bool splashDrawn = false;
  if (!splashDrawn) {
    // Background utama: dark navy
    tft.fillScreen(COLOR_BG);

    // Gradient halus dari atas-kiri sedikit lebih terang ke dasar gelap
    // (mengikuti pola alcura.gif di mana sudut kiri-atas ada hint biru)
    for (int y = 0; y < 320; y++) {
      // Top sedikit lebih terang, semakin ke bawah semakin gelap
      uint16_t rowColor;
      if (y < 80) {
        // baris atas: blend dari BG_LIGHTER ke BG
        int blend = 255 - (y * 255 / 80);
        rowColor = blend565(COLOR_BG_LIGHTER, COLOR_BG, blend);
      } else {
        rowColor = COLOR_BG;
      }
      tft.drawFastHLine(0, y, 480, rowColor);
    }

    // Radial glow di pojok kanan-bawah (pola lingkaran dari gif)
    drawRadialGlow(440, 300, 220, COLOR_GLOW_CENTER, COLOR_BG);

    // Lingkaran kecil aksen (mirip dot di frame akhir gif)
    tft.fillCircle(440, 300, 4, COLOR_ACCENT_TEAL);

    splashDrawn = true;
  }

  // ALCURA text - fade in (mengikuti animasi gif)
  if (progress > 0.10f) {
    float textAlpha = min((progress - 0.10f) / 0.30f, 1.0f);
    tft.setTextDatum(MC_DATUM);
    uint16_t alcuraColor = alpha565(COLOR_ACCENT_CYAN, COLOR_BG, textAlpha);
    tft.setTextColor(alcuraColor);
    tft.drawString("ALCURA", 240, 130, 7);  // large text di tengah-atas

    // Subtitle "Display System" - muncul setelah ALCURA
    if (progress > 0.30f) {
      float subAlpha = min((progress - 0.30f) / 0.25f, 1.0f);
      uint16_t subColor = alpha565(COLOR_ACCENT_SOFT, COLOR_BG, subAlpha);
      tft.setTextColor(subColor);
      tft.drawString("Display System", 240, 180, 4);
    }

    // Versi
    if (progress > 0.45f) {
      float verAlpha = min((progress - 0.45f) / 0.20f, 1.0f);
      uint16_t verColor = alpha565(COLOR_TEXT_MUTED, COLOR_BG, verAlpha);
      tft.setTextColor(verColor);
      tft.drawString("v1.0", 240, 215, 2);
    }
  }

  // Animated progress bar (sederhana di bawah)
  if (progress > 0.55f) {
    int barMaxWidth = 280;
    int barWidth = (int)(barMaxWidth * min((progress - 0.55f) / 0.40f, 1.0f));
    int barX = (480 - barMaxWidth) / 2;
    int barY = 260;

    // Track
    tft.drawRoundRect(barX, barY, barMaxWidth, 6, 3, COLOR_TEXT_MUTED);
    // Fill
    tft.fillRoundRect(barX + 1, barY + 1, max(0, barWidth - 2), 4, 2, COLOR_ACCENT_CYAN);
  }

  // Loading text
  if (progress > 0.75f) {
    float loadAlpha = min((progress - 0.75f) / 0.20f, 1.0f);
    tft.setTextDatum(MC_DATUM);
    uint16_t loadColor = alpha565(COLOR_TEXT_MUTED, COLOR_BG, loadAlpha);
    tft.setTextColor(loadColor);
    tft.drawString("Loading...", 240, 290, 2);
  }

  if (elapsedTime >= splashDuration) {
    currentState = MENU;
    menuSelection = 0;
    screenDrawn = false;
    delay(200);
  }
}

// Helper: blend dua warna 565 berdasarkan faktor (0..255, 0=full a, 255=full b)
uint16_t blend565(uint16_t a, uint16_t b, uint8_t factor) {
  uint8_t ar = (a >> 11) & 0x1F;
  uint8_t ag = (a >> 5) & 0x3F;
  uint8_t ab = a & 0x1F;
  uint8_t br = (b >> 11) & 0x1F;
  uint8_t bg_c = (b >> 5) & 0x3F;
  uint8_t bb = b & 0x1F;
  uint8_t r = ar + ((br - ar) * factor) / 255;
  uint8_t g = ag + ((bg_c - ag) * factor) / 255;
  uint8_t bl = ab + ((bb - ab) * factor) / 255;
  return (r << 11) | (g << 5) | bl;
}

// Helper: blend warna foreground ke background dengan alpha 0..1
uint16_t alpha565(uint16_t fg, uint16_t bg, float alpha) {
  if (alpha <= 0) return bg;
  if (alpha >= 1) return fg;
  uint8_t factor = (uint8_t)(alpha * 255);
  return blend565(bg, fg, factor);  // 0 = bg, 255 = fg
}

// Helper: gambar radial glow (lingkaran dengan warna yang fading ke luar)
void drawRadialGlow(int cx, int cy, int maxRadius, uint16_t centerColor, uint16_t edgeColor) {
  int steps = 24;
  for (int i = steps; i >= 1; i--) {
    int r = (maxRadius * i) / steps;
    uint8_t factor = ((steps - i) * 255) / steps;  // 0 di pusat, 255 di tepi
    uint16_t color = blend565(centerColor, edgeColor, factor);
    tft.drawCircle(cx, cy, r, color);
  }
  // Isi dalam dengan center color (dengan alpha bertahap untuk menghindari solid disk)
  for (int r = maxRadius; r >= 0; r -= 4) {
    uint8_t factor = ((maxRadius - r) * 200) / maxRadius;  // softer falloff
    uint16_t color = blend565(edgeColor, centerColor, 255 - factor);
    tft.drawCircle(cx, cy, r, color);
  }
}

// ==================== MAIN MENU ====================
void showMainMenu() {
  // TOUCH INPUT
  if (tft.getTouch(&touchX, &touchY)) {
    Serial.printf("Touch: X=%d, Y=%d\n", touchX, touchY);

    // Home card touch (kiri besar)
    if (touchX < 240) {
      menuSelection = 0;
      delay(200);
    }
    // Settings card touch (kanan atas)
    else if (touchX >= 240 && touchY < 160) {
      menuSelection = 1;
      delay(200);
    }
    // Info card touch (kanan bawah)
    else if (touchX >= 240 && touchY >= 160) {
      menuSelection = 2;
      delay(200);
    }
  }

  // SERIAL INPUT (cadangan)
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'U' || cmd == 'u' || cmd == 'L' || cmd == 'l') {
      menuSelection = (menuSelection - 1 + 3) % 3;
    } else if (cmd == 'D' || cmd == 'd' || cmd == 'R' || cmd == 'r') {
      menuSelection = (menuSelection + 1) % 3;
    } else if (cmd == 'E' || cmd == 'e') {
      AppState states[] = {HOME, SETTINGS, INFO};
      currentState = states[menuSelection];
      screenDrawn = false;
      delay(200);
      return;
    }
  }

  if (menuSelection == previousMenuSelection && screenDrawn) {
    return;
  }

  // Background dark navy + sentuhan glow pojok kanan-bawah
  tft.fillScreen(COLOR_BG);
  drawRadialGlow(440, 300, 180, COLOR_GLOW_RING, COLOR_BG);

  // Header bar
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_LIGHT);
  tft.drawString("9:41", 18, 18, 2);

  // Battery icon (cyan accent)
  tft.drawRoundRect(440, 14, 22, 11, 3, COLOR_TEXT_MUTED);
  tft.fillRoundRect(442, 16, 15, 7, 1, COLOR_BATTERY_FILL);
  tft.fillRect(463, 17, 2, 5, COLOR_TEXT_MUTED);

  // Title
  tft.setTextColor(COLOR_TEXT_LIGHT);
  tft.drawString("Menu", 20, 45, 4);
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("Pilih salah satu menu", 20, 68, 2);

  // Cards (4 macam warna cyan/teal sesuai palet gif)
  drawHomeCard(16, 96, 220, 196, menuSelection == 0);
  drawSettingCard(252, 96, 212, 92, menuSelection == 1);
  drawInfoCard(252, 200, 212, 92, menuSelection == 2);

  // Bottom pill indicator
  tft.fillRoundRect(210, 305, 60, 5, 2, COLOR_PILL);

  previousMenuSelection = menuSelection;
  screenDrawn = true;
}

void drawRoundedCard(int x, int y, int w, int h, int r, uint16_t color) {
  tft.fillRect(x + r, y, w - 2*r, h, color);
  tft.fillRect(x, y + r, w, h - 2*r, color);
  tft.fillCircle(x + r, y + r, r, color);
  tft.fillCircle(x + w - r, y + r, r, color);
  tft.fillCircle(x + r, y + h - r, r, color);
  tft.fillCircle(x + w - r, y + h - r, r, color);
}

void drawHomeCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, 22, COLOR_CARD_HOME);
  if (selected) {
    tft.drawRoundRect(x, y, w, h, 22, COLOR_BORDER_SELECT);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 22, COLOR_BORDER_SELECT);
  }

  int cx = x + w / 2;
  int cy = y + 62;
  int r = 38;
  tft.fillCircle(cx, cy, r, COLOR_TEXT_WHITE);

  // Home icon (darker version of card color)
  uint16_t c = COLOR_CARD_HOME;
  tft.drawLine(cx - 18, cy + 2, cx, cy - 18, c);
  tft.drawLine(cx, cy - 18, cx + 18, cy + 2, c);
  tft.drawRect(cx - 13, cy + 2, 26, 20, c);
  tft.fillRect(cx - 5, cy + 10, 10, 12, COLOR_TEXT_WHITE);
  tft.drawRect(cx - 5, cy + 10, 10, 12, c);

  // Text
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Home", cx, y + 136, 4);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Halaman utama", cx, y + 160, 2);
}

void drawSettingCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, 22, COLOR_CARD_SETTINGS);
  if (selected) {
    tft.drawRoundRect(x, y, w, h, 22, COLOR_BORDER_SELECT);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 22, COLOR_BORDER_SELECT);
  }

  int cx = x + 48;
  int cy = y + 46;
  int r = 30;
  tft.fillCircle(cx, cy, r, COLOR_TEXT_WHITE);

  // Setting icon (gear sederhana)
  uint16_t c = COLOR_CARD_SETTINGS;
  tft.drawLine(cx - 13, cy - 8, cx + 13, cy - 8, c);
  tft.drawLine(cx - 13, cy,     cx + 13, cy,     c);
  tft.drawLine(cx - 13, cy + 8, cx + 13, cy + 8, c);
  tft.fillCircle(cx + 5, cy - 8, 3, c);
  tft.fillCircle(cx - 7, cy,     3, c);
  tft.fillCircle(cx + 7, cy + 8, 3, c);

  // Text
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Setting", cx + 44, cy - 8, 4);
}

void drawInfoCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, 22, COLOR_CARD_INFO);
  if (selected) {
    tft.drawRoundRect(x, y, w, h, 22, COLOR_BORDER_SELECT);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 22, COLOR_BORDER_SELECT);
  }

  int cx = x + 48;
  int cy = y + 46;
  int r = 30;
  tft.fillCircle(cx, cy, r, COLOR_TEXT_WHITE);

  // Info icon (i dengan dot di atas)
  uint16_t c = COLOR_CARD_INFO;
  tft.fillCircle(cx, cy - 12, 4, c);
  tft.fillRoundRect(cx - 3, cy - 5, 7, 18, 3, c);

  // Text
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Info", cx + 44, cy - 8, 4);
}

// ==================== HOME PAGE ====================
void showHome() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      delay(200);
      return;
    }
  }
  // Subtle glow background
  drawRadialGlow(440, 300, 180, COLOR_GLOW_RING, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_ACCENT_CYAN);
  tft.drawString("Home", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT_LIGHT);
  int y = 80;
  tft.drawString("Welcome!", 20, y, 2); y += 40;
  tft.drawString("ACURA System", 20, y, 2); y += 40;
  tft.drawString("v1.0", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("Press B to Back", 240, 300, 2);
}

// ==================== SETTINGS PAGE ====================
void showSettings() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      delay(200);
      return;
    }
  }
  drawRadialGlow(440, 300, 180, COLOR_GLOW_RING, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_ACCENT_TEAL);
  tft.drawString("Settings", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT_LIGHT);
  int y = 80;
  tft.drawString("Brightness: 100%", 20, y, 2); y += 40;
  tft.drawString("Rotation: Landscape", 20, y, 2); y += 40;
  tft.drawString("Timer: 5 min", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("Press B to Back", 240, 300, 2);
}

// ==================== INFO PAGE ====================
void showInfo() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      delay(200);
      return;
    }
  }
  drawRadialGlow(440, 300, 180, COLOR_GLOW_RING, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_CARD_INFO);
  tft.drawString("Info", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT_LIGHT);
  int y = 80;
  tft.drawString("Device: ESP32-035", 20, y, 2); y += 40;
  tft.drawString("Resolution: 480x320", 20, y, 2); y += 40;
  tft.drawString("Uptime: " + String(millis()/1000) + "s", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("Press B to Back", 240, 300, 2);
}

// ==================== ABOUT PAGE ====================
void showAbout() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      delay(200);
      return;
    }
  }
  drawRadialGlow(440, 300, 180, COLOR_GLOW_RING, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_CARD_ABOUT);
  tft.drawString("About", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT_LIGHT);
  int y = 80;
  tft.drawString("ACURA System", 20, y, 2); y += 40;
  tft.drawString("Version 1.0", 20, y, 2); y += 40;
  tft.drawString("ESP32-035 TFT", 20, y, 2); y += 40;
  tft.drawString("Made with", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_MUTED);
  tft.drawString("Press B to Back", 240, 300, 2);
}
