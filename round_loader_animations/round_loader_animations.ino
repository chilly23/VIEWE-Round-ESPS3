/*
  VIEWE-Round (ESP32-S3) - CSS loader animations, redrawn with 2D primitives

  CSS masks/clip-paths/3D transforms have no direct GFX equivalent, so each
  animation below is a redraw, not a pixel-exact port:
    0 = contrast-mask triangle spinner -> rotating glowing triangle, hue-cycled
    1 = ghost grid                     -> Pac-Man-style ghost: bounce, moving
                                           eyes, flickering skirt, shadow
    2 = hamster wheel                  -> rotating spoked wheel + hamster blob
    3 = psychedelic ring loader        -> concentric rotating rings, cyan/yellow
    4 = "cercle" bouncing dots         -> 4 bouncing dots (near-exact port)
    5 = pyramid loader                 -> rotating diamond (width*cos(angle)
                                           fakes the 3D spin) + pulsing shadow

  Each hardware reset (RST button) or BOOT-button press advances to the next
  animation and stays on it until the next reset. Index is stored in flash
  (Preferences/NVS) so it survives power loss.

  Library needed: GFX Library for Arduino (moononournation)
  Board settings: ESP32S3 Dev Module, PSRAM enabled if available.
*/

#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <math.h>

constexpr uint16_t BLACK = RGB565_BLACK;
constexpr uint16_t WHITE = RGB565_WHITE;
constexpr uint16_t BLUE = RGB565_BLUE;

// ===================== USER CONFIG =====================
#define SCREEN_W 240
#define SCREEN_H 240
#define NUM_ANIMS 6

// VIEWE-ROUND-V1.1 CO5300 AMOLED, 466x466 QSPI.
#define PIN_CS   12
#define PIN_SCK  48
#define PIN_D0   13
#define PIN_D1   47
#define PIN_D2   21
#define PIN_D3   14
#define PIN_RST  11
#define PIN_BL   -1   // AMOLED has no separate backlight GPIO.

#define BOOT_BTN_PIN 0  // standard ESP32 BOOT button (active low)
// =======================================================

Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_CS, PIN_SCK, PIN_D0, PIN_D1, PIN_D2, PIN_D3);
Arduino_GFX *panel = new Arduino_CO5300(bus, PIN_RST, 0, 466, 466, 6, 0, 6, 0);
// Keep original geometry near the center with even-aligned AMOLED coordinates.
Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel, 112, 112);

Preferences prefs;
int animIndex = 0;
const int CX = SCREEN_W / 2, CY = SCREEN_H / 2;

// ---------- helpers ----------
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t hsv565(float h, float s, float v) {
  h = fmodf(h, 1.0f); if (h < 0) h += 1.0f;
  float r, g, b;
  int i = (int)(h * 6);
  float f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return rgb565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

void rotatePoint(float x, float y, float ang, float &ox, float &oy) {
  ox = x * cosf(ang) - y * sinf(ang);
  oy = x * sinf(ang) + y * cosf(ang);
}

// ---------- 0: contrast-mask triangle spinner ----------
void animMaskSpinner(uint32_t t) {
  canvas->fillScreen(BLACK);
  float ang = t / 900.0f;
  uint16_t glow = hsv565(0.08f - 0.08f * sinf(t / 1500.0f), 0.8f, 1.0f);
  for (int k = 0; k < 3; k++) {
    float a = ang + k * (2 * PI / 3);
    float x1, y1, x2, y2, x3, y3;
    rotatePoint(0, -55, a, x1, y1);
    rotatePoint(48, 35, a, x2, y2);
    rotatePoint(-48, 35, a, x3, y3);
    canvas->fillTriangle(CX + x1, CY + y1, CX + x2, CY + y2, CX + x3, CY + y3, glow);
  }
  canvas->fillCircle(CX, CY, 20, BLACK);
  canvas->drawCircle(CX, CY, 20, glow);
}

// ---------- 1: ghost ----------
void animGhost(uint32_t t) {
  canvas->fillScreen(BLACK);
  int bounce = ((t / 500) % 2 == 0) ? 0 : -10;
  int by = CY + bounce;

  bool flick = (t / 500) % 2 == 0;
  uint16_t red = rgb565(220, 0, 0);
  uint16_t redDim = rgb565(90, 0, 0);

  // shadow
  uint16_t shadowCol = flick ? rgb565(20, 20, 20) : rgb565(45, 45, 45);
  canvas->fillCircle(CX, CY + 85, 45, shadowCol);

  // body: dome + rectangle
  canvas->fillCircle(CX, by - 15, 50, red);
  canvas->fillRect(CX - 50, by - 15, 100, 55, red);

  // zigzag skirt (4 scallops)
  int skirtY = by + 40;
  for (int i = 0; i < 4; i++) {
    int x0 = CX - 50 + i * 25;
    uint16_t c = ((i % 2 == 0) == flick) ? red : redDim;
    canvas->fillTriangle(x0, skirtY - 15, x0 + 25, skirtY - 15, x0 + 12, skirtY + 15, c);
  }

  // eyes
  float eyeShift = sinf(t / 480.0f) > 0 ? 4 : 0;  // steps every ~3s in CSS; simplified wobble
  for (int side = -1; side <= 1; side += 2) {
    int ex = CX + side * 22;
    canvas->fillRoundRect(ex - 12, by - 25, 24, 30, 8, WHITE);
    canvas->fillCircle(ex + (int)eyeShift, by - 8, 8, BLUE);
  }
}

// ---------- 2: hamster wheel ----------
void animHamsterWheel(uint32_t t) {
  canvas->fillScreen(BLACK);
  float ang = t / 1000.0f * 2 * PI;
  int R = 95;

  canvas->drawCircle(CX, CY, R, rgb565(150, 150, 150));
  canvas->drawCircle(CX, CY, R - 1, rgb565(150, 150, 150));

  for (int i = 0; i < 8; i++) {
    float a = ang + i * (PI / 4);
    float x1, y1;
    rotatePoint(R - 3, 0, a, x1, y1);
    canvas->drawLine(CX, CY, CX + x1, CY + y1, rgb565(120, 120, 120));
  }

  // hamster body, gently bobbing (running in place)
  int hop = ((t / 250) % 2 == 0) ? 0 : -6;
  int hx = CX - 20, hy = CY + 35 + hop;
  canvas->fillRoundRect(hx, hy, 55, 30, 12, rgb565(230, 150, 60));
  canvas->fillCircle(hx - 5, hy + 5, 16, rgb565(230, 150, 60));  // head
  canvas->fillCircle(hx - 10, hy - 2, 3, BLACK);                  // eye

  // shadow
  canvas->fillCircle(CX, CY + 95, 40, rgb565(20, 20, 20));
}

// ---------- 3: psychedelic rings ----------
void animPsychedelicRings(uint32_t t) {
  canvas->fillScreen(BLACK);
  uint16_t cyan = rgb565(0, 255, 255);
  uint16_t yellow = rgb565(255, 255, 0);

  float a1 = t / 1400.0f, a2 = -t / 2200.0f, a3 = t / 700.0f;
  for (int k = 0; k < 3; k++) {
    float ang = a1 + k * (2 * PI / 3);
    float x1, y1, x2, y2, x3, y3;
    rotatePoint(0, -75, ang, x1, y1);
    rotatePoint(65, 45, ang, x2, y2);
    rotatePoint(-65, 45, ang, x3, y3);
    canvas->drawTriangle(CX + x1, CY + y1, CX + x2, CY + y2, CX + x3, CY + y3, cyan);
  }
  canvas->drawCircle(CX, CY, 68, cyan);
  for (int i = 0; i < 24; i++) {
    float ang = a2 + i * (2 * PI / 24);
    float x, y;
    rotatePoint(68, 0, ang, x, y);
    if (i % 2 == 0) canvas->drawPixel(CX + x, CY + y, cyan);
  }
  float pulse = 5 + 3 * sinf(a3);
  canvas->fillCircle(CX, CY, pulse, yellow);
}

// ---------- 4: bouncing dots ("cercle" loader) ----------
void animBounceDots(uint32_t t) {
  canvas->fillScreen(BLACK);
  struct Dot { int dx, dy, r; uint16_t color; float amp, speed, lo; };
  Dot dots[4] = {
    { -20, -10, 30, rgb565(0x3f, 0x7e, 0xe8),  5, 15.7f, 0 },   // blue, big
    {  20,  10, 12, rgb565(0xde, 0x40, 0x32),  5, 15.7f, 0 },   // red
    {  15,  45, 15, rgb565(0xee, 0xb2, 0x05), 8.5f, 15.7f, -1.5f }, // yellow
    {  45,   0,  6, rgb565(0x31, 0xaa, 0x52),  5, 15.7f, 0 },   // green
  };
  for (auto &d : dots) {
    float y = d.lo * 10 + d.amp * sinf(t / 1000.0f * d.speed);
    canvas->fillCircle(CX + d.dx, CY + d.dy + (int)y, d.r, d.color);
  }
}

// ---------- 5: pyramid (2D fake of the 3D spin) ----------
void animPyramid(uint32_t t) {
  canvas->fillScreen(BLACK);
  float ang = fmodf(t / 4000.0f, 1.0f) * 2 * PI;
  float scale = cosf(ang);
  int w = (int)(75 * fabsf(scale));
  if (w < 4) w = 4;
  int h = 80;
  uint16_t color = hsv565(scale >= 0 ? 0.55f : 0.83f, 0.7f, 1.0f);
  int apexY = CY - h / 2, baseY = CY + h / 2;
  canvas->fillTriangle(CX - w / 2, baseY, CX + w / 2, baseY, CX, apexY, color);
  int shadowW = 35 - (int)(15 * fabsf(sinf(ang)));
  canvas->fillCircle(CX, baseY + 18, shadowW, rgb565(90, 60, 140));
}

typedef void (*AnimFn)(uint32_t);
AnimFn anims[NUM_ANIMS] = {
  animMaskSpinner, animGhost, animHamsterWheel,
  animPsychedelicRings, animBounceDots, animPyramid
};

// ---------- boot-cycled index ----------
void advanceAndReboot() {
  prefs.begin("loader", false);
  int idx = prefs.getInt("idx", -1);
  idx = (idx + 1) % NUM_ANIMS;
  prefs.putInt("idx", idx);
  prefs.end();
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  if (PIN_BL >= 0) { pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, HIGH); }

  prefs.begin("loader", false);
  animIndex = prefs.getInt("idx", 0);
  if (animIndex < 0 || animIndex >= NUM_ANIMS) animIndex = 0;
  prefs.end();

  if (!canvas->begin(40000000)) { Serial.println("Display init failed"); while (1) delay(1000); }
  panel->fillScreen(BLACK);
}

void loop() {
  uint32_t t0 = millis();
  while (true) {
    anims[animIndex](millis() - t0);
    canvas->flush();

    // BOOT button also cycles, no need to touch the physical reset pin
    if (digitalRead(BOOT_BTN_PIN) == LOW) {
      delay(30);
      if (digitalRead(BOOT_BTN_PIN) == LOW) advanceAndReboot();
    }
    delay(16);
  }
}
