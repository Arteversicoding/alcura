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

// Colors matching SVG (RGB565)
#define COLOR_BG           TFT_WHITE
#define COLOR_TEXT         0x2965   // #2C2C2A
#define COLOR_SUBTITLE     0x8C30   // #888780
#define COLOR_ACCENT       0xFC68   // #FF8C42
#define COLOR_HOME         0xDAC6   // #D85A30
#define COLOR_SETTINGS     0x1CEE   // #1D9E75
#define COLOR_INFO         0x345B   // #378ADD
#define COLOR_ABOUT        0xD4A035 // Gold
#define COLOR_PILL         0xD698   // #D3D1C7
#define COLOR_BATTERY_FILL 0x1CEE   // #1D9E75

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

// ==================== SPLASH SCREEN - ALCURA FULL SCREEN 320x480 ====================
void showSplashScreen() {
  unsigned long elapsedTime = millis() - splashStartTime;
  int splashDuration = 3500;
  float progress = min((float)elapsedTime / splashDuration, 1.0f);

  static bool splashDrawn = false;
  if (!splashDrawn) {
    // Draw FULL gradient background (dark blue → teal) - 320x480 MAKSIMAL
    for (int x = 0; x < 480; x++) {
      int colorValue = (int)(0x0E79 * (x / 480.0f));  // Gradient interpolation
      uint16_t color = x < 240 ? 0x0000 : colorValue;  // Dark blue to teal
      tft.drawLine(x, 0, x, 320, color);
    }
    splashDrawn = true;
  }

  // BIG LOGO - ALCURA text (MAKSIMAL)
  if (progress > 0.15f) {
    float textAlpha = min((progress - 0.15f) / 0.35f, 1.0f);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x05FF);  // Cyan

    // Large ALCURA text - MAKSIMAL SIZE
    tft.drawString("ALCURA", 240, 100, 7);  // Very large

    // Subtitle
    if (progress > 0.35f) {
      tft.setTextColor(0x05FF);
      tft.drawString("Display System", 240, 180, 4);
      tft.setTextColor(0xAA55);  // Light gray
      tft.drawString("v1.0", 240, 220, 2);
    }
  }

  // Animated progress bar (MAKSIMAL)
  if (progress > 0.5f) {
    int barWidth = (int)(300 * min((progress - 0.5f) / 0.5f, 1.0f));
    int barX = (480 - barWidth) / 2;

    // Background
    tft.fillRect(barX - 2, 270, barWidth + 4, 8, 0x2104);  // Dark

    // Progress fill
    tft.fillRect(barX, 272, barWidth, 4, 0x05FF);  // Cyan
  }

  // Loading text
  if (progress > 0.7f) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xAA55);
    tft.drawString("Loading...", 240, 300, 2);
  }

  if (elapsedTime >= splashDuration) {
    currentState = MENU;
    menuSelection = 0;
    screenDrawn = false;
    delay(200);
  }
}

// ==================== MAIN MENU ====================
void showMainMenu() {
  // TOUCH INPUT
  if (tft.getTouch(&touchX, &touchY)) {
    Serial.printf("Touch: X=%d, Y=%d\n", touchX, touchY);

    // Home card touch (left side, top-bottom)
    if (touchX < 240) {
      menuSelection = 0;  // Home
      delay(200);
    }
    // Settings card touch (right side, top)
    else if (touchX >= 240 && touchY < 160) {
      menuSelection = 1;  // Settings
      delay(200);
    }
    // Info card touch (right side, bottom)
    else if (touchX >= 240 && touchY >= 160) {
      menuSelection = 2;  // Info
      delay(200);
    }
  }

  // SERIAL INPUT (still available)
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

  // TOUCH - Double tap to select (automatic from card touch above)

  if (menuSelection == previousMenuSelection && screenDrawn) {
    return;
  }

  // Header
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT);
  tft.drawString("9:41", 18, 18, 2);

  // Battery
  tft.drawRoundRect(440, 14, 22, 11, 3, COLOR_SUBTITLE);
  tft.fillRoundRect(442, 16, 15, 7, 1, COLOR_BATTERY_FILL);
  tft.fillRect(463, 17, 2, 5, COLOR_SUBTITLE);

  // Title
  tft.setTextColor(COLOR_TEXT);
  tft.drawString("Menu", 20, 45, 4);
  tft.setTextColor(COLOR_SUBTITLE);
  tft.drawString("Pilih salah satu menu", 20, 68, 2);

  // Cards
  drawHomeCard(16, 96, 220, 196, menuSelection == 0);
  drawSettingCard(252, 96, 212, 92, menuSelection == 1);
  drawInfoCard(252, 200, 212, 92, menuSelection == 2);

  // Bottom pill
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
  drawRoundedCard(x, y, w, h, 22, COLOR_HOME);
  if (selected) {
    tft.drawRoundRect(x, y, w, h, 22, COLOR_ACCENT);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 22, COLOR_ACCENT);
  }

  int cx = x + w / 2;
  int cy = y + 62;
  int r = 38;
  tft.fillCircle(cx, cy, r, TFT_WHITE);

  // Home icon
  uint16_t c = COLOR_HOME;
  tft.drawLine(cx - 18, cy + 2, cx, cy - 18, c);
  tft.drawLine(cx, cy - 18, cx + 18, cy + 2, c);
  tft.drawRect(cx - 13, cy + 2, 26, 20, c);
  tft.fillRect(cx - 5, cy + 10, 10, 12, TFT_WHITE);
  tft.drawRect(cx - 5, cy + 10, 10, 12, c);

  // Text
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Home", cx, y + 136, 4);
  tft.drawString("Halaman utama", cx, y + 160, 2);
}

void drawSettingCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, 22, COLOR_SETTINGS);
  if (selected) {
    tft.drawRoundRect(x, y, w, h, 22, COLOR_ACCENT);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 22, COLOR_ACCENT);
  }

  int cx = x + 48;
  int cy = y + 46;
  int r = 30;
  tft.fillCircle(cx, cy, r, TFT_WHITE);

  // Setting icon
  uint16_t c = COLOR_SETTINGS;
  tft.drawLine(cx - 13, cy - 8, cx + 13, cy - 8, c);
  tft.drawLine(cx - 13, cy,     cx + 13, cy,     c);
  tft.drawLine(cx - 13, cy + 8, cx + 13, cy + 8, c);
  tft.fillCircle(cx + 5, cy - 8, 3, c);
  tft.fillCircle(cx - 7, cy,     3, c);
  tft.fillCircle(cx + 7, cy + 8, 3, c);

  // Text
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Setting", cx + 44, cy - 8, 4);
}

void drawInfoCard(int x, int y, int w, int h, bool selected) {
  drawRoundedCard(x, y, w, h, 22, COLOR_INFO);
  if (selected) {
    tft.drawRoundRect(x, y, w, h, 22, COLOR_ACCENT);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 22, COLOR_ACCENT);
  }

  int cx = x + 48;
  int cy = y + 46;
  int r = 30;
  tft.fillCircle(cx, cy, r, TFT_WHITE);

  // Info icon
  uint16_t c = COLOR_INFO;
  tft.fillCircle(cx, cy - 12, 4, c);
  tft.fillRoundRect(cx - 3, cy - 5, 7, 18, 3, c);

  // Text
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
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
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_HOME);
  tft.drawString("Home", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT);
  int y = 80;
  tft.drawString("Welcome!", 20, y, 2); y += 40;
  tft.drawString("ACURA System", 20, y, 2); y += 40;
  tft.drawString("v1.0", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_ACCENT);
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
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_SETTINGS);
  tft.drawString("Settings", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT);
  int y = 80;
  tft.drawString("Brightness: 100%", 20, y, 2); y += 40;
  tft.drawString("Rotation: Landscape", 20, y, 2); y += 40;
  tft.drawString("Timer: 5 min", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_ACCENT);
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
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_INFO);
  tft.drawString("Info", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT);
  int y = 80;
  tft.drawString("Device: ESP32-035", 20, y, 2); y += 40;
  tft.drawString("Resolution: 480x320", 20, y, 2); y += 40;
  tft.drawString("Uptime: " + String(millis()/1000) + "s", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_ACCENT);
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
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_ABOUT);
  tft.drawString("About", 20, 20, 4);
  tft.setTextColor(COLOR_TEXT);
  int y = 80;
  tft.drawString("ACURA System", 20, y, 2); y += 40;
  tft.drawString("Version 1.0", 20, y, 2); y += 40;
  tft.drawString("ESP32-035 TFT", 20, y, 2); y += 40;
  tft.drawString("Made with", 20, y, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_ACCENT);
  tft.drawString("Press B to Back", 240, 300, 2);
}
