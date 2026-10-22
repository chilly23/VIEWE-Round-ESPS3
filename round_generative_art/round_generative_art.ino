/*
  VIEWE-Round (ESP32-S3) - original generative wireframe/particle animations
  Inspired by the general style of the reference GIFs (node networks, particle
  bursts, scribble loops, wireframe clusters) - these are new, independently
  generated patterns, not reproductions of any specific piece.

  0 = NodeNetwork    - rotating torus-knot point cloud with nearby-point mesh lines
  1 = Starburst      - center rays pulsing outward + twinkling background dots
  2 = StarNetwork    - rotating square nodes joined by crossing star lines
  3 = ScribbleLoop   - closed multi-harmonic loop curve with sparse mesh lines
  4 = PolyhedraCluster - cluster of small rotating wireframe spheres

  Each reset (RST) or BOOT-button press advances to the next animation; the
  index is stored in flash (Preferences) so it survives power loss.

  Library needed: GFX Library for Arduino (moononournation)
*/

#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <math.h>

// ===================== USER CONFIG =====================
#define SCREEN_W 240
#define SCREEN_H 240
#define NUM_ANIMS 5

// Display pins - PLACEHOLDERS, fill in from the VIEWE-Round V1.1 schematic.
#define PIN_DC   -1
#define PIN_CS   -1
#define PIN_SCK  -1
#define PIN_MOSI -1
#define PIN_RST  -1
#define PIN_BL   -1

#define BOOT_BTN_PIN 0
// =======================================================

Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *panel = new Arduino_GC9A01(bus, PIN_RST, 0, true);
Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel);

Preferences prefs;
int animIndex = 0;
const int CX = SCREEN_W / 2, CY = SCREEN_H / 2;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void rotProject(float x, float y, float z, float ay, float ax, float scale,
                 float &sx, float &sy, float &depth) {
  float xz = x * cosf(ay) + z * sinf(ay);
  float zz = -x * sinf(ay) + z * cosf(ay);
  float yz = y * cosf(ax) - zz * sinf(ax);
  float zz2 = y * sinf(ax) + zz * cosf(ax);
  float persp = 260.0f / (260.0f + zz2);
  sx = CX + xz * scale * persp;
  sy = CY + yz * scale * persp;
  depth = zz2;
}

// ================= 0: NodeNetwork (torus knot) =================
#define NA 40
float baseA[NA][3];
void initNodeNetwork() {
  for (int i = 0; i < NA; i++) {
    float t = (float)i / NA * 2 * PI;
    float R = 55, r = 28, p = 2, q = 3;
    baseA[i][0] = (R + r * cosf(q * t)) * cosf(p * t);
    baseA[i][1] = (R + r * cosf(q * t)) * sinf(p * t);
    baseA[i][2] = r * sinf(q * t);
  }
}
void animNodeNetwork(uint32_t t) {
  canvas->fillScreen(BLACK);
  float ay = t / 1800.0f, ax = 0.5f + 0.2f * sinf(t / 2600.0f);
  float sx[NA], sy[NA];
  for (int i = 0; i < NA; i++) {
    float d;
    rotProject(baseA[i][0], baseA[i][1], baseA[i][2], ay, ax, 1.15f, sx[i], sy[i], d);
  }
  for (int i = 0; i < NA; i++)
    for (int j = i + 1; j < NA; j++) {
      float dx = sx[i] - sx[j], dy = sy[i] - sy[j];
      float dist2 = dx * dx + dy * dy;
      if (dist2 < 38 * 38) {
        uint8_t bright = (uint8_t)(255 - sqrtf(dist2) * 4);
        canvas->drawLine(sx[i], sy[i], sx[j], sy[j], rgb565(bright / 2, bright / 3, bright));
      }
    }
  for (int i = 0; i < NA; i++) {
    uint16_t col = (i % 3 == 0) ? rgb565(255, 170, 220) : rgb565(210, 220, 255);
    canvas->fillCircle(sx[i], sy[i], 2, col);
  }
}

// ================= 1: Starburst =================
#define NB_RAYS 14
#define NB_STARS 10
float starX[NB_STARS], starY[NB_STARS], starPhase[NB_STARS];
void initStarburst() {
  for (int i = 0; i < NB_STARS; i++) {
    float a = i * 2 * PI / NB_STARS + 0.3f;
    float r = 90 + (i % 3) * 10;
    starX[i] = CX + r * cosf(a);
    starY[i] = CY + r * sinf(a);
    starPhase[i] = i * 0.7f;
  }
}
void animStarburst(uint32_t t) {
  canvas->fillScreen(BLACK);
  for (int i = 0; i < NB_STARS; i++) {
    float b = 0.5f + 0.5f * sinf(t / 400.0f + starPhase[i]);
    uint8_t v = (uint8_t)(120 + 120 * b);
    canvas->fillCircle(starX[i], starY[i], 1 + (b > 0.8f ? 1 : 0), rgb565(v, v, v));
  }
  float rot = t / 6000.0f;
  for (int i = 0; i < NB_RAYS; i++) {
    float a = i * 2 * PI / NB_RAYS + rot;
    float pulse = 0.5f + 0.5f * sinf(t / 260.0f + i * 1.1f);
    float len = 15 + 85 * pulse;
    uint8_t v = (uint8_t)(140 + 115 * pulse);
    float ex = CX + len * cosf(a), ey = CY + len * sinf(a);
    canvas->drawLine(CX, CY, ex, ey, rgb565(v, v, v));
  }
  float core = 6 + 4 * sinf(t / 200.0f);
  canvas->fillCircle(CX, CY, core, WHITE);
}

// ================= 2: StarNetwork =================
#define NC 9
float baseCx[NC], baseCy[NC];
void initStarNetwork() {
  float offx[NC] = { -70, -35, -50, -20, 5, 55, 30, -55, 60 };
  float offy[NC] = { -30, -15, 5, -5, -25, -35, 40, 60, 65 };
  for (int i = 0; i < NC; i++) { baseCx[i] = offx[i]; baseCy[i] = offy[i]; }
}
void animStarNetwork(uint32_t t) {
  canvas->fillScreen(BLACK);
  float ang = t / 5000.0f;
  float ca = cosf(ang), sa = sinf(ang);
  float sx[NC], sy[NC];
  for (int i = 0; i < NC; i++) {
    sx[i] = CX + baseCx[i] * ca - baseCy[i] * sa;
    sy[i] = CY + baseCx[i] * sa + baseCy[i] * ca;
  }
  for (int i = 0; i < NC; i++) {
    canvas->drawLine(sx[i], sy[i], sx[(i + 4) % NC], sy[(i + 4) % NC], rgb565(220, 220, 220));
    canvas->drawLine(sx[i], sy[i], sx[(i + 2) % NC], sy[(i + 2) % NC], rgb565(150, 150, 150));
  }
  for (int i = 0; i < NC; i++) {
    int s = (i % 3 == 0) ? 9 : 5;
    canvas->fillRect(sx[i] - s / 2, sy[i] - s / 2, s, s, WHITE);
  }
}

// ================= 3: ScribbleLoop =================
#define ND 160
float baseDx[ND], baseDy[ND];
void initScribbleLoop() {
  for (int i = 0; i < ND; i++) {
    float th = (float)i / ND * 2 * PI;
    float rad = 65 + 16 * sinf(3 * th + 1.0f) + 11 * sinf(5 * th + 2.1f) + 7 * sinf(8 * th + 0.4f);
    baseDx[i] = rad * cosf(th);
    baseDy[i] = rad * sinf(th);
  }
}
void animScribbleLoop(uint32_t t) {
  canvas->fillScreen(BLACK);
  float ang = t / 9000.0f;
  float ca = cosf(ang), sa = sinf(ang);
  float sx[ND], sy[ND];
  for (int i = 0; i < ND; i++) {
    sx[i] = CX + baseDx[i] * ca - baseDy[i] * sa;
    sy[i] = CY + baseDx[i] * sa + baseDy[i] * ca;
  }
  for (int i = 0; i < ND; i++)
    canvas->drawLine(sx[i], sy[i], sx[(i + 1) % ND], sy[(i + 1) % ND], rgb565(200, 200, 200));
  for (int i = 0; i < ND; i += 5)
    for (int j = i + 15; j < ND; j += 5) {
      float dx = sx[i] - sx[j], dy = sy[i] - sy[j];
      if (dx * dx + dy * dy < 30 * 30)
        canvas->drawLine(sx[i], sy[i], sx[j], sy[j], rgb565(90, 90, 90));
    }
}

// ================= 4: PolyhedraCluster =================
#define NE_CLUSTERS 5
#define NE_PTS 10
float baseEx[NE_CLUSTERS][NE_PTS][3];
float clusterOff[NE_CLUSTERS][3] = {
  { -35, 10, 0 }, { 30, 0, 10 }, { 0, -30, -10 }, { 40, 30, -15 }, { -20, 35, 15 }
};
float clusterR[NE_CLUSTERS] = { 42, 38, 30, 26, 24 };
void initPolyhedraCluster() {
  for (int c = 0; c < NE_CLUSTERS; c++) {
    for (int i = 0; i < NE_PTS; i++) {
      float phi = acosf(1 - 2.0f * (i + 0.5f) / NE_PTS);
      float theta = PI * (1 + sqrtf(5.0f)) * i;
      baseEx[c][i][0] = clusterR[c] * sinf(phi) * cosf(theta);
      baseEx[c][i][1] = clusterR[c] * sinf(phi) * sinf(theta);
      baseEx[c][i][2] = clusterR[c] * cosf(phi);
    }
  }
}
void animPolyhedraCluster(uint32_t t) {
  canvas->fillScreen(BLACK);
  float ay = t / 4000.0f, ax = 0.4f;
  for (int c = 0; c < NE_CLUSTERS; c++) {
    float sx[NE_PTS], sy[NE_PTS];
    for (int i = 0; i < NE_PTS; i++) {
      float d;
      rotProject(baseEx[c][i][0] + clusterOff[c][0],
                 baseEx[c][i][1] + clusterOff[c][1],
                 baseEx[c][i][2] + clusterOff[c][2], ay, ax, 1.05f, sx[i], sy[i], d);
    }
    for (int i = 0; i < NE_PTS; i++) {
      canvas->drawLine(sx[i], sy[i], sx[(i + 1) % NE_PTS], sy[(i + 1) % NE_PTS], rgb565(230, 230, 230));
      canvas->drawLine(sx[i], sy[i], sx[(i + 3) % NE_PTS], sy[(i + 3) % NE_PTS], rgb565(120, 120, 120));
    }
  }
}

typedef void (*AnimFn)(uint32_t);
AnimFn anims[NUM_ANIMS] = {
  animNodeNetwork, animStarburst, animStarNetwork, animScribbleLoop, animPolyhedraCluster
};

void advanceAndReboot() {
  prefs.begin("genart", false);
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

  prefs.begin("genart", false);
  animIndex = prefs.getInt("idx", 0);
  prefs.end();

  initNodeNetwork();
  initStarburst();
  initStarNetwork();
  initScribbleLoop();
  initPolyhedraCluster();

  if (!canvas->begin()) { Serial.println("Display init failed"); while (1) delay(1000); }
}

void loop() {
  uint32_t t0 = millis();
  while (true) {
    anims[animIndex](millis() - t0);
    canvas->flush();
    if (digitalRead(BOOT_BTN_PIN) == LOW) {
      delay(30);
      if (digitalRead(BOOT_BTN_PIN) == LOW) advanceAndReboot();
    }
    delay(16);
  }
}
