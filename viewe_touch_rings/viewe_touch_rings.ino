/*
  VIEWE touch rings -- Arduino IDE, single-file sketch.
  Artwork: Uiverse.io / Nawsome, from the SVG supplied by the user.
  Native C++ rendering of the SVG paths and CSS transforms (no web browser).

  Board: VIEWE-Round-V1.1 / ESP32-S3-N16R8, 466x466 CO5300 AMOLED.
  Arduino Boards Manager: "esp32" by Espressif Systems, version 3.3.0.
  Board: ESP32S3 Dev Module. Flash: 16MB. PSRAM: OPI PSRAM.
  USB CDC On Boot: Enabled. USB Mode: Hardware CDC and JTAG.
  No Library Manager installations required.

  Hold a finger on the display: expand / rotate / skew the SVG layers.
  Release: smoothly return to concentric rings. BOOT also previews the effect.
  Serial Monitor: 115200 baud. Touch coordinates and diagnostics are printed.

  Hardware sources and upload instructions are in README.md beside this file.
  Display init is adapted from VIEWESMART's Apache-2.0 ESP-IDF example.
  The vendor's example uses CST816 registers; the datasheet says CST9217.
  Both are supported, selected by their I2C addresses at startup.
*/

#include <Arduino.h>
#include <Wire.h>
#include <esp_arduino_version.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_lcd_panel_io.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <vector>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <cstring>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error Select ESP32S3 Dev Module in Tools > Board.
#endif
#if ESP_ARDUINO_VERSION_MAJOR < 3
#error Install esp32 by Espressif Systems version 3.3.0 or newer.
#endif

// User adjustments. The display's brightness range is 0..255.
static constexpr uint8_t BRIGHTNESS = 180;
static constexpr uint32_t FRAME_MS = 33;  // Target 30 fps; hardware speed varies.
static constexpr bool BOOT_PREVIEWS_TOUCH = true;
static constexpr int LCD_X_OFFSET = 6; // Panel window in vendor initialization.
static constexpr int LCD_Y_OFFSET = 0;
static constexpr int LCD_WIDTH = 466, LCD_HEIGHT = 466;
static constexpr int LCD_CS = 12, LCD_CLK = 48;
static constexpr int LCD_D0 = 13, LCD_D1 = 47, LCD_D2 = 21, LCD_D3 = 14;
static constexpr int LCD_RST = 11;
static constexpr int TOUCH_SDA = 17, TOUCH_SCL = 18;
static constexpr int TOUCH_RST = 10, TOUCH_INT = 9;

class TouchRings {
  struct Point { float x, y; };
  struct Edge { Point a, b; };
  struct Shape {
    std::vector<Edge> edges;
    int layer;
    float period;
    uint16_t color;
    bool centerTransform;
  };
  static constexpr float PI_F = 3.14159265358979323846f;
  static constexpr uint16_t CYAN = 0x07FF, YELLOW = 0xFFE0;
  static constexpr int STRIPE_ROWS = 16;
  std::vector<Shape> shapes;
  std::vector<Edge> transformed;
  uint16_t *frame = nullptr, *stripe = nullptr;
  float coverage[LCD_WIDTH];
  esp_lcd_panel_io_handle_t io = nullptr;
  SemaphoreHandle_t sent = nullptr;
  uint8_t touchAddress = 0;
  bool pressed = false, targetPressed = false;
  float hover = 0, transitionFrom = 0;
  uint32_t transitionAt = 0, lastFrame = 0, lastTouchLog = 0;
  uint32_t lastGoodTouch = 0, lastTouchProbe = 0;
  bool ready = false;

  void fatal(const char *message) {
    Serial.println(message);
    while (true) { delay(1000); Serial.println(message); }
  }
  void check(esp_err_t result, const char *where) {
    if (result != ESP_OK) {
      Serial.printf("%s: %s\n", where, esp_err_to_name(result));
      fatal("Stopped. Check board settings and press RST.");
    }
  }
  static bool IRAM_ATTR transferDone(esp_lcd_panel_io_handle_t,
                                    esp_lcd_panel_io_event_data_t *, void *context) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<TouchRings *>(context)->sent, &wake);
    return wake == pdTRUE;
  }
  void command(uint8_t cmd, const void *data, size_t length) {
    check(esp_lcd_panel_io_tx_param(io, 0x02000000 | (uint32_t(cmd) << 8),
                                  data, length), "Display command");
  }
  void byteCommand(uint8_t cmd, uint8_t value) { command(cmd, &value, 1); }

  void initDisplay() {
    if (!psramFound()) fatal("Enable Tools > PSRAM > OPI PSRAM, then upload again.");
    frame = static_cast<uint16_t *>(heap_caps_calloc(
      LCD_WIDTH * LCD_HEIGHT, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    stripe = static_cast<uint16_t *>(heap_caps_malloc(
      LCD_WIDTH * STRIPE_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    sent = xSemaphoreCreateBinary();
    if (!frame || !stripe || !sent) fatal("Display buffer allocation failed.");

    pinMode(LCD_RST, OUTPUT);
    digitalWrite(LCD_RST, LOW); delay(20);
    digitalWrite(LCD_RST, HIGH); delay(150);

    spi_bus_config_t bus = {};
    bus.sclk_io_num = LCD_CLK;
    bus.data0_io_num = LCD_D0; bus.data1_io_num = LCD_D1;
    bus.data2_io_num = LCD_D2; bus.data3_io_num = LCD_D3;
    bus.data4_io_num = -1; bus.data5_io_num = -1;
    bus.data6_io_num = -1; bus.data7_io_num = -1;
    bus.max_transfer_sz = LCD_WIDTH * STRIPE_ROWS * 2;
    check(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), "QSPI bus");
    esp_lcd_panel_io_spi_config_t cfg = {};
    cfg.cs_gpio_num = LCD_CS; cfg.dc_gpio_num = -1;
    cfg.spi_mode = 0; cfg.pclk_hz = 40000000;
    cfg.trans_queue_depth = 1;
    cfg.on_color_trans_done = transferDone; cfg.user_ctx = this;
    cfg.lcd_cmd_bits = 32; cfg.lcd_param_bits = 8;
    cfg.flags.quad_mode = true;
    check(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                   &cfg, &io), "QSPI display IO");

    byteCommand(0xFE, 0x00);  // User command page
    byteCommand(0x36, 0x00);  // RGB, unrotated
    byteCommand(0xC4, 0x80);  // Vendor's interface configuration
    byteCommand(0x3A, 0x55);  // RGB565
    byteCommand(0x35, 0x00); delay(10);
    byteCommand(0x53, 0x20); delay(10);
    byteCommand(0x51, BRIGHTNESS); delay(10);
    byteCommand(0x63, 0xFF); delay(10);
    command(0x11, nullptr, 0); delay(120); // Sleep out
    flush();                            // Clear RAM before turning on
    command(0x29, nullptr, 0); delay(20);
    // AMOLED has no backlight: GPIO45 is not needed.
    Serial.println("CO5300 initialized: 466x466, RGB565, QSPI 40 MHz.");
  }

  void flush() {
    for (int y = 0; y < LCD_HEIGHT; y += STRIPE_ROWS) {
      const int rows = std::min(STRIPE_ROWS, LCD_HEIGHT - y);
      const int pixels = rows * LCD_WIDTH;
      const uint16_t *src = frame + y * LCD_WIDTH;
      for (int i = 0; i < pixels; ++i) stripe[i] = (src[i] << 8) | (src[i] >> 8);
      const int x0 = LCD_X_OFFSET, x1 = x0 + LCD_WIDTH - 1;
      const int y0 = y + LCD_Y_OFFSET, y1 = y0 + rows - 1;
      const uint8_t col[] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
      const uint8_t row[] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
      command(0x2A, col, sizeof(col)); command(0x2B, row, sizeof(row));
      check(esp_lcd_panel_io_tx_color(io, 0x32002C00, stripe, pixels * 2), "Pixel transfer");
      // Wait before reusing the DMA buffer, including the last stripe.
      if (xSemaphoreTake(sent, pdMS_TO_TICKS(1000)) != pdTRUE)
        fatal("Display DMA timeout. Check board variant and reset.");
    }
  }

  bool probe(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
  }
  bool readRegister(uint16_t reg, bool wide, uint8_t *out, size_t count) {
    Wire.beginTransmission(touchAddress);
    if (wide) Wire.write(uint8_t(reg >> 8));
    Wire.write(uint8_t(reg));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(touchAddress, count, true) != count) return false;
    for (size_t i = 0; i < count; ++i) out[i] = Wire.read();
    return true;
  }
  bool writeRegister(uint16_t reg, bool wide, int value = -1) {
    Wire.beginTransmission(touchAddress);
    if (wide) Wire.write(uint8_t(reg >> 8));
    Wire.write(uint8_t(reg));
    if (value >= 0) Wire.write(uint8_t(value));
    return Wire.endTransmission() == 0;
  }
  void findTouch() {
    if (probe(0x5A)) {
      touchAddress = 0x5A;
      writeRegister(0xD101, true); // Vendor's CST9217 startup command
      delay(10);
      Serial.println("Touch: CST9217 family at 0x5A.");
    } else if (probe(0x15)) {
      touchAddress = 0x15;
      writeRegister(0xFE, false, 1); // CST816/820: disable automatic sleep
      Serial.println("Touch: CST816/820 family at 0x15.");
    } else {
      touchAddress = 0;
      Serial.println("Touch not detected. Tap screen; retrying. BOOT previews effect.");
    }
    lastGoodTouch = millis();
  }
  void initTouch() {
    pinMode(TOUCH_INT, INPUT_PULLUP);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW); delay(20);
    digitalWrite(TOUCH_RST, HIGH); delay(200);
    if (!Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000))
      fatal("Touch I2C bus initialization failed.");
    Wire.setTimeOut(20);
    findTouch();
  }
  void pollTouch(uint32_t now) {
    if (!touchAddress) {
      if (now - lastTouchProbe >= 2000) { lastTouchProbe = now; findTouch(); }
      return;
    }
    uint8_t data[7];
    bool valid = false;
    int x = 0, y = 0;
    if (touchAddress == 0x5A) {
      if (readRegister(0xD000, true, data, 7)) {
        writeRegister(0xD000, true, 0xAB); // Required acknowledge: D0 00 AB
        if (data[6] == 0xAB) {
          pressed = (data[5] & 0x0F) != 0 && (data[0] & 0x0F) == 0x06;
          x = (int(data[1]) << 4) | (data[3] >> 4);
          y = (int(data[2]) << 4) | (data[3] & 0x0F);
          valid = true;
        }
      }
    } else if (readRegister(0x02, false, data, 5)) {
      pressed = (data[0] & 0x0F) != 0;
      x = ((data[1] & 0x0F) << 8) | data[2];
      y = ((data[3] & 0x0F) << 8) | data[4];
      valid = true;
    }
    if (valid) {
      lastGoodTouch = now;
      if (pressed && now - lastTouchLog > 500) {
        lastTouchLog = now;
        Serial.printf("Touch: %d, %d\n", x, y);
      }
    } else if (now - lastGoodTouch > 750) {
      pressed = false; // A disconnected controller must not leave touch stuck.
    }
  }

  static float number(const char *&s) {
    while (*s == ' ' || *s == ',' || *s == '\n') ++s;
    char *end;
    float value = strtof(s, &end);
    s = end;
    return value;
  }
  static Point point(const char *&s) {
    float x = number(s); float y = number(s);
    return {x - 172.0f, y - 172.0f};
  }
  void addPath(const char *s, int layer, float period, uint16_t color, bool center = false) {
    Shape shape = {{}, layer, period, color, center};
    Point current = {0, 0}, start = {0, 0};
    while (*s) {
      char op = *s++;
      if (op == 'M') { current = point(s); start = current; }
      else if (op == 'L' || op == 'H' || op == 'V') {
        Point next = current;
        if (op == 'L') next = point(s);
        if (op == 'H') next.x = number(s) - 172;
        if (op == 'V') next.y = number(s) - 172;
        shape.edges.push_back({current, next}); current = next;
      } else if (op == 'C') {
        Point a = current, b = point(s), c = point(s), d = point(s);
        const float length = hypotf(b.x-a.x,b.y-a.y) + hypotf(c.x-b.x,c.y-b.y) + hypotf(d.x-c.x,d.y-c.y);
        const int steps = std::max(4, int(ceilf(length / 3.0f)));
        for (int i = 1; i <= steps; ++i) {
          float t = float(i) / steps, u = 1 - t;
          Point next = {u*u*u*a.x + 3*u*u*t*b.x + 3*u*t*t*c.x + t*t*t*d.x,
                        u*u*u*a.y + 3*u*u*t*b.y + 3*u*t*t*c.y + t*t*t*d.y};
          shape.edges.push_back({current, next}); current = next;
        }
      } else if (op == 'Z' || op == 'z') {
        shape.edges.push_back({current, start}); current = start;
      }
    }
    shapes.push_back(std::move(shape));
  }
  void addBand(float radius, float width, float beginDeg, float endDeg,
               int layer, float period, uint16_t color, bool center = false) {
    Shape shape = {{}, layer, period, color, center};
    std::vector<Point> outline;
    const int steps = std::max(8, int(ceilf((endDeg-beginDeg) / 2.0f)));
    for (int side = 0; side < 2; ++side) {
      float r = radius + (side ? -width : width) * 0.5f;
      for (int j = 0; j <= steps; ++j) {
        int i = side ? steps-j : j;
        float a = (beginDeg + (endDeg-beginDeg)*i/steps) * PI_F/180;
        outline.push_back({r*cosf(a), r*sinf(a)});
      }
    }
    for (size_t i = 0; i < outline.size(); ++i)
      shape.edges.push_back({outline[i], outline[(i+1)%outline.size()]});
    shapes.push_back(std::move(shape));
  }

  void buildArtwork() {
    shapes.reserve(16); transformed.reserve(512);
    // out1: the two stationary cyan outlines.
    addBand(100, 2, 0, 360, 0, 0, CYAN);
    addBand(25.322f, 2, 0, 360, 0, 0, CYAN);
    // out2: original seven SVG mask contours, rotating over 7 seconds.
    addPath("M102.892 127.966C93.3733 142.905 88.9517 160.527 90.2897 178.19L94.3752 177.88C93.1041 161.1 97.3046 144.36 106.347 130.168L102.892 127.966Z",1,7,CYAN);
    addPath("M93.3401 194.968C98.3049 211.971 108.646 226.908 122.814 237.541L125.273 234.264C111.814 224.163 101.99 209.973 97.2731 193.819L93.3401 194.968Z",1,7,CYAN);
    addPath("M152.707 92.3592C140.33 95.3575 128.822 101.199 119.097 109.421L121.742 112.55C130.981 104.739 141.914 99.1897 153.672 96.3413L152.707 92.3592Z",1,7,CYAN);
    addPath("M253.294 161.699C255.099 175.937 253.132 190.4 247.59 203.639L243.811 202.057C249.075 189.48 250.944 175.74 249.23 162.214L253.294 161.699Z",1,7,CYAN);
    addPath("M172 90.0557C184.677 90.0557 197.18 92.9967 208.528 98.6474C219.875 104.298 229.757 112.505 237.396 122.621L234.126 125.09C226.869 115.479 217.481 107.683 206.701 102.315C195.921 96.9469 184.043 94.1529 172 94.1529V90.0557Z",1,7,CYAN);
    addPath("M244.195 133.235C246.991 138.442 249.216 143.937 250.83 149.623L246.888 150.742C245.355 145.34 243.242 140.12 240.586 135.174L244.195 133.235Z",1,7,CYAN);
    addPath("M234.238 225.304C223.932 237.338 210.358 246.126 195.159 250.604C179.961 255.082 163.79 255.058 148.606 250.534L149.775 246.607C164.201 250.905 179.563 250.928 194.001 246.674C208.44 242.42 221.335 234.071 231.126 222.639L234.238 225.304Z",1,7,CYAN);
    // inner3: very thin paired contours, sampled from the original Beziers.
    addPath("M195.136 135.689C188.115 131.215 179.948 128.873 171.624 128.946C163.299 129.019 155.174 131.503 148.232 136.099L148.42 136.382C155.307 131.823 163.368 129.358 171.627 129.286C179.886 129.213 187.988 131.537 194.954 135.975L195.136 135.689Z M195.136 208.311C188.115 212.784 179.948 215.127 171.624 215.054C163.299 214.981 155.174 212.496 148.232 207.901L148.42 207.618C155.307 212.177 163.368 214.642 171.627 214.714C179.886 214.786 187.988 212.463 194.954 208.025L195.136 208.311Z",2,4,CYAN);
    // CSS #out3 overrides the SVG stroke to yellow.
    addBand(68.944f, 1, 0, 315, 2, 3, YELLOW);
    // inner1: preserve the detailed notches and the even-odd outline holes.
    addPath("M145.949 124.51L148.554 129.259C156.575 124.859 165.672 122.804 174.806 123.331C183.94 123.858 192.741 126.944 200.203 132.236C207.665 137.529 213.488 144.815 217.004 153.261C220.521 161.707 221.59 170.972 220.09 179.997L224.108 180.665L224.102 180.699L229.537 181.607C230.521 175.715 230.594 169.708 229.753 163.795L225.628 164.381C224.987 159.867 223.775 155.429 222.005 151.179C218.097 141.795 211.628 133.699 203.337 127.818C195.045 121.937 185.266 118.508 175.118 117.923C165.302 117.357 155.525 119.474 146.83 124.037C146.535 124.192 146.241 124.349 145.949 124.51Z M224.638 164.522C224.009 160.091 222.819 155.735 221.082 151.563C217.246 142.352 210.897 134.406 202.758 128.634C194.62 122.862 185.021 119.496 175.06 118.922C165.432 118.367 155.841 120.441 147.311 124.914L148.954 127.91C156.922 123.745 165.876 121.814 174.864 122.333C184.185 122.87 193.166 126.019 200.782 131.421C208.397 136.822 214.339 144.257 217.928 152.877C221.388 161.188 222.526 170.276 221.23 179.173L224.262 179.677C224.998 174.671 225.35 169.535 224.638 164.522Z",3,4,CYAN);
    addPath("M139.91 220.713C134.922 217.428 130.469 213.395 126.705 208.758L130.983 205.286L130.985 205.288L134.148 202.721C141.342 211.584 151.417 217.642 162.619 219.839C173.821 222.036 185.438 220.232 195.446 214.742L198.051 219.491C197.759 219.651 197.465 219.809 197.17 219.963C186.252 225.693 173.696 227.531 161.577 225.154C154.613 223.789 148.041 221.08 142.202 217.234L139.91 220.713Z M142.752 216.399C148.483 220.174 154.934 222.833 161.769 224.173C173.658 226.504 185.977 224.704 196.689 219.087L195.046 216.09C185.035 221.323 173.531 222.998 162.427 220.82C151.323 218.643 141.303 212.747 134.01 204.122L131.182 206.5C134.451 210.376 138.515 213.607 142.752 216.399Z",3,4,CYAN);
    addPath("M180.956 186.056C183.849 184.212 186.103 181.521 187.41 178.349C188.717 175.177 189.013 171.679 188.258 168.332C187.503 164.986 185.734 161.954 183.192 159.65C180.649 157.346 177.458 155.883 174.054 155.46C170.649 155.038 167.197 155.676 164.169 157.288C161.14 158.9 158.683 161.407 157.133 164.468C155.582 167.528 155.014 170.992 155.505 174.388C155.997 177.783 157.524 180.944 159.879 183.439L161.129 182.259C159.018 180.021 157.648 177.186 157.207 174.141C156.766 171.096 157.276 167.989 158.667 165.245C160.057 162.5 162.261 160.252 164.977 158.806C167.693 157.36 170.788 156.788 173.842 157.167C176.895 157.546 179.757 158.858 182.037 160.924C184.317 162.99 185.904 165.709 186.581 168.711C187.258 171.712 186.992 174.849 185.82 177.694C184.648 180.539 182.627 182.952 180.032 184.606L180.956 186.056Z",4,2,YELLOW);
    addBand(5.2885f, 0.535f, 0, 360, 4, 0, CYAN, true);
  }

  // Invert the CSS cubic Bezier's x coordinate, then evaluate its y.
  static float cssEase(float x, float x1, float y1, float x2, float y2) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    float lo = 0, hi = 1, t = x;
    for (int i = 0; i < 12; ++i) {
      t = (lo + hi) * 0.5f; float u = 1-t;
      float bx = 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t;
      if (bx < x) lo = t; else hi = t;
    }
    float u = 1-t;
    return 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t;
  }
  static float rotation(float seconds, float period) {
    if (!period) return 0;
    float phase = fmodf(seconds, period*2) / period;
    if (phase > 1) phase = 2-phase;
    return 2*PI_F*cssEase(phase, .42f, 0, .58f, 1);
  }
  void blend(int x, int y, uint16_t color, float alpha) {
    alpha = std::min(1.0f, alpha);
    uint16_t &old = frame[y*LCD_WIDTH+x];
    const float keep = 1-alpha;
    int r = int(((color>>11)&31)*alpha + ((old>>11)&31)*keep + .5f);
    int g = int(((color>>5)&63)*alpha + ((old>>5)&63)*keep + .5f);
    int b = int((color&31)*alpha + (old&31)*keep + .5f);
    old = uint16_t((r<<11)|(g<<5)|b);
  }
  void paint(const Shape &shape, float seconds) {
    transformed.clear();
    float angle = rotation(seconds, shape.period), ca = cosf(angle), sa = sinf(angle);
    float tilt = -80 * PI_F/180 * hover, ct = cosf(tilt), st = sinf(tilt);
    float skew = tanf(30 * PI_F/180 * hover);
    const int layerOffset[] = {0,1,0,1,2};
    float dx = 45 * layerOffset[shape.layer] * hover;
    float dy = -35 * layerOffset[shape.layer] * hover;
    // Shrink slightly while spread so every layer fits the circular viewport.
    float scale = 1.80f + (1.36f-1.80f)*hover;
    auto map = [&](Point p) -> Point {
      float x = ca*p.x-sa*p.y, y = sa*p.x+ca*p.y;
      if (shape.centerTransform) {
        float a = -30 * PI_F/180 * hover;
        x += 45*hover; y -= 3*hover;
        float xx = cosf(a)*x-sinf(a)*y;
        y = sinf(a)*x+cosf(a)*y; x = xx;
      }
      x += dx; y += dy;
      x += skew*y;
      return {LCD_WIDTH*.5f + scale*(ct*x-st*y),
              LCD_HEIGHT*.5f + scale*(st*x+ct*y)};
    };
    float minY = LCD_HEIGHT, maxY = 0, minX = LCD_WIDTH, maxX = 0;
    for (const Edge &e : shape.edges) {
      Edge t = {map(e.a),map(e.b)};
      transformed.push_back(t);
      minY = std::min(minY,std::min(t.a.y,t.b.y)); maxY = std::max(maxY,std::max(t.a.y,t.b.y));
      minX = std::min(minX,std::min(t.a.x,t.b.x)); maxX = std::max(maxX,std::max(t.a.x,t.b.x));
    }
    int y0 = std::max(0,int(floorf(minY))), y1 = std::min(LCD_HEIGHT-1,int(ceilf(maxY)));
    int x0 = std::max(0,int(floorf(minX))), x1 = std::min(LCD_WIDTH-1,int(ceilf(maxX)));
    if (x1 < x0 || y1 < y0) return;
    // Even-odd filling preserves holes. Two subrows + fractional horizontal
    // coverage keep the original subpixel thin rings visible without jaggies.
    for (int y = y0; y <= y1; ++y) {
      memset(coverage+x0, 0, (x1-x0+1)*sizeof(float));
      for (int sub = 0; sub < 2; ++sub) {
        float sy = y + (sub+.5f)*.5f;
        float crossings[64]; int n = 0;
        for (const Edge &e : transformed) {
          if ((e.a.y <= sy && e.b.y > sy) || (e.b.y <= sy && e.a.y > sy)) {
            if (n == 64) fatal("Artwork crossing buffer exceeded.");
            crossings[n++] = e.a.x+(sy-e.a.y)*(e.b.x-e.a.x)/(e.b.y-e.a.y);
          }
        }
        std::sort(crossings,crossings+n);
        for (int i = 0; i+1 < n; i += 2) {
          float a = std::max(float(x0),crossings[i]);
          float b = std::min(float(x1+1),crossings[i+1]);
          if (b <= a) continue;
          int first = int(floorf(a)), last = std::min(x1,int(ceilf(b))-1);
          for (int x = first; x <= last; ++x)
            coverage[x] += .5f*std::max(0.0f,std::min(float(x+1),b)-std::max(float(x),a));
        }
      }
      for (int x = x0; x <= x1; ++x)
        if (coverage[x] > 0) blend(x,y,shape.color,coverage[x]);
    }
  }

 public:
  void begin() {
    Serial.begin(115200); delay(500);
    Serial.println("\nVIEWE / Nawsome touch rings");
    if (BOOT_PREVIEWS_TOUCH) pinMode(0, INPUT_PULLUP);
    buildArtwork();
    initDisplay(); initTouch();
    ready = true;
    Serial.println("Ready. Hold touch to spread the rings; release to return.");
  }
  void tick() {
    if (!ready) return;
    uint32_t now = millis();
    if (now-lastFrame < FRAME_MS) { delay(1); return; }
    lastFrame = now;
    pollTouch(now);
    bool wanted = pressed || (BOOT_PREVIEWS_TOUCH && digitalRead(0) == LOW);
    if (wanted != targetPressed) {
      transitionFrom = hover; transitionAt = now; targetPressed = wanted;
    }
    float t = std::min(1.0f,float(now-transitionAt)/500.0f);
    hover = transitionFrom + ((targetPressed ? 1.0f : 0.0f)-transitionFrom)*cssEase(t,.25f,.1f,.25f,1);
    memset(frame,0,LCD_WIDTH*LCD_HEIGHT*2);
    float seconds = float((esp_timer_get_time()/1000) % 168000) / 1000.0f;
    // CSS z-index puts SVG 0 above SVG 1 ... above SVG 4.
    for (int layer = 4; layer >= 0; --layer)
      for (const Shape &shape : shapes) if (shape.layer == layer) paint(shape,seconds);
    flush();
  }
};

TouchRings rings;
void setup() { rings.begin(); }
void loop() { rings.tick(); }
