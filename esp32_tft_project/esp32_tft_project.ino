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
  ABOUT,
  CHART_DETAIL,
  LIGHT,
  FAN
};

AppState currentState = SPLASH_SCREEN;  // Kembali ke splash screen
AppState previousState = SPLASH_SCREEN;
unsigned long splashStartTime = 0;
int menuSelection = 0;
int chartSensor = 0;  // 0=CO2, 1=DO, 2=Suhu, 3=pH (urutan kartu di Home)
int previousMenuSelection = -1;
bool screenDrawn = false;

// Light page state
bool lampState[5]    = {true, true, false, true, false};
int  lightBrightness = 72;

// Fan page state
bool fanState[2] = {true, false};

// Settings (WiFi) page state
bool wifiState = true;

// ==================== PALET WARNA HIJAU MEWAH ====================
// #EEF5F0 - Very light green (background terang)
// #309D5B - Medium green (accent utama)
// #173123 - Dark green (text/dark element)
// #647A6B - Muted green/gray (secondary)
// #D1EAD7 - Light green (light accent)
#define COLOR_BG_SPLASH      0xF79E   // #EEF5F0 - light green background
#define COLOR_BG_MENU        0xF79E   // #EEF5F0 - light green menu background
#define COLOR_ACCENT_GREEN   0x34CB   // #309D5B - medium green accent
#define COLOR_DARK_GREEN     0x1684   // #173123 - dark green
#define COLOR_MUTED_GREEN    0x67AD   // #647A6B - muted green/gray
#define COLOR_LIGHT_GREEN    0xD7BA   // #D1EAD7 - light green
#define COLOR_GLOW_CYAN      0x34CB   // Glow cyan (sama dengan accent green)
#define COLOR_TEXT_WHITE     0xFFFF   // Putih
#define COLOR_TEXT_BLACK     0x0000   // Hitam
#define COLOR_TEXT_GRAY      0x67AD   // Abu-abu (muted green)
#define COLOR_TEXT_ORANGE    0x34CB   // Orange (accent green untuk subtitle)
#define COLOR_CARD_HOME      0x34CB   // Hijau medium - Home card
#define COLOR_CARD_SETTINGS  0x34CB   // Hijau medium - Settings card
#define COLOR_CARD_INFO      0x34CB   // Hijau medium - Info card
#define COLOR_BORDER_LIGHT   0xD7BA   // Light green border
#define COLOR_PILL           0x67AD   // Muted green pill
#define COLOR_BG_HOME_RIGHT  0xF7FE   // Very light mint green (right panel home)
#define COLOR_GAUGE_TRACK    0x2C49   // Dark muted green (gauge background track)

void setup(void) {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ALCURA (480x320) WITH TOUCH ===");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  delay(100);

  tft.init();
  tft.setRotation(1);
  tft.setTouch(calData);
  tft.fillScreen(0x050F);
  delay(100);

  Serial.println("Setup complete!");
  splashStartTime = millis();
}

void loop() {
  if (currentState != previousState) {
    tft.fillScreen(COLOR_BG_SPLASH);
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
    case CHART_DETAIL: showChartDetail(); break;
    case LIGHT: showLight(); break;
    case FAN: showFan(); break;
  }
  delay(50);
}

// ==================== SPLASH SCREEN - ANIMATED VERSION ====================
void showSplashScreen() {
  unsigned long elapsedTime = millis() - splashStartTime;
  int splashDuration = 3500;
  float progress = min((float)elapsedTime / splashDuration, 1.0f);

  // INIT SCREEN DENGAN GRADIENT BACKGROUND
  static bool initialized = false;
  if (!initialized) {
    // Gradient background: Teal/Cyan di corner, Dark Navy di tengah/bawah
    // Membuat gradient effect dengan gradient fill sederhana
    for (int y = 0; y < 320; y++) {
      float yFactor = (float)y / 320.0f;
      // Gradient dari teal (atas kiri) ke dark navy (bawah kanan)
      uint16_t gradColor = blendGradient(0x07FF, 0x0000, yFactor);  // Cyan to Black
      tft.drawFastHLine(0, y, 480, gradColor);
    }
    delay(100);
    initialized = true;
  }

  // ===== ANIMATED GLOW EFFECT & TEKS ALCURA =====
  // Text muncul setelah 20% progress
  if (progress > 0.20f && progress < 0.80f) {
    // Animation progress
    float animProgress = (progress - 0.20f) / (0.80f - 0.20f);

    // Pulsing glow intensity
    float glowIntensity = sin(animProgress * 6.28f) * 0.3f + 0.7f;  // 0.4 - 1.0

    // Draw subtle outer glow circle (hanya 1-2 circle, bukan banyak)
    int outerGlow = 80;
    tft.drawCircle(240, 160, outerGlow, 0x07FF);  // Cyan outer glow

    // ===== TEXT ALCURA =====
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xFFFF);  // White text
    tft.drawString("ALCURA", 240, 160, 4);
  }

  // AUTO TRANSITION KE MENU
  if (elapsedTime >= splashDuration) {
    currentState = MENU;
    menuSelection = 0;
    screenDrawn = false;
    initialized = false;
    delay(200);
  }
}

// Helper function untuk gradient blend
uint16_t blendGradient(uint16_t colorA, uint16_t colorB, float factor) {
  uint8_t ar = (colorA >> 11) & 0x1F;
  uint8_t ag = (colorA >> 5) & 0x3F;
  uint8_t ab = colorA & 0x1F;
  uint8_t br = (colorB >> 11) & 0x1F;
  uint8_t bg = (colorB >> 5) & 0x3F;
  uint8_t bb = colorB & 0x1F;

  uint8_t r = ar + ((br - ar) * factor);
  uint8_t g = ag + ((bg - ag) * factor);
  uint8_t b = ab + ((bb - ab) * factor);

  return (r << 11) | (g << 5) | b;
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

// ==================== BACK BUTTON ====================
// Gambar tombol "< Menu" di posisi (x,y), lebar w, tinggi h
void drawBackButton(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, h / 2, COLOR_DARK_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xFFFF);
  tft.drawString("< Menu", x + w / 2, y + h / 2, 2);
}

// Cek apakah koordinat touch ada di dalam area tombol back
bool touchInBackBtn(uint16_t tx, uint16_t ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

// ==================== MAIN MENU ====================
void showMainMenu() {
  // TOUCH INPUT
  // Sumbu X touch terbalik: kiri layar = touchX tinggi, kanan = touchX rendah
  if (tft.getTouch(&touchX, &touchY, 200)) {
    if (touchX >= 240 && touchY >= 90 && touchY < 292) {
      currentState = HOME;
      screenDrawn = false;
      delay(200);
      return;
    }
    else if (touchX < 240 && touchY >= 90 && touchY < 155) {
      currentState = SETTINGS;
      screenDrawn = false;
      delay(200);
      return;
    }
    else if (touchX < 240 && touchY >= 158 && touchY < 225) {
      currentState = FAN;
      screenDrawn = false;
      delay(200);
      return;
    }
    else if (touchX < 240 && touchY >= 228 && touchY < 295) {
      currentState = LIGHT;
      screenDrawn = false;
      delay(200);
      return;
    }
  }

  // SERIAL INPUT (cadangan)
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'U' || cmd == 'u' || cmd == 'L' || cmd == 'l') {
      menuSelection = (menuSelection - 1 + 4) % 4;
    } else if (cmd == 'D' || cmd == 'd' || cmd == 'R' || cmd == 'r') {
      menuSelection = (menuSelection + 1) % 4;
    } else if (cmd == 'E' || cmd == 'e') {
      AppState states[] = {HOME, SETTINGS, FAN, LIGHT};
      currentState = states[menuSelection];
      screenDrawn = false;
      delay(200);
      return;
    }
  }

  if (menuSelection == previousMenuSelection && screenDrawn) {
    return;
  }

  tft.fillScreen(COLOR_BG_MENU);

  // Status bar
  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);

  // Battery isi penuh (solid hijau)
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  // Title
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("Menu", 20, 43, 4);

  // Subtitle
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Pilih salah satu menu", 20, 70, 2);

  // Home: square card besar di kiri
  drawHomeCard(14, 90, 220, 200, menuSelection == 0);

  // 3 pill buttons di kanan
  drawSettingCard(242, 90,  232, 62, menuSelection == 1);
  drawFanCard(    242, 158, 232, 62, menuSelection == 2);
  drawLightCard(  242, 226, 232, 62, menuSelection == 3);

  // Bottom indicator
  tft.fillRoundRect(210, 308, 60, 5, 2, COLOR_PILL);

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
  drawRoundedCard(x, y, w, h, 18, COLOR_CARD_HOME);

  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 20, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, 21, COLOR_TEXT_GRAY);
  }

  // Icon circle putih di tengah-atas card
  int cx = x + w / 2;
  int cy = y + 78;
  tft.fillCircle(cx, cy, 40, COLOR_TEXT_WHITE);
  drawHouseIcon(cx, cy, COLOR_CARD_HOME);

  // Text centered di bawah circle
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Home", cx, y + 155, 4);
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString("Halaman utama", cx, y + 175, 2);
}

void drawSettingCard(int x, int y, int w, int h, bool selected) {
  // Pill button
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_SETTINGS);

  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }

  int cx = x + h / 2;
  int cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawSlidersIcon(cx, cy, COLOR_CARD_SETTINGS);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Setting", cx + h / 2 + 4, cy, 4);
}

void drawInfoCard(int x, int y, int w, int h, bool selected) {
  // Pill button
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_INFO);

  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }

  int cx = x + h / 2;
  int cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawInfoIconPill(cx, cy, COLOR_CARD_INFO);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Info", cx + h / 2 + 4, cy, 4);
}

// ==================== MENU ICON HELPERS ====================
void drawHouseIcon(int cx, int cy, uint16_t c) {
  tft.fillTriangle(cx, cy - 18, cx - 18, cy + 2, cx + 18, cy + 2, c);  // atap
  tft.fillRect(cx - 13, cy + 1, 26, 20, c);                              // dinding
  tft.fillRect(cx - 5, cy + 9, 10, 12, 0xFFFF);                         // pintu (potong putih)
}

void drawSlidersIcon(int cx, int cy, uint16_t c) {
  // 3 bar horizontal
  tft.fillRect(cx - 13, cy - 9, 26, 2, c);
  tft.fillRect(cx - 13, cy - 1, 26, 2, c);
  tft.fillRect(cx - 13, cy + 7, 26, 2, c);
  // Knob slider di posisi berbeda
  tft.fillCircle(cx - 4, cy - 8, 5, c);
  tft.fillCircle(cx + 5, cy,     5, c);
  tft.fillCircle(cx - 2, cy + 8, 5, c);
}

void drawInfoIconPill(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx, cy - 9, 4, c);               // titik atas
  tft.fillRoundRect(cx - 3, cy - 3, 7, 16, 2, c); // batang bawah
}

void drawBulbIcon(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx, cy - 4, 11, c);       // badan bola lampu
  tft.fillRect(cx - 7, cy + 6,  14, 4, c); // dudukan atas
  tft.fillRect(cx - 5, cy + 10, 10, 4, c); // dudukan tengah
  tft.fillRect(cx - 3, cy + 14,  6, 3, c); // dudukan bawah
}

void drawLightCard(int x, int y, int w, int h, bool selected) {
  // Pill button
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_HOME);

  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }

  int cx = x + h / 2;
  int cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawBulbIcon(cx, cy, COLOR_CARD_HOME);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Light", cx + h / 2 + 4, cy, 4);
}

void drawFanIcon(int cx, int cy, uint16_t c) {
  // 4 bilah fan tersapu (swept blades)
  tft.fillTriangle(cx,   cy-3,  cx-8,  cy-16, cx+6,  cy-14, c);
  tft.fillTriangle(cx+3, cy,    cx+16, cy-6,  cx+14, cy+8,  c);
  tft.fillTriangle(cx,   cy+3,  cx+8,  cy+16, cx-6,  cy+14, c);
  tft.fillTriangle(cx-3, cy,    cx-16, cy+6,  cx-14, cy-8,  c);
  tft.fillCircle(cx, cy, 4, c);  // hub tengah
}

void drawFanCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, h / 2, COLOR_CARD_SETTINGS);

  if (selected) {
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, COLOR_TEXT_WHITE);
    tft.drawRoundRect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, COLOR_TEXT_GRAY);
  }

  int cx = x + h / 2;
  int cy = y + h / 2;
  tft.fillCircle(cx, cy, h / 2 - 8, COLOR_TEXT_WHITE);
  drawFanIcon(cx, cy, COLOR_CARD_SETTINGS);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Fan", cx + h / 2 + 4, cy, 4);
}

// ==================== SENSOR CARD ICONS ====================
void drawCloudIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillCircle(cx - 5, cy + 1, 5, c);
  tft.fillCircle(cx + 5, cy + 1, 5, c);
  tft.fillCircle(cx,     cy - 3, 6, c);
  tft.fillRect(cx - 9, cy + 1, 19, 5, c);
}

void drawDropIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillTriangle(cx, cy - 9, cx - 6, cy + 1, cx + 6, cy + 1, c);
  tft.fillCircle(cx, cy + 2, 6, c);
}

void drawThermIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillRoundRect(cx - 3, cy - 10, 6, 14, 3, c);
  tft.fillCircle(cx, cy + 6, 6, c);
}

void drawFlaskIcon(int cx, int cy) {
  uint16_t c = COLOR_ACCENT_GREEN;
  tft.fillRect(cx - 3, cy - 10, 6, 8, c);
  tft.fillRect(cx - 7, cy - 2,  14, 2, c);
  tft.fillRoundRect(cx - 8, cy, 16, 11, 4, c);
}

// ==================== SENSOR CARD ====================
void drawSensorCard(int x, int y, int w, int h, const char* label, const char* value, const char* unit, uint8_t iconType) {
  // Card hijau accent (mengikuti komposisi warna card di menu)
  drawRoundedCard(x, y, w, h, 12, COLOR_CARD_HOME);

  // Label (top-left) - putih
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString(label, x + 10, y + 10, 2);

  // Icon circle putih (top-right) dengan ikon hijau - seperti card menu
  int icx = x + w - 25, icy = y + 23;
  tft.fillCircle(icx, icy, 16, COLOR_TEXT_WHITE);
  switch (iconType) {
    case 0: drawCloudIcon(icx, icy); break;
    case 1: drawDropIcon(icx, icy);  break;
    case 2: drawThermIcon(icx, icy); break;
    case 3: drawFlaskIcon(icx, icy); break;
  }

  // Value (large) - putih
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString(value, x + 10, y + 52, 4);

  // Unit (bottom-left) - hijau terang
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString(unit, x + 10, y + h - 24, 2);
}

// ==================== HOME / CULTURE PAGE ====================
// Grid: 3 kolom (x=8,165,322; w=149), Udara 2×h62, Air 1×h70
// Kembali ke Menu: tap header (touchY < 62)

// Icon kecil untuk kartu kultur (muat dalam kotak 22×22)
void drawCloudIconSm(int cx, int cy, uint16_t c) {
  tft.fillCircle(cx-3, cy+1, 3, c);
  tft.fillCircle(cx+3, cy+1, 3, c);
  tft.fillCircle(cx,   cy-2, 4, c);
  tft.fillRect  (cx-5, cy+1, 11, 3, c);
}
void drawDropIconSm(int cx, int cy, uint16_t c) {
  tft.fillTriangle(cx, cy-6, cx-4, cy, cx+4, cy, c);
  tft.fillCircle(cx, cy+1, 4, c);
}
void drawThermIconSm(int cx, int cy, uint16_t c) {
  tft.fillRoundRect(cx-2, cy-7, 4, 9, 2, c);
  tft.fillCircle(cx, cy+4, 4, c);
}
void drawFlaskIconSm(int cx, int cy, uint16_t c) {
  tft.fillRect(cx-2, cy-7, 4, 5, c);
  tft.fillRect(cx-5, cy-2, 10, 2, c);
  tft.fillRoundRect(cx-5, cy, 10, 7, 3, c);
}

// Kartu sensor untuk halaman kultur
void drawCultureCard(int x, int y, int w, int h,
                     const char* name, const char* val, const char* unit,
                     const char* badge, uint8_t iconType, bool isAir) {
  uint16_t iconBg = isAir ? 0xCEFF : COLOR_LIGHT_GREEN;
  uint16_t iconFg = isAir ? 0x3C1E : COLOR_DARK_GREEN;

  // Latar putih + border
  tft.fillRoundRect(x, y, w, h, 10, COLOR_TEXT_WHITE);
  tft.drawRoundRect(x, y, w, h, 10, COLOR_BORDER_LIGHT);

  // Kotak ikon (kiri atas, 22×22)
  tft.fillRoundRect(x+5, y+5, 22, 22, 6, iconBg);
  int icx = x + 16, icy = y + 16;
  switch (iconType) {
    case 0: drawCloudIconSm(icx, icy, iconFg); break;
    case 1: drawDropIconSm (icx, icy, iconFg); break;
    case 2: drawThermIconSm(icx, icy, iconFg); break;
    default: drawFlaskIconSm(icx, icy, iconFg); break;
  }

  // Badge status (kanan atas)
  int bl = strlen(badge), bw2 = bl * 6 + 10;
  int bx2 = x + w - bw2 - 4;
  tft.fillRoundRect(bx2, y+7, bw2, 16, 8, iconBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(iconFg);
  tft.drawString(badge, bx2 + bw2/2, y+15, 1);

  // Nama sensor
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString(name, x+6, y+31, 1);

  // Nilai besar + unit kecil
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(val, x+6, y+42, 2);
  int vw = tft.textWidth(val, 2);
  tft.setTextColor(COLOR_TEXT_GRAY);
  tft.drawString(unit, x + 8 + vw, y+48, 1);
}

void showHome() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    uint16_t dx = (touchX <= 480) ? (480 - touchX) : 0;

    // Kembali ke Menu: tap header
    if (touchY < 62) {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }
    // Sensor Udara: 2 baris × 3 kolom → tiap kartu punya chart sendiri
    //   Baris 1 (y=80-142):  O2(4),  PM2.5(5), PM10(6)
    //   Baris 2 (y=146-208): HCHO(7), VOC(8),  Humidity(9)
    if (touchY >= 80 && touchY < 208) {
      int row = (touchY < 142) ? 0 : (touchY >= 146) ? 1 : -1;
      if (row >= 0) {
        int col = (dx < 165) ? 0 : (dx < 322) ? 1 : 2;
        const int airMap[2][3] = {{4, 5, 6}, {7, 8, 9}};
        chartSensor = airMap[row][col];
        currentState = CHART_DETAIL;
        screenDrawn = false;
        delay(200);
        return;
      }
    }
    // Sensor Air (y=228-298): DO→1, Suhu Air→2, Turbidity→3
    if (touchY >= 228 && touchY < 298) {
      int col = (dx < 165) ? 0 : (dx < 322) ? 1 : 2;
      chartSensor = col + 1;  // 1=DO, 2=Suhu, 3=Turbidity
      currentState = CHART_DETAIL;
      screenDrawn = false;
      delay(200);
      return;
    }
  }
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);

  // Status bar
  tft.fillRect(0, 0, 480, 30, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 30, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 10, 8, 2);
  tft.fillRoundRect(440, 8, 30, 14, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 11, 3, 8, COLOR_TEXT_GRAY);

  // Header
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< Sensor Readings", 10, 33, 2);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Dikelompokkan: Udara & Air", 10, 51, 1);

  // Live pill (kanan)
  tft.fillRoundRect(388, 33, 82, 22, 11, COLOR_ACCENT_GREEN);
  tft.fillCircle(401, 44, 4, COLOR_LIGHT_GREEN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Live", 409, 44, 1);

  // Garis pemisah
  tft.drawFastHLine(0, 62, 480, COLOR_BORDER_LIGHT);

  // ── Sensor Udara ──
  tft.fillCircle(14, 71, 5, COLOR_ACCENT_GREEN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("Sensor Udara", 24, 71, 2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(COLOR_ACCENT_GREEN);
  tft.drawString("6 sensor", 474, 71, 1);

  // Baris 1 (y=80, h=62)
  drawCultureCard(  8, 80, 149, 62, "O2",       "21",   "%",     "Optimal", 0, false);
  drawCultureCard(165, 80, 149, 62, "PM2.5",    "8",    "ug/m3", "Baik",    0, false);
  drawCultureCard(322, 80, 149, 62, "PM10",     "25",   "ug/m3", "Baik",    0, false);

  // Baris 2 (y=146, h=62)
  drawCultureCard(  8, 146, 149, 62, "HCHO",     "0.03","ppm",   "Aman",    3, false);
  drawCultureCard(165, 146, 149, 62, "VOC",      "58",  "ppb",   "Baik",    3, false);
  drawCultureCard(322, 146, 149, 62, "Humidity", "52",  "%",     "Ideal",   1, false);

  // ── Sensor Air ──
  tft.fillCircle(14, 220, 5, 0x3C1E);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("Sensor Air", 24, 220, 2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(0x3C1E);
  tft.drawString("3 sensor", 474, 220, 1);

  // Baris Air (y=230, h=70)
  drawCultureCard(  8, 230, 149, 70, "DO",        "11.2","mg/L",   "Baik",   1, true);
  drawCultureCard(165, 230, 149, 70, "Suhu Air",  "27",  "Celsius","Normal", 2, true);
  drawCultureCard(322, 230, 149, 70, "Turbidity", "4.1", "NTU",    "Jernih", 1, true);

  tft.fillRoundRect(210, 309, 60, 5, 2, COLOR_PILL);
  screenDrawn = true;
}

// ==================== SETTINGS PAGE ====================
#define SUB_BTN_X    0
#define SUB_BTN_Y  278
#define SUB_BTN_W  480
#define SUB_BTN_H   40

void drawWifiIcon(int cx, int cy, uint16_t c) {
  // 3 cincin konsentris: gambar disc penuh lalu lubangi dengan putih
  tft.fillCircle(cx, cy, 22, c);
  tft.fillCircle(cx, cy, 18, COLOR_TEXT_WHITE);
  tft.fillCircle(cx, cy, 15, c);
  tft.fillCircle(cx, cy, 11, COLOR_TEXT_WHITE);
  tft.fillCircle(cx, cy,  8, c);
  tft.fillCircle(cx, cy,  4, COLOR_TEXT_WHITE);
  // Hapus separuh bawah → bentuk busur ∩ (sinyal ke atas)
  tft.fillRect(cx - 25, cy, 51, 28, COLOR_TEXT_WHITE);
  // Titik sinyal
  tft.fillCircle(cx, cy + 12, 5, c);
}

void drawWifiCard() {
  uint16_t bg = wifiState ? COLOR_ACCENT_GREEN : 0xC618;
  tft.fillRoundRect(10, 92, 460, 86, 14, bg);

  // Lingkaran putih + ikon WiFi
  tft.fillCircle(65, 135, 33, COLOR_TEXT_WHITE);
  drawWifiIcon(65, 135, bg);

  // Label + status
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("WiFi", 112, 120, 4);
  tft.setTextColor(wifiState ? COLOR_LIGHT_GREEN : 0xEF7D);
  tft.drawString(wifiState ? "Aktif" : "Tidak aktif", 112, 150, 2);

  // Toggle kanan
  int tx = 382, ty = 117;
  tft.fillRoundRect(tx, ty, 68, 36, 18, wifiState ? COLOR_DARK_GREEN : 0x8410);
  tft.fillCircle(wifiState ? tx + 50 : tx + 18, 135, 14, COLOR_TEXT_WHITE);
}

void drawWifiStatusCard() {
  tft.fillRoundRect(10, 186, 460, 66, 14, COLOR_TEXT_WHITE);
  tft.drawRoundRect(10, 186, 460, 66, 14, COLOR_BORDER_LIGHT);

  tft.fillCircle(40, 219, 7, COLOR_TEXT_GRAY);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(wifiState ? "Tidak terhubung" : "WiFi Mati", 60, 207, 2);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString(
    wifiState ? "Sambungkan jaringan untuk terhubung"
              : "Aktifkan WiFi terlebih dahulu", 60, 232, 1);
}

void showSettings() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    // Back: tap area header
    if (touchY >= 36 && touchY < 90) {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }
    // Toggle WiFi: tap card
    if (touchY >= 92 && touchY < 178) {
      wifiState = !wifiState;
      drawWifiCard();
      drawWifiStatusCard();
      delay(100);
      return;
    }
  }
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }
  }
  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);

  // Status bar
  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  // Header "< WiFi"
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< WiFi", 15, 40, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Koneksi jaringan", 36, 68, 2);

  drawWifiCard();
  drawWifiStatusCard();

  tft.fillRoundRect(210, 307, 60, 5, 2, COLOR_PILL);
  screenDrawn = true;
}

// ==================== INFO PAGE ====================
void showInfo() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    if (touchInBackBtn(touchX, touchY, SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H)) {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
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
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_CARD_INFO);
  tft.drawString("Info", 20, 10, 4);

  tft.setTextColor(COLOR_TEXT_BLACK);
  int y = 80;
  tft.drawString("Device: ESP32-035", 20, y, 2); y += 40;
  tft.drawString("Resolution: 480x320", 20, y, 2); y += 40;
  tft.drawString("Uptime: " + String(millis()/1000) + "s", 20, y, 2);

  drawBackButton(SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H);
  screenDrawn = true;
}

// ==================== ABOUT PAGE ====================
void showAbout() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    if (touchInBackBtn(touchX, touchY, SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H)) {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
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
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_CARD_HOME);
  tft.drawString("About", 20, 10, 4);

  tft.setTextColor(COLOR_TEXT_BLACK);
  int y = 80;
  tft.drawString("ALCURA", 20, y, 2); y += 40;
  tft.drawString("Version 1.0", 20, y, 2); y += 40;
  tft.drawString("ESP32-035 TFT", 20, y, 2); y += 40;
  tft.drawString("Made with", 20, y, 2);

  drawBackButton(SUB_BTN_X, SUB_BTN_Y, SUB_BTN_W, SUB_BTN_H);
  screenDrawn = true;
}

// ==================== LIGHT PAGE ====================
// Brightness card: y=90, h=90 (s/d y=180). Slider center y=150.
// Lamp grid: 3 kolom × 2 baris, col w=146, row h=55, mulai y=196.

void drawBrightnessCard() {
  tft.fillRoundRect(10, 90, 460, 90, 14, COLOR_ACCENT_GREEN);

  // Row 1: ikon + label + nilai
  drawBulbIcon(40, 108, COLOR_TEXT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Kecerahan", 58, 108, 4);
  char val[8];
  sprintf(val, "%d%%", lightBrightness);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(val, 462, 108, 4);

  // Row 2: slider — track hijau muda (kosong), isi gelap (penuh), knob putih
  int fillW = (430 * lightBrightness) / 100;
  int knobX = 25 + fillW;
  tft.fillRoundRect(25, 143, 430, 14, 7, COLOR_LIGHT_GREEN);
  if (fillW > 0)
    tft.fillRoundRect(25, 143, fillW, 14, 7, COLOR_DARK_GREEN);
  tft.fillCircle(knobX, 150, 13, COLOR_TEXT_WHITE);
}

void drawLampCard(int idx) {
  int col = idx % 3, row = idx / 3;
  int gx  = 10 + col * 157;
  int gy  = 196 + row * 65;
  bool on = lampState[idx];

  if (on) {
    tft.fillRoundRect(gx, gy, 146, 55, 14, COLOR_ACCENT_GREEN);
  } else {
    tft.fillRoundRect(gx, gy, 146, 55, 14, COLOR_TEXT_WHITE);
    tft.drawRoundRect(gx, gy, 146, 55, 14, COLOR_BORDER_LIGHT);
  }

  // Lingkaran ikon + bohlam
  int icx = gx + 23, icy = gy + 28;
  tft.fillCircle(icx, icy, 16, on ? COLOR_DARK_GREEN : 0xC618);
  drawBulbIcon(icx, icy, on ? COLOR_TEXT_WHITE : COLOR_TEXT_GRAY);

  // Nama + status
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

    // Kembali: tap header
    if (touchY >= 36 && touchY < 90) {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }

    // Slider kecerahan: tight loop drag mulus
    if (touchY >= 137 && touchY <= 167 && dx >= 25 && dx <= 455) {
      do {
        dx = touchX <= 480 ? 480 - touchX : 0;
        int nb = (int)(dx - 25) * 100 / 430;
        if (nb < 0)   nb = 0;
        if (nb > 100) nb = 100;
        if (nb != lightBrightness) {
          lightBrightness = nb;

          // Partial redraw: nilai teks
          tft.fillRect(370, 95, 94, 27, COLOR_ACCENT_GREEN);
          char val[8]; sprintf(val, "%d%%", lightBrightness);
          tft.setTextDatum(MR_DATUM);
          tft.setTextColor(COLOR_TEXT_WHITE);
          tft.drawString(val, 462, 108, 4);

          // Partial redraw: slider saja
          tft.fillRect(22, 137, 440, 30, COLOR_ACCENT_GREEN);
          int fillW = (430 * lightBrightness) / 100;
          int knobX = 25 + fillW;
          tft.fillRoundRect(25, 143, 430, 14, 7, COLOR_LIGHT_GREEN);
          if (fillW > 0)
            tft.fillRoundRect(25, 143, fillW, 14, 7, COLOR_DARK_GREEN);
          tft.fillCircle(knobX, 150, 13, COLOR_TEXT_WHITE);
        }
      } while (tft.getTouch(&touchX, &touchY, 200));
      return;
    }

    // Toggle lampu: grid 3×2
    for (int i = 0; i < 5; i++) {
      int col = i % 3, row = i / 3;
      int gx = 10 + col * 157;
      int gy = 196 + row * 65;
      if (dx >= gx && dx < gx + 146 && touchY >= gy && touchY < gy + 55) {
        lampState[i] = !lampState[i];
        drawLampCard(i);
        delay(100);
        return;
      }
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);

  // Status bar
  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  // Header "< Light"
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< Light", 15, 40, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Kontrol lampu", 36, 68, 2);

  drawBrightnessCard();

  // Label seksi
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_GRAY);
  tft.drawString("Lampu", 15, 183, 2);

  // Grid 5 lampu
  for (int i = 0; i < 5; i++) drawLampCard(i);

  tft.fillRoundRect(210, 309, 60, 5, 2, COLOR_PILL);
  screenDrawn = true;
}

// ==================== FAN PAGE ====================
// Hero card: y=92, h=78 (s/d y=170)
// Fan rows: y=178, h=52, step=60 — 2 baris s/d y=290

void drawBigFanIcon(int cx, int cy, uint16_t c) {
  // 4 bilah tersapu, skala besar (muat di lingkaran r=33)
  tft.fillTriangle(cx,   cy-6,  cx-14, cy-28, cx+11, cy-24, c);
  tft.fillTriangle(cx+6, cy,    cx+28, cy-11, cx+24, cy+14, c);
  tft.fillTriangle(cx,   cy+6,  cx+14, cy+28, cx-11, cy+24, c);
  tft.fillTriangle(cx-6, cy,    cx-28, cy+11, cx-24, cy-14, c);
  tft.fillCircle(cx, cy, 7, c);                   // hub
  tft.fillCircle(cx, cy, 3, COLOR_TEXT_WHITE);    // lubang hub
}

void drawFanHeroCard() {
  tft.fillRoundRect(10, 92, 460, 78, 14, COLOR_ACCENT_GREEN);

  // Lingkaran putih besar + ikon kipas
  tft.fillCircle(65, 131, 33, COLOR_TEXT_WHITE);
  drawBigFanIcon(65, 131, COLOR_ACCENT_GREEN);

  // Teks
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT_WHITE);
  tft.drawString("Kipas Angin", 112, 116, 4);
  tft.setTextColor(COLOR_LIGHT_GREEN);
  tft.drawString("Kelola 2 kipas", 112, 148, 2);
}

void drawFanRowCard(int idx) {
  int ry  = 178 + idx * 60;
  int rcy = ry + 26;
  bool on = fanState[idx];

  // Baris: latar putih + border
  tft.fillRoundRect(10, ry, 460, 52, 26, COLOR_TEXT_WHITE);
  tft.drawRoundRect(10, ry, 460, 52, 26, COLOR_BORDER_LIGHT);

  // Lingkaran ikon kipas
  tft.fillCircle(44, rcy, 22, on ? COLOR_ACCENT_GREEN : 0xC618);
  drawFanIcon(44, rcy, COLOR_TEXT_WHITE);

  // Nama kipas
  char name[10];
  sprintf(name, "Kipas %d", idx + 1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString(name, 76, ry + 16, 2);

  // Status text
  tft.setTextColor(on ? COLOR_ACCENT_GREEN : COLOR_TEXT_GRAY);
  tft.drawString(on ? "Menyala" : "Mati", 76, ry + 37, 2);

  // Toggle pill dengan label ON/OFF
  int tx = 390, ty = ry + 10;
  tft.fillRoundRect(tx, ty, 70, 32, 16, on ? COLOR_ACCENT_GREEN : 0xC618);
  if (on) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("ON", tx + 8, ty + 16, 2);
    tft.fillCircle(tx + 54, ty + 16, 12, COLOR_TEXT_WHITE);
  } else {
    tft.fillCircle(tx + 16, ty + 16, 12, COLOR_TEXT_WHITE);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COLOR_TEXT_WHITE);
    tft.drawString("OFF", tx + 62, ty + 16, 2);
  }
}

void showFan() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    // Tombol kembali: tap area header
    if (touchY >= 36 && touchY < 90) {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }
    // Toggle kipas: tap baris mana saja
    for (int i = 0; i < 2; i++) {
      int ry = 178 + i * 60;
      if (touchY >= ry && touchY < ry + 54) {
        fanState[i] = !fanState[i];
        drawFanRowCard(i);
        delay(100);
        return;
      }
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'B' || cmd == 'b') {
      currentState = MENU;
      screenDrawn = false;
      delay(200);
      return;
    }
  }

  if (screenDrawn) return;

  tft.fillScreen(COLOR_BG_MENU);

  // Status bar
  tft.fillRect(0, 0, 480, 35, COLOR_TEXT_WHITE);
  tft.drawFastHLine(0, 35, 480, COLOR_BORDER_LIGHT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_BLACK);
  tft.drawString("9:41", 15, 8, 2);
  tft.fillRoundRect(440, 10, 30, 16, 3, COLOR_ACCENT_GREEN);
  tft.fillRect(470, 14, 3, 8, COLOR_TEXT_GRAY);

  // Header "< Fan"
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_DARK_GREEN);
  tft.drawString("< Fan", 15, 40, 4);
  tft.setTextColor(COLOR_MUTED_GREEN);
  tft.drawString("Kontrol kipas", 36, 68, 2);

  // Hero card
  drawFanHeroCard();

  // 2 baris kipas
  for (int i = 0; i < 2; i++) drawFanRowCard(i);

  // Bottom indicator
  tft.fillRoundRect(210, 307, 60, 5, 2, COLOR_PILL);

  screenDrawn = true;
}

// ==================== CHART DETAIL PAGE ====================
// Warna khusus halaman grafik
#define COLOR_ICON_CIRCLE   0xFFFF   // Lingkaran ikon putih (seperti card home)
#define COLOR_MUTED_LIGHT   0xD7BA   // Teks sekunder hijau terang (= COLOR_LIGHT_GREEN)
#define COLOR_CHART_LINE    0x1B48   // Garis kurva (hijau tua)
#define COLOR_CHART_FILL    0xDF5B   // Area fill di bawah kurva (hijau muda)
#define COLOR_RT_DOT        0xD7BA   // Dot indikator real-time (hijau terang)

// ----- Tema BIRU (untuk sensor air: DO, Suhu Air, Turbidity) -----
#define COLOR_ACCENT_BLUE      0x2C7C   // Biru medium (panel & aksen)
#define COLOR_DARK_BLUE        0x19CD   // Biru tua (teks gelap & pill)
#define COLOR_MUTED_LIGHT_BLUE 0xBEBE   // Biru terang (subjudul/unit di panel)
#define COLOR_MUTED_BLUE       0x6CDB   // Biru keabu (teks sekunder kartu)
#define COLOR_LIGHT_BLUE       0xDF5F   // Biru sangat muda (pill & kartu statistik)
#define COLOR_BG_BLUE          0xEF9F   // Background panel kanan (biru sangat muda)
#define COLOR_CHART_LINE_BLUE  0x22B1   // Garis kurva (biru tua)
#define COLOR_CHART_FILL_BLUE  0xD73E   // Area fill (biru muda)

// Ikon besar putih untuk panel kiri
void drawBigSensorIcon(int cx, int cy, uint8_t type, uint16_t col) {
  switch (type) {
    case 0:  // Cloud (CO2)
      tft.fillCircle(cx - 12, cy + 3, 11, col);
      tft.fillCircle(cx + 12, cy + 3, 11, col);
      tft.fillCircle(cx - 1,  cy - 7, 13, col);
      tft.fillRect(cx - 20, cy + 3, 41, 11, col);
      break;
    case 1:  // Drop (DO)
      tft.fillTriangle(cx, cy - 18, cx - 13, cy + 5, cx + 13, cy + 5, col);
      tft.fillCircle(cx, cy + 6, 13, col);
      break;
    case 2:  // Thermometer (Suhu)
      tft.fillRoundRect(cx - 5, cy - 16, 10, 22, 5, col);
      tft.fillCircle(cx, cy + 10, 9, col);
      break;
    case 3:  // Weight / timbangan (pH)
      tft.fillRoundRect(cx - 6, cy - 16, 12, 8, 3, col);          // knob atas
      tft.fillTriangle(cx - 17, cy + 14, cx - 9, cy - 7, cx + 9, cy - 7, col);
      tft.fillTriangle(cx - 17, cy + 14, cx + 17, cy + 14, cx + 9, cy - 7, col);
      break;
    case 4:  // Wind (O2 / udara)
      // 3 garis angin horizontal dengan ujung melengkung (hook)
      tft.fillRoundRect(cx - 16, cy - 11, 24, 5, 2, col);
      tft.fillCircle(cx + 11, cy - 8, 5, col);
      tft.fillCircle(cx + 11, cy - 8, 2, COLOR_ICON_CIRCLE);
      tft.fillRoundRect(cx - 16, cy - 2, 30, 5, 2, col);
      tft.fillRoundRect(cx - 16, cy + 7, 20, 5, 2, col);
      tft.fillCircle(cx + 5, cy + 10, 5, col);
      tft.fillCircle(cx + 5, cy + 10, 2, COLOR_ICON_CIRCLE);
      break;
    case 5:  // Layers / tumpukan (PM10)
      // 3 belah ketupat bertumpuk
      tft.fillTriangle(cx, cy - 16, cx - 18, cy - 6, cx + 18, cy - 6, col);
      tft.fillTriangle(cx, cy + 4,  cx - 18, cy - 6, cx + 18, cy - 6, col);
      tft.fillTriangle(cx, cy - 4,  cx - 18, cy + 4, cx + 18, cy + 4, col);
      tft.fillTriangle(cx, cy + 14, cx - 18, cy + 4, cx + 18, cy + 4, col);
      // garis pemisah (warna lingkaran) agar terlihat bertumpuk
      tft.drawLine(cx - 18, cy - 6, cx, cy + 3, COLOR_ICON_CIRCLE);
      tft.drawLine(cx + 18, cy - 6, cx, cy + 3, COLOR_ICON_CIRCLE);
      break;
    case 6:  // Molekul / share (VOC)
      tft.drawLine(cx - 12, cy - 10, cx + 11, cy + 8, col);
      tft.drawLine(cx - 12, cy - 10, cx + 11, cy - 9, col);
      tft.fillCircle(cx - 13, cy - 10, 5, col);   // simpul kiri
      tft.fillCircle(cx + 12, cy - 9,  5, col);    // simpul kanan atas
      tft.fillCircle(cx + 12, cy + 9,  5, col);    // simpul kanan bawah
      break;
    case 7:  // Waves / gelombang (Turbidity)
      for (int wy = -10; wy <= 10; wy += 9) {
        for (int wx = -16; wx <= 6; wx += 11) {
          tft.drawLine(wx + cx,      cy + wy,     wx + cx + 5,  cy + wy - 4, col);
          tft.drawLine(wx + cx + 5,  cy + wy - 4, wx + cx + 11, cy + wy,     col);
          tft.drawLine(wx + cx,      cy + wy + 1, wx + cx + 5,  cy + wy - 3, col);
          tft.drawLine(wx + cx + 5,  cy + wy - 3, wx + cx + 11, cy + wy + 1, col);
        }
      }
      break;
  }
}

void showChartDetail() {
  if (tft.getTouch(&touchX, &touchY, 200)) {
    uint16_t dx = (touchX <= 480) ? (480 - touchX) : 0;
    uint16_t dy = touchY;
    // Tombol "< Kembali" di pojok kiri-atas panel gelap
    if (dx <= 170 && dy <= 55) {
      currentState = HOME;
      screenDrawn = false;
      delay(200);
      return;
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
  // ----- Data per sensor -----
  // Udara (tema hijau)
  static const float dO2[12]   = {20.6,20.75,21.0,21.2,21.42,21.28,21.1,20.92,20.9,21.08,21.12,21.0};
  static const float dPM25[12] = {6.0,7.0,8.0,9.0,10.0,10.0,8.0,7.0,8.0,9.0,8.0,8.0};
  static const float dPM10[12] = {20,22,25,28,30,28,26,23,25,27,26,25};
  static const float dHCHO[12] = {0.02,0.02,0.03,0.04,0.04,0.04,0.03,0.03,0.03,0.04,0.03,0.03};
  static const float dVOC[12]  = {48,52,60,65,68,64,59,55,57,62,60,58};
  static const float dHUM[12]  = {47,49,52,55,57,55,53,50,52,54,53,52};
  // Air (tema biru)
  static const float dDO[12]   = {10.1,10.6,11.2,11.8,12.3,12.0,11.4,10.8,11.2,11.6,11.4,11.2};
  static const float dSUHU[12] = {25,26,27.5,28.5,29,28,26.5,26,27,28,27.5,27};
  static const float dTURB[12] = {2.7,3.2,4.1,5.0,5.5,5.0,4.3,3.7,4.1,4.6,4.3,4.1};

  const float* data;
  const char *title, *subtitle, *bigValue, *unit, *trend;
  const char *statMin, *statAvg, *statMax;
  const char* yLabels[5];
  float yMin, yMax;
  bool trendUp, isDegree = false, isWater = false;
  uint8_t iconType;

  switch (s) {
    // ---------- SENSOR AIR (tema biru) ----------
    case 1:  // DO
      data = dDO; title = "DO Monitor"; subtitle = "Oksigen Terlarut";
      bigValue = "11.2"; unit = "mg/L"; trend = "2.3%"; trendUp = true; iconType = 1; isWater = true;
      yMin = 9.0; yMax = 13.0;
      yLabels[0]="9.0"; yLabels[1]="10.0"; yLabels[2]="11.0"; yLabels[3]="12.0"; yLabels[4]="13.0";
      statMin = "10.1"; statAvg = "11.3"; statMax = "12.3";
      break;
    case 2:  // Suhu Air (tema biru, sensor air)
      data = dSUHU; title = "Suhu Air Monitor"; subtitle = "Temperatur Air";
      bigValue = "27"; unit = "C"; trend = "0.7%"; trendUp = false; iconType = 2; isDegree = true; isWater = true;
      yMin = 24.0; yMax = 32.0;
      yLabels[0]="24"; yLabels[1]="26"; yLabels[2]="28"; yLabels[3]="30"; yLabels[4]="32";
      statMin = "25"; statAvg = "27"; statMax = "29";
      break;
    case 3:  // Turbidity
      data = dTURB; title = "Turbidity Monitor"; subtitle = "Kekeruhan Air";
      bigValue = "4.1"; unit = "NTU"; trend = "4.0%"; trendUp = false; iconType = 7; isWater = true;
      yMin = 2.0; yMax = 6.0;
      yLabels[0]="2.0"; yLabels[1]="3.0"; yLabels[2]="4.0"; yLabels[3]="5.0"; yLabels[4]="6.0";
      statMin = "2.7"; statAvg = "4.2"; statMax = "5.5";
      break;
    // ---------- SENSOR UDARA (tema hijau) ----------
    case 5:  // PM2.5
      data = dPM25; title = "PM2.5 Monitor"; subtitle = "Partikulat Halus";
      bigValue = "8"; unit = "ug/m3"; trend = "5.2%"; trendUp = false; iconType = 0;
      yMin = 4.0; yMax = 12.0;
      yLabels[0]="4"; yLabels[1]="6"; yLabels[2]="8"; yLabels[3]="10"; yLabels[4]="12";
      statMin = "6"; statAvg = "8"; statMax = "10";
      break;
    case 6:  // PM10
      data = dPM10; title = "PM10 Monitor"; subtitle = "Partikulat Kasar";
      bigValue = "25"; unit = "ug/m3"; trend = "3.4%"; trendUp = true; iconType = 5;
      yMin = 15.0; yMax = 35.0;
      yLabels[0]="15"; yLabels[1]="20"; yLabels[2]="25"; yLabels[3]="30"; yLabels[4]="35";
      statMin = "20"; statAvg = "25"; statMax = "30";
      break;
    case 7:  // HCHO
      data = dHCHO; title = "HCHO Monitor"; subtitle = "Formaldehida";
      bigValue = "0.03"; unit = "ppm"; trend = "2.1%"; trendUp = false; iconType = 3;
      yMin = 0.01; yMax = 0.05;
      yLabels[0]="0.01"; yLabels[1]="0.02"; yLabels[2]="0.03"; yLabels[3]="0.04"; yLabels[4]="0.05";
      statMin = "0.02"; statAvg = "0.03"; statMax = "0.04";
      break;
    case 8:  // VOC
      data = dVOC; title = "VOC Monitor"; subtitle = "Senyawa Organik";
      bigValue = "58"; unit = "ppb"; trend = "1.6%"; trendUp = true; iconType = 6;
      yMin = 40.0; yMax = 80.0;
      yLabels[0]="40"; yLabels[1]="50"; yLabels[2]="60"; yLabels[3]="70"; yLabels[4]="80";
      statMin = "48"; statAvg = "59"; statMax = "68";
      break;
    case 9:  // Humidity
      data = dHUM; title = "Humidity Monitor"; subtitle = "Kelembapan Udara";
      bigValue = "52"; unit = "%"; trend = "0.9%"; trendUp = true; iconType = 1;
      yMin = 45.0; yMax = 65.0;
      yLabels[0]="45"; yLabels[1]="50"; yLabels[2]="55"; yLabels[3]="60"; yLabels[4]="65";
      statMin = "47"; statAvg = "52"; statMax = "57";
      break;
    default: // 4 = O2
      data = dO2; title = "O2 Monitor"; subtitle = "Oksigen";
      bigValue = "21"; unit = "%"; trend = "0.8%"; trendUp = true; iconType = 4;
      yMin = 20.0; yMax = 22.0;
      yLabels[0]="20.0"; yLabels[1]="20.5"; yLabels[2]="21.0"; yLabels[3]="21.5"; yLabels[4]="22.0";
      statMin = "20.6"; statAvg = "21.0"; statMax = "21.4";
      break;
  }

  // ----- Pilih palet sesuai tema (udara=hijau, air=biru) -----
  uint16_t thAccent  = isWater ? COLOR_ACCENT_BLUE       : COLOR_ACCENT_GREEN;
  uint16_t thMutedLt = isWater ? COLOR_MUTED_LIGHT_BLUE  : COLOR_MUTED_LIGHT;
  uint16_t thDark    = isWater ? COLOR_DARK_BLUE         : COLOR_DARK_GREEN;
  uint16_t thMuted   = isWater ? COLOR_MUTED_BLUE        : COLOR_MUTED_GREEN;
  uint16_t thBgRight = isWater ? COLOR_BG_BLUE           : COLOR_BG_MENU;
  uint16_t thLine    = isWater ? COLOR_CHART_LINE_BLUE   : COLOR_CHART_LINE;
  uint16_t thFill    = isWater ? COLOR_CHART_FILL_BLUE   : COLOR_CHART_FILL;
  uint16_t thLight   = isWater ? COLOR_LIGHT_BLUE        : COLOR_LIGHT_GREEN;

  // ===================== PANEL KIRI (accent: hijau/biru) =====================
  tft.fillRect(0, 0, 184, 320, thAccent);

  // Tombol "< Kembali"
  tft.drawLine(20, 19, 12, 26, 0xFFFF);
  tft.drawLine(12, 26, 20, 33, 0xFFFF);
  tft.drawLine(21, 19, 13, 26, 0xFFFF);
  tft.drawLine(13, 26, 21, 33, 0xFFFF);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(0xFFFF);
  tft.drawString("Kembali", 30, 26, 2);

  // Lingkaran ikon putih + ikon accent (seperti card home)
  int icx = 92, icy = 92;
  tft.fillCircle(icx, icy, 32, COLOR_ICON_CIRCLE);
  drawBigSensorIcon(icx, icy, iconType, thAccent);

  // Judul
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xFFFF);
  tft.drawString(title, 92, 144, 4);
  // Subjudul
  tft.setTextColor(thMutedLt);
  tft.drawString(subtitle, 92, 168, 2);

  // Nilai besar
  tft.setTextColor(0xFFFF);
  tft.drawString(bigValue, 92, 204, 6);
  if (isDegree) {
    int w = tft.textWidth(bigValue, 6);
    int rx = 92 + w / 2 + 9;
    tft.drawCircle(rx, 190, 5, 0xFFFF);
    tft.drawCircle(rx, 190, 4, 0xFFFF);
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

  // Pill tren: pill terang + panah/teks beraksen (sesuai desain)
  int pw = 88, ph = 24, px = 92 - pw / 2, py = 252;
  uint16_t pillBg   = thLight;
  uint16_t trendCol = thAccent;
  tft.fillRoundRect(px, py, pw, ph, ph / 2, pillBg);
  int tx = px + 22, ty = py + ph / 2;
  if (trendUp) tft.fillTriangle(tx, ty - 5, tx - 5, ty + 4, tx + 5, ty + 4, trendCol);
  else         tft.fillTriangle(tx, ty + 5, tx - 5, ty - 4, tx + 5, ty - 4, trendCol);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(trendCol);
  tft.drawString(trend, tx + 12, ty, 2);

  // Indikator real-time
  tft.fillCircle(40, 296, 4, thMutedLt);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(0xFFFF);
  tft.drawString("Real-time", 52, 296, 2);

  // ===================== PANEL KANAN (background terang sesuai tema) =====================
  tft.fillRect(184, 0, 296, 320, thBgRight);

  // Kartu grafik
  drawRoundedCard(190, 8, 284, 248, 14, 0xFFFF);

  // Header
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(thDark);
  tft.drawString("Tren 12 Jam Terakhir", 202, 18, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(thMuted);
  tft.drawString("06:00-17:00", 462, 18, 2);
  tft.drawFastHLine(202, 40, 260, COLOR_BORDER_LIGHT);

  // Area plot
  int pL = 232, pR = 462, pT = 56, pB = 222;

  // Gridline + label Y
  for (int i = 0; i < 5; i++) {
    int gy = pB - (pB - pT) * i / 4;
    for (int x = pL; x < pR; x += 8) tft.drawFastHLine(x, gy, 4, COLOR_BORDER_LIGHT);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(thMuted);
    tft.drawString(yLabels[i], pL - 6, gy, 1);
  }

  // Konversi data ke pixel
  int xp[12], yp[12];
  for (int i = 0; i < 12; i++) {
    xp[i] = pL + (pR - pL) * i / 11;
    float t = (data[i] - yMin) / (yMax - yMin);
    if (t < 0) t = 0; if (t > 1) t = 1;
    yp[i] = pB - (int)(t * (pB - pT));
  }

  // Area fill di bawah kurva (interpolasi linear antar titik)
  for (int i = 0; i < 11; i++) {
    int x0 = xp[i], x1 = xp[i + 1];
    for (int x = x0; x <= x1; x++) {
      float f = (x1 == x0) ? 0 : (float)(x - x0) / (x1 - x0);
      int y = yp[i] + (int)((yp[i + 1] - yp[i]) * f);
      if (y < pB) tft.drawFastVLine(x, y, pB - y, thFill);
    }
  }

  // Garis kurva (tebal 3px)
  for (int i = 0; i < 11; i++) {
    tft.drawLine(xp[i], yp[i],     xp[i + 1], yp[i + 1],     thLine);
    tft.drawLine(xp[i], yp[i] - 1, xp[i + 1], yp[i + 1] - 1, thLine);
    tft.drawLine(xp[i], yp[i] + 1, xp[i + 1], yp[i + 1] + 1, thLine);
  }

  // Marker titik
  for (int i = 0; i < 12; i++) {
    if (i == 11) {
      tft.fillCircle(xp[i], yp[i], 7, thLight);
      tft.fillCircle(xp[i], yp[i], 4, thLine);
    } else {
      tft.fillCircle(xp[i], yp[i], 3, 0xFFFF);
      tft.drawCircle(xp[i], yp[i], 3, thLine);
    }
  }

  // Label X
  const char* xl[4] = {"06:00", "09:00", "12:00", "15:00"};
  int xi[4] = {0, 3, 6, 9};
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(thMuted);
  for (int k = 0; k < 4; k++) tft.drawString(xl[k], xp[xi[k]], pB + 8, 1);

  // ===================== KARTU STATISTIK =====================
  int sy = 260, sh = 54;
  const char* slab[3] = {"Min", "Avg", "Max"};
  const char* sval[3] = {statMin, statAvg, statMax};
  int sx[3] = {190, 287, 384};
  for (int k = 0; k < 3; k++) {
    drawRoundedCard(sx[k], sy, 90, sh, 10, thLight);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(thMuted);
    tft.drawString(slab[k], sx[k] + 45, sy + 8, 2);
    tft.setTextColor(thDark);
    tft.drawString(sval[k], sx[k] + 45, sy + 26, 4);
  }
}