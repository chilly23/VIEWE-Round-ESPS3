/*
  VIEWE-Round-V1.1 / ESP32-S3-N16R8, 466x466 CO5300 AMOLED, QSPI.
  All 11 animations from earlier, now driving the real panel instead of a
  placeholder GC9A01/SPI config. Display bring-up (pins, QSPI init, flush)
  is adapted from the known-working touch-rings sketch for this board;
  the touch controller and its specific artwork are not reused here.

  Arduino Boards Manager: "esp32" by Espressif Systems, 3.3.0+.
  Board: ESP32S3 Dev Module. Flash: 16MB. PSRAM: OPI PSRAM.
  USB CDC On Boot: Enabled. No Library Manager installs required.

  BOOT button: advances to the next animation and remembers it in flash
  (survives power loss). Plain RST just restarts the one currently stored.

  0 ContrastSpinner  5 Pyramid           9  ScribbleLoop
  1 Ghost            6 NodeNetwork       10 PolyhedraCluster
  2 HamsterWheel     7 Starburst
  3 PsychedelicRings 8 StarNetwork
  4 BounceDots
*/

#include <Arduino.h>
#include <Preferences.h>
#include <esp_arduino_version.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error Select ESP32S3 Dev Module in Tools > Board.
#endif
#if ESP_ARDUINO_VERSION_MAJOR < 3
#error Install esp32 by Espressif Systems version 3.3.0 or newer.
#endif

// ----- confirmed-working hardware config -----
static constexpr int LCD_WIDTH = 466, LCD_HEIGHT = 466;
static constexpr int LCD_X_OFFSET = 6, LCD_Y_OFFSET = 0;
static constexpr int LCD_CS = 12, LCD_CLK = 48;
static constexpr int LCD_D0 = 13, LCD_D1 = 47, LCD_D2 = 21, LCD_D3 = 14;
static constexpr int LCD_RST = 11;
static constexpr int STRIPE_ROWS = 16;
static constexpr uint8_t BRIGHTNESS = 180;
#define BOOT_BTN_PIN 0
#define NUM_ANIMS 11

const int CX = LCD_WIDTH / 2, CY = LCD_HEIGHT / 2;
const float S = LCD_WIDTH / 240.0f;  // scale factor from the old 240px sketches

uint16_t *frame = nullptr, *stripe = nullptr;
esp_lcd_panel_io_handle_t io = nullptr;
SemaphoreHandle_t sent = nullptr;
Preferences prefs;
int animIndex = 0;

// ================= pixel-buffer drawing primitives =================
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static const uint16_t BLK = 0x0000, WHT = 0xFFFF, BLU = rgb565(30, 60, 255);

static inline void setPx(int x, int y, uint16_t c) {
  if ((unsigned)x < (unsigned)LCD_WIDTH && (unsigned)y < (unsigned)LCD_HEIGHT)
    frame[y * LCD_WIDTH + x] = c;
}
void fillScreenBuf(uint16_t c) { for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) frame[i] = c; }

void drawLineBuf(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    setPx(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void fillCircleBuf(int cx, int cy, int r, uint16_t c) {
  if (r < 0) return;
  for (int y = -r; y <= r; y++) {
    int xr = (int)sqrtf((float)(r * r - y * y));
    for (int x = -xr; x <= xr; x++) setPx(cx + x, cy + y, c);
  }
}
void drawCircleBuf(int cx, int cy, int r, uint16_t c) {
  int x = r, y = 0, err = 0;
  while (x >= y) {
    setPx(cx + x, cy + y, c); setPx(cx + y, cy + x, c);
    setPx(cx - y, cy + x, c); setPx(cx - x, cy + y, c);
    setPx(cx - x, cy - y, c); setPx(cx - y, cy - x, c);
    setPx(cx + y, cy - x, c); setPx(cx + x, cy - y, c);
    y += 1; err += 1 + 2 * y;
    if (2 * (err - x) + 1 > 0) { x -= 1; err += 1 - 2 * x; }
  }
}
void fillRectBuf(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) setPx(i, j, c);
}
void fillRoundRectBuf(int x, int y, int w, int h, int r, uint16_t c) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  fillRectBuf(x + r, y, w - 2 * r, h, c);
  fillRectBuf(x, y + r, w, h - 2 * r, c);
  fillCircleBuf(x + r, y + r, r, c);
  fillCircleBuf(x + w - r - 1, y + r, r, c);
  fillCircleBuf(x + r, y + h - r - 1, r, c);
  fillCircleBuf(x + w - r - 1, y + h - r - 1, r, c);
}
void fillTriangleBuf(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  int t;
  if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y0 == y2) {
    int lo = x0, hi = x0;
    if (x1 < lo) lo = x1; if (x1 > hi) hi = x1;
    if (x2 < lo) lo = x2; if (x2 > hi) hi = x2;
    for (int x = lo; x <= hi; x++) setPx(x, y0, c);
    return;
  }
  for (int y = y0; y <= y2; y++) {
    bool second = y > y1;
    int xa = x0 + (int)((long)(x2 - x0) * (y - y0) / (y2 - y0));
    int xb = second ? x1 + (int)((long)(x2 - x1) * (y - y1) / (y2 - y1))
                     : x0 + (int)((long)(x1 - x0) * (y - y0) / (y1 - y0));
    if (xa > xb) { t = xa; xa = xb; xb = t; }
    for (int x = xa; x <= xb; x++) setPx(x, y, c);
  }
}
void rotatePoint(float x, float y, float ang, float &ox, float &oy) {
  ox = x * cosf(ang) - y * sinf(ang);
  oy = x * sinf(ang) + y * cosf(ang);
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
uint16_t hsv565(float h, float s, float v) {
  h = fmodf(h, 1.0f); if (h < 0) h += 1.0f;
  float r, g, b;
  int i = (int)(h * 6);
  float f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), tt = v * (1 - (1 - f) * s);
  switch (i % 6) {
    case 0: r = v; g = tt; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = tt; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = tt; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return rgb565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

// ================= hardware (CO5300 / QSPI) =================
void fatal(const char *msg) { Serial.println(msg); while (true) { delay(1000); Serial.println(msg); } }
void check(esp_err_t r, const char *where) {
  if (r != ESP_OK) {
    Serial.printf("%s: %s\n", where, esp_err_to_name(r));
    fatal("Stopped. Check board settings and press RST.");
  }
}
bool IRAM_ATTR transferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR(sent, &wake);
  return wake == pdTRUE;
}
void command(uint8_t cmd, const void *data, size_t len) {
  check(esp_lcd_panel_io_tx_param(io, 0x02000000 | (uint32_t(cmd) << 8), data, len), "Display command");
}
void byteCommand(uint8_t cmd, uint8_t value) { command(cmd, &value, 1); }

void flush() {
  for (int y = 0; y < LCD_HEIGHT; y += STRIPE_ROWS) {
    int rows = (STRIPE_ROWS < (LCD_HEIGHT - y)) ? STRIPE_ROWS : (LCD_HEIGHT - y);
    int pixels = rows * LCD_WIDTH;
    const uint16_t *src = frame + y * LCD_WIDTH;
    for (int i = 0; i < pixels; i++) stripe[i] = (src[i] << 8) | (src[i] >> 8);
    int x0 = LCD_X_OFFSET, x1 = x0 + LCD_WIDTH - 1;
    int y0 = y + LCD_Y_OFFSET, y1 = y0 + rows - 1;
    uint8_t col[] = { uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1) };
    uint8_t row[] = { uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1) };
    command(0x2A, col, sizeof(col)); command(0x2B, row, sizeof(row));
    check(esp_lcd_panel_io_tx_color(io, 0x32002C00, stripe, pixels * 2), "Pixel transfer");
    if (xSemaphoreTake(sent, pdMS_TO_TICKS(1000)) != pdTRUE)
      fatal("Display DMA timeout. Check board variant and reset.");
  }
}

void initDisplay() {
  if (!psramFound()) fatal("Enable Tools > PSRAM > OPI PSRAM, then upload again.");
  frame = (uint16_t *)heap_caps_calloc(LCD_WIDTH * LCD_HEIGHT, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  stripe = (uint16_t *)heap_caps_malloc(LCD_WIDTH * STRIPE_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  sent = xSemaphoreCreateBinary();
  if (!frame || !stripe || !sent) fatal("Display buffer allocation failed.");

  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW); delay(20);
  digitalWrite(LCD_RST, HIGH); delay(150);

  spi_bus_config_t bus = {};
  bus.sclk_io_num = LCD_CLK;
  bus.data0_io_num = LCD_D0; bus.data1_io_num = LCD_D1;
  bus.data2_io_num = LCD_D2; bus.data3_io_num = LCD_D3;
  bus.data4_io_num = -1; bus.data5_io_num = -1; bus.data6_io_num = -1; bus.data7_io_num = -1;
  bus.max_transfer_sz = LCD_WIDTH * STRIPE_ROWS * 2;
  check(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), "QSPI bus");

  esp_lcd_panel_io_spi_config_t cfg = {};
  cfg.cs_gpio_num = LCD_CS; cfg.dc_gpio_num = -1;
  cfg.spi_mode = 0; cfg.pclk_hz = 40000000;
  cfg.trans_queue_depth = 1;
  cfg.on_color_trans_done = transferDone; cfg.user_ctx = nullptr;
  cfg.lcd_cmd_bits = 32; cfg.lcd_param_bits = 8;
  cfg.flags.quad_mode = true;
  check(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &io), "QSPI display IO");

  byteCommand(0xFE, 0x00);
  byteCommand(0x36, 0x00);
  byteCommand(0xC4, 0x80);
  byteCommand(0x3A, 0x55);
  byteCommand(0x35, 0x00); delay(10);
  byteCommand(0x53, 0x20); delay(10);
  byteCommand(0x51, BRIGHTNESS); delay(10);
  byteCommand(0x63, 0xFF); delay(10);
  command(0x11, nullptr, 0); delay(120);
  flush();
  command(0x29, nullptr, 0); delay(20);
  Serial.println("CO5300 initialized: 466x466, RGB565, QSPI 40 MHz.");
}

// ================= 0: ContrastSpinner =================
void anim0_ContrastSpinner(uint32_t t) {
  fillScreenBuf(BLK);
  float ang = t / 900.0f;
  uint16_t glow = hsv565(0.08f - 0.08f * sinf(t / 1500.0f), 0.8f, 1.0f);
  for (int k = 0; k < 3; k++) {
    float a = ang + k * (2 * PI / 3);
    float x1, y1, x2, y2, x3, y3;
    rotatePoint(0, -55 * S, a, x1, y1);
    rotatePoint(48 * S, 35 * S, a, x2, y2);
    rotatePoint(-48 * S, 35 * S, a, x3, y3);
    fillTriangleBuf(CX + (int)x1, CY + (int)y1, CX + (int)x2, CY + (int)y2, CX + (int)x3, CY + (int)y3, glow);
  }
  fillCircleBuf(CX, CY, (int)(20 * S), BLK);
  drawCircleBuf(CX, CY, (int)(20 * S), glow);
}

// ================= 1: Ghost =================
void anim1_Ghost(uint32_t t) {
  fillScreenBuf(BLK);
  int bounce = ((t / 500) % 2 == 0) ? 0 : (int)(-10 * S);
  int by = CY + bounce;
  bool flick = (t / 500) % 2 == 0;
  uint16_t red = rgb565(220, 0, 0), redDim = rgb565(90, 0, 0);
  uint16_t shadowCol = flick ? rgb565(20, 20, 20) : rgb565(45, 45, 45);
  fillCircleBuf(CX, CY + (int)(85 * S), (int)(45 * S), shadowCol);
  fillCircleBuf(CX, by - (int)(15 * S), (int)(50 * S), red);
  fillRectBuf(CX - (int)(50 * S), by - (int)(15 * S), (int)(100 * S), (int)(55 * S), red);
  int skirtY = by + (int)(40 * S);
  for (int i = 0; i < 4; i++) {
    int x0 = CX - (int)(50 * S) + i * (int)(25 * S);
    uint16_t c = ((i % 2 == 0) == flick) ? red : redDim;
    fillTriangleBuf(x0, skirtY - (int)(15 * S), x0 + (int)(25 * S), skirtY - (int)(15 * S),
                     x0 + (int)(12 * S), skirtY + (int)(15 * S), c);
  }
  int eyeShift = sinf(t / 480.0f) > 0 ? (int)(4 * S) : 0;
  for (int side = -1; side <= 1; side += 2) {
    int ex = CX + side * (int)(22 * S);
    fillRoundRectBuf(ex - (int)(12 * S), by - (int)(25 * S), (int)(24 * S), (int)(30 * S), (int)(8 * S), WHT);
    fillCircleBuf(ex + eyeShift, by - (int)(8 * S), (int)(8 * S), BLU);
  }
}

// ================= 2: HamsterWheel =================
void anim2_HamsterWheel(uint32_t t) {
  fillScreenBuf(BLK);
  float ang = t / 1000.0f * 2 * PI;
  int R = (int)(95 * S);
  drawCircleBuf(CX, CY, R, rgb565(150, 150, 150));
  drawCircleBuf(CX, CY, R - 1, rgb565(150, 150, 150));
  for (int i = 0; i < 8; i++) {
    float a = ang + i * (PI / 4);
    float x1, y1;
    rotatePoint(R - 3, 0, a, x1, y1);
    drawLineBuf(CX, CY, CX + (int)x1, CY + (int)y1, rgb565(120, 120, 120));
  }
  int hop = ((t / 250) % 2 == 0) ? 0 : (int)(-6 * S);
  int hx = CX - (int)(20 * S), hy = CY + (int)(35 * S) + hop;
  fillRoundRectBuf(hx, hy, (int)(55 * S), (int)(30 * S), (int)(12 * S), rgb565(230, 150, 60));
  fillCircleBuf(hx - (int)(5 * S), hy + (int)(5 * S), (int)(16 * S), rgb565(230, 150, 60));
  fillCircleBuf(hx - (int)(10 * S), hy - (int)(2 * S), (int)(3 * S), BLK);
  fillCircleBuf(CX, CY + (int)(95 * S), (int)(40 * S), rgb565(20, 20, 20));
}

// ================= 3: PsychedelicRings =================
void anim3_PsychedelicRings(uint32_t t) {
  fillScreenBuf(BLK);
  uint16_t cyan = rgb565(0, 255, 255), yellow = rgb565(255, 255, 0);
  float a1 = t / 1400.0f, a2 = -t / 2200.0f, a3 = t / 700.0f;
  for (int k = 0; k < 3; k++) {
    float ang = a1 + k * (2 * PI / 3);
    float x1, y1, x2, y2, x3, y3;
    rotatePoint(0, -75 * S, ang, x1, y1);
    rotatePoint(65 * S, 45 * S, ang, x2, y2);
    rotatePoint(-65 * S, 45 * S, ang, x3, y3);
    drawLineBuf(CX + (int)x1, CY + (int)y1, CX + (int)x2, CY + (int)y2, cyan);
    drawLineBuf(CX + (int)x2, CY + (int)y2, CX + (int)x3, CY + (int)y3, cyan);
    drawLineBuf(CX + (int)x3, CY + (int)y3, CX + (int)x1, CY + (int)y1, cyan);
  }
  drawCircleBuf(CX, CY, (int)(68 * S), cyan);
  for (int i = 0; i < 24; i++) {
    float ang = a2 + i * (2 * PI / 24);
    float x, y;
    rotatePoint(68 * S, 0, ang, x, y);
    if (i % 2 == 0) setPx(CX + (int)x, CY + (int)y, cyan);
  }
  float pulse = 5 * S + 3 * S * sinf(a3);
  fillCircleBuf(CX, CY, (int)pulse, yellow);
}

// ================= 4: BounceDots =================
void anim4_BounceDots(uint32_t t) {
  fillScreenBuf(BLK);
  struct Dot { int dx, dy, r; uint16_t color; float amp, speed, lo; };
  Dot dots[4] = {
    { (int)(-20 * S), (int)(-10 * S), (int)(30 * S), rgb565(0x3f, 0x7e, 0xe8), 5 * S, 15.7f, 0 },
    { (int)(20 * S),  (int)(10 * S),  (int)(12 * S), rgb565(0xde, 0x40, 0x32), 5 * S, 15.7f, 0 },
    { (int)(15 * S),  (int)(45 * S),  (int)(15 * S), rgb565(0xee, 0xb2, 0x05), 8.5f * S, 15.7f, -1.5f },
    { (int)(45 * S),  0,              (int)(6 * S),  rgb565(0x31, 0xaa, 0x52), 5 * S, 15.7f, 0 },
  };
  for (auto &d : dots) {
    float y = d.lo * 10 * S + d.amp * sinf(t / 1000.0f * d.speed);
    fillCircleBuf(CX + d.dx, CY + d.dy + (int)y, d.r, d.color);
  }
}

// ================= 5: Pyramid =================
void anim5_Pyramid(uint32_t t) {
  fillScreenBuf(BLK);
  float ang = fmodf(t / 4000.0f, 1.0f) * 2 * PI;
  float scale = cosf(ang);
  int w = (int)(75 * S * fabsf(scale)); if (w < (int)(4 * S)) w = (int)(4 * S);
  int h = (int)(80 * S);
  uint16_t color = hsv565(scale >= 0 ? 0.55f : 0.83f, 0.7f, 1.0f);
  int apexY = CY - h / 2, baseY = CY + h / 2;
  fillTriangleBuf(CX - w / 2, baseY, CX + w / 2, baseY, CX, apexY, color);
  int shadowW = (int)(35 * S - 15 * S * fabsf(sinf(ang)));
  fillCircleBuf(CX, baseY + (int)(18 * S), shadowW, rgb565(90, 60, 140));
}

// ================= 6: NodeNetwork =================
#define NA 40
float baseA[NA][3];
void initNodeNetwork() {
  for (int i = 0; i < NA; i++) {
    float t = (float)i / NA * 2 * PI;
    float R = 55 * S, r = 28 * S, p = 2, q = 3;
    baseA[i][0] = (R + r * cosf(q * t)) * cosf(p * t);
    baseA[i][1] = (R + r * cosf(q * t)) * sinf(p * t);
    baseA[i][2] = r * sinf(q * t);
  }
}
void anim6_NodeNetwork(uint32_t t) {
  fillScreenBuf(BLK);
  float ay = t / 1800.0f, ax = 0.5f + 0.2f * sinf(t / 2600.0f);
  static float sx[NA], sy[NA];
  for (int i = 0; i < NA; i++) {
    float d;
    rotProject(baseA[i][0], baseA[i][1], baseA[i][2], ay, ax, 1.15f, sx[i], sy[i], d);
  }
  float thresh = 38 * S;
  for (int i = 0; i < NA; i++)
    for (int j = i + 1; j < NA; j++) {
      float dx = sx[i] - sx[j], dy = sy[i] - sy[j];
      float dist2 = dx * dx + dy * dy;
      if (dist2 < thresh * thresh) {
        int bv = 255 - (int)(sqrtf(dist2) * 4 / S);
        if (bv < 0) bv = 0;
        uint8_t bright = (uint8_t)bv;
        drawLineBuf((int)sx[i], (int)sy[i], (int)sx[j], (int)sy[j], rgb565(bright / 2, bright / 3, bright));
      }
    }
  for (int i = 0; i < NA; i++) {
    uint16_t col = (i % 3 == 0) ? rgb565(255, 170, 220) : rgb565(210, 220, 255);
    fillCircleBuf((int)sx[i], (int)sy[i], (int)(2 * S), col);
  }
}

// ================= 7: Starburst =================
#define NB_RAYS 14
#define NB_STARS 10
float starX[NB_STARS], starY[NB_STARS], starPhase[NB_STARS];
void initStarburst() {
  for (int i = 0; i < NB_STARS; i++) {
    float a = i * 2 * PI / NB_STARS + 0.3f;
    float r = (90 + (i % 3) * 10) * S;
    starX[i] = CX + r * cosf(a);
    starY[i] = CY + r * sinf(a);
    starPhase[i] = i * 0.7f;
  }
}
void anim7_Starburst(uint32_t t) {
  fillScreenBuf(BLK);
  for (int i = 0; i < NB_STARS; i++) {
    float b = 0.5f + 0.5f * sinf(t / 400.0f + starPhase[i]);
    uint8_t v = (uint8_t)(120 + 120 * b);
    fillCircleBuf((int)starX[i], (int)starY[i], (int)((1 + (b > 0.8f ? 1 : 0)) * S), rgb565(v, v, v));
  }
  float rot = t / 6000.0f;
  for (int i = 0; i < NB_RAYS; i++) {
    float a = i * 2 * PI / NB_RAYS + rot;
    float pulse = 0.5f + 0.5f * sinf(t / 260.0f + i * 1.1f);
    float len = (15 + 85 * pulse) * S;
    uint8_t v = (uint8_t)(140 + 115 * pulse);
    float ex = CX + len * cosf(a), ey = CY + len * sinf(a);
    drawLineBuf(CX, CY, (int)ex, (int)ey, rgb565(v, v, v));
  }
  float core = (6 + 4 * sinf(t / 200.0f)) * S;
  fillCircleBuf(CX, CY, (int)core, WHT);
}

// ================= 8: StarNetwork =================
#define NC 9
float baseCx[NC], baseCy[NC];
void initStarNetwork() {
  float offx[NC] = { -70, -35, -50, -20, 5, 55, 30, -55, 60 };
  float offy[NC] = { -30, -15, 5, -5, -25, -35, 40, 60, 65 };
  for (int i = 0; i < NC; i++) { baseCx[i] = offx[i] * S; baseCy[i] = offy[i] * S; }
}
void anim8_StarNetwork(uint32_t t) {
  fillScreenBuf(BLK);
  float ang = t / 5000.0f;
  float ca = cosf(ang), sa = sinf(ang);
  static float sx[NC], sy[NC];
  for (int i = 0; i < NC; i++) {
    sx[i] = CX + baseCx[i] * ca - baseCy[i] * sa;
    sy[i] = CY + baseCx[i] * sa + baseCy[i] * ca;
  }
  for (int i = 0; i < NC; i++) {
    drawLineBuf((int)sx[i], (int)sy[i], (int)sx[(i + 4) % NC], (int)sy[(i + 4) % NC], rgb565(220, 220, 220));
    drawLineBuf((int)sx[i], (int)sy[i], (int)sx[(i + 2) % NC], (int)sy[(i + 2) % NC], rgb565(150, 150, 150));
  }
  for (int i = 0; i < NC; i++) {
    int s = (int)(((i % 3 == 0) ? 9 : 5) * S);
    fillRectBuf((int)sx[i] - s / 2, (int)sy[i] - s / 2, s, s, WHT);
  }
}

// ================= 9: ScribbleLoop =================
#define ND 160
float baseDx[ND], baseDy[ND];
void initScribbleLoop() {
  for (int i = 0; i < ND; i++) {
    float th = (float)i / ND * 2 * PI;
    float rad = (65 + 16 * sinf(3 * th + 1.0f) + 11 * sinf(5 * th + 2.1f) + 7 * sinf(8 * th + 0.4f)) * S;
    baseDx[i] = rad * cosf(th);
    baseDy[i] = rad * sinf(th);
  }
}
void anim9_ScribbleLoop(uint32_t t) {
  fillScreenBuf(BLK);
  float ang = t / 9000.0f;
  float ca = cosf(ang), sa = sinf(ang);
  static float sx[ND], sy[ND];
  for (int i = 0; i < ND; i++) {
    sx[i] = CX + baseDx[i] * ca - baseDy[i] * sa;
    sy[i] = CY + baseDx[i] * sa + baseDy[i] * ca;
  }
  for (int i = 0; i < ND; i++)
    drawLineBuf((int)sx[i], (int)sy[i], (int)sx[(i + 1) % ND], (int)sy[(i + 1) % ND], rgb565(200, 200, 200));
  float thresh = 30 * S;
  for (int i = 0; i < ND; i += 5)
    for (int j = i + 15; j < ND; j += 5) {
      float dx = sx[i] - sx[j], dy = sy[i] - sy[j];
      if (dx * dx + dy * dy < thresh * thresh)
        drawLineBuf((int)sx[i], (int)sy[i], (int)sx[j], (int)sy[j], rgb565(90, 90, 90));
    }
}

// ================= 10: PolyhedraCluster =================
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
      float rr = clusterR[c] * S;
      baseEx[c][i][0] = rr * sinf(phi) * cosf(theta);
      baseEx[c][i][1] = rr * sinf(phi) * sinf(theta);
      baseEx[c][i][2] = rr * cosf(phi);
    }
    clusterOff[c][0] *= S; clusterOff[c][1] *= S; clusterOff[c][2] *= S;
  }
}
void anim10_PolyhedraCluster(uint32_t t) {
  fillScreenBuf(BLK);
  float ay = t / 4000.0f, ax = 0.4f;
  static float sx[NE_PTS], sy[NE_PTS];
  for (int c = 0; c < NE_CLUSTERS; c++) {
    for (int i = 0; i < NE_PTS; i++) {
      float d;
      rotProject(baseEx[c][i][0] + clusterOff[c][0],
                 baseEx[c][i][1] + clusterOff[c][1],
                 baseEx[c][i][2] + clusterOff[c][2], ay, ax, 1.05f, sx[i], sy[i], d);
    }
    for (int i = 0; i < NE_PTS; i++) {
      drawLineBuf((int)sx[i], (int)sy[i], (int)sx[(i + 1) % NE_PTS], (int)sy[(i + 1) % NE_PTS], rgb565(230, 230, 230));
      drawLineBuf((int)sx[i], (int)sy[i], (int)sx[(i + 3) % NE_PTS], (int)sy[(i + 3) % NE_PTS], rgb565(120, 120, 120));
    }
  }
}

typedef void (*AnimFn)(uint32_t);
AnimFn anims[NUM_ANIMS] = {
  anim0_ContrastSpinner, anim1_Ghost, anim2_HamsterWheel, anim3_PsychedelicRings,
  anim4_BounceDots, anim5_Pyramid, anim6_NodeNetwork, anim7_Starburst,
  anim8_StarNetwork, anim9_ScribbleLoop, anim10_PolyhedraCluster
};

void advanceAndReboot() {
  prefs.begin("anims7", false);
  int idx = prefs.getInt("idx", -1);
  idx = (idx + 1) % NUM_ANIMS;
  prefs.putInt("idx", idx);
  prefs.end();
  ESP.restart();
}

void setup() {
  Serial.begin(115200); delay(300);
  Serial.println("\nVIEWE-Round animations (CO5300 466x466 QSPI)");
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  prefs.begin("anims7", false);
  animIndex = prefs.getInt("idx", 0);
  prefs.end();

  initNodeNetwork();
  initStarburst();
  initStarNetwork();
  initScribbleLoop();
  initPolyhedraCluster();

  initDisplay();
  Serial.printf("Showing animation %d / %d\n", animIndex, NUM_ANIMS - 1);
}

void loop() {
  static uint32_t t0 = millis();
  anims[animIndex](millis() - t0);
  flush();
  if (digitalRead(BOOT_BTN_PIN) == LOW) {
    delay(30);
    if (digitalRead(BOOT_BTN_PIN) == LOW) advanceAndReboot();
  }
  delay(10);
}
