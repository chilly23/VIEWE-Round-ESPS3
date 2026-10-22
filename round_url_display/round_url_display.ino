/*
  VIEWE-Round (ESP32-S3) - show any GIF / JPEG / PNG from a URL on the round screen

  Libraries (Library Manager):
    - GFX Library for Arduino   (moononournation)
    - AnimatedGIF               (bitbank2)
    - JPEGDEC                   (bitbank2)
    - PNGdec                    (bitbank2)

  Arduino IDE board settings:
    Board: ESP32S3 Dev Module
    PSRAM: OPI PSRAM (or QSPI, match your module: N16R8 = OPI)
    Flash size: match your module (e.g. 16MB)
    USB CDC On Boot: Enabled

  Best results: square image, 240x240 (GIFs are cropped to centre if larger;
  JPEGs are auto down-scaled by 1/2, 1/4, 1/8 if larger).
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>
#include <AnimatedGIF.h>
#include <JPEGDEC.h>
#include <PNGdec.h>

constexpr uint16_t BLACK = RGB565_BLACK;
constexpr uint16_t WHITE = RGB565_WHITE;

// ===================== USER CONFIG =====================
const char *WIFI_SSID = "N Network";
const char *WIFI_PASS = "miv48w55";

// >>> PUT YOUR IMAGE / GIF LINK HERE (direct link to the file, http or https) <<<
const char *IMAGE_URL = "https://encrypted-tbn0.gstatic.com/images?q=tbn:ANd9GcRJxq9gwoQ5fDxCIBRcW34aNcGVX-N7gCf9LlgGUo6ceA&s=10";

// Display size
#define SCREEN_W 240
#define SCREEN_H 240

// VIEWE-ROUND-V1.1 CO5300 AMOLED, 466x466 QSPI.
#define PIN_CS   12
#define PIN_SCK  48
#define PIN_D0   13
#define PIN_D1   47
#define PIN_D2   21
#define PIN_D3   14
#define PIN_RST  11
#define PIN_BL   -1   // AMOLED: no separate backlight GPIO.
// =======================================================

Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_CS, PIN_SCK, PIN_D0, PIN_D1, PIN_D2, PIN_D3);
Arduino_GFX *panel = new Arduino_CO5300(bus, PIN_RST, 0, 466, 466, 6, 0, 6, 0);
// Preserve 240x240 image geometry; even-aligned near the display center.
Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel, 112, 112);

AnimatedGIF gif;
JPEGDEC jpeg;
PNG png;

uint8_t *imgBuf = nullptr;
size_t imgLen = 0;
int offX = 0, offY = 0;
uint16_t *fb = nullptr;

// ---------- download ----------
bool downloadImage(const char *url) {
  WiFiClientSecure secure;
  WiFiClient plain;
  HTTPClient http;
  secure.setInsecure();  // skip cert check (fine for images)

  bool https = String(url).startsWith("https");
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return false;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0 ESP32");
  int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP error: %d\n", code);
    http.end();
    return false;
  }

  int total = http.getSize();
  size_t cap = total > 0 ? total : 512 * 1024;
  if (imgBuf) free(imgBuf);
  imgBuf = (uint8_t *)ps_malloc(cap);
  if (!imgBuf) { http.end(); return false; }

  WiFiClient *s = http.getStreamPtr();
  imgLen = 0;
  uint32_t lastData = millis();
  while (http.connected() && (total < 0 || imgLen < (size_t)total)) {
    size_t avail = s->available();
    if (avail) {
      if (imgLen + avail > cap) {
        cap = (imgLen + avail) * 2;
        uint8_t *nb = (uint8_t *)ps_realloc(imgBuf, cap);
        if (!nb) { http.end(); return false; }
        imgBuf = nb;
      }
      imgLen += s->readBytes(imgBuf + imgLen, avail);
      lastData = millis();
    } else if (millis() - lastData > 10000) {
      break;
    } else {
      delay(1);
    }
  }
  http.end();
  Serial.printf("Downloaded %u bytes\n", (unsigned)imgLen);
  return imgLen > 0;
}

// ---------- GIF ----------
void GIFDraw(GIFDRAW *d) {
  int y = d->iY + d->y + offY;
  if (y < 0 || y >= SCREEN_H) return;
  uint8_t *src = d->pPixels;
  uint16_t *pal = d->pPalette;
  int x0 = d->iX + offX;
  for (int i = 0; i < d->iWidth; i++) {
    int x = x0 + i;
    if (x < 0 || x >= SCREEN_W) continue;
    uint8_t p = src[i];
    if (d->ucHasTransparency && p == d->ucTransparent) continue;
    fb[y * SCREEN_W + x] = pal[p];
  }
}

// ---------- JPEG ----------
int JPGDraw(JPEGDRAW *d) {
  for (int row = 0; row < d->iHeight; row++) {
    int y = d->y + row;
    if (y < 0 || y >= SCREEN_H) continue;
    for (int col = 0; col < d->iWidth; col++) {
      int x = d->x + col;
      if (x < 0 || x >= SCREEN_W) continue;
      fb[y * SCREEN_W + x] = d->pPixels[row * d->iWidth + col];
    }
  }
  return 1;
}

// ---------- PNG ----------
int PNGDraw(PNGDRAW *d) {
  static uint16_t line[2048];
  if (d->iWidth > 2048) return 0;
  png.getLineAsRGB565(d, line, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
  int y = d->y + offY;
  if (y < 0 || y >= SCREEN_H) return 1;
  for (int i = 0; i < d->iWidth; i++) {
    int x = i + offX;
    if (x < 0 || x >= SCREEN_W) continue;
    fb[y * SCREEN_W + x] = line[i];
  }
  return 1;
}

// ---------- helpers ----------
void showMessage(const char *msg) {
  canvas->fillScreen(BLACK);
  canvas->setTextColor(WHITE);
  canvas->setTextSize(2);
  canvas->setCursor(30, 110);
  canvas->println(msg);
  canvas->flush();
}

void playGIF() {
  gif.begin(LITTLE_ENDIAN_PIXELS);
  if (!gif.open(imgBuf, imgLen, GIFDraw)) {
    showMessage("GIF error");
    return;
  }
  offX = (SCREEN_W - gif.getCanvasWidth()) / 2;
  offY = (SCREEN_H - gif.getCanvasHeight()) / 2;
  canvas->fillScreen(BLACK);
  while (true) {
    int r = gif.playFrame(true, NULL);  // true = respect frame delay
    canvas->flush();
    if (r <= 0) gif.reset();            // loop forever
  }
}

void showJPEG() {
  if (!jpeg.openRAM(imgBuf, imgLen, JPGDraw)) { showMessage("JPEG error"); return; }
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  int w = jpeg.getWidth(), h = jpeg.getHeight();
  int scale = 0, div = 1;
  if (w > SCREEN_W * 4 || h > SCREEN_H * 4)      { scale = JPEG_SCALE_EIGHTH;  div = 8; }
  else if (w > SCREEN_W * 2 || h > SCREEN_H * 2) { scale = JPEG_SCALE_QUARTER; div = 4; }
  else if (w > SCREEN_W || h > SCREEN_H)         { scale = JPEG_SCALE_HALF;    div = 2; }
  offX = (SCREEN_W - w / div) / 2;
  offY = (SCREEN_H - h / div) / 2;
  canvas->fillScreen(BLACK);
  jpeg.decode(offX, offY, scale);
  jpeg.close();
  canvas->flush();
}

void showPNG() {
  if (png.openRAM(imgBuf, imgLen, PNGDraw) != PNG_SUCCESS) { showMessage("PNG error"); return; }
  offX = (SCREEN_W - png.getWidth()) / 2;
  offY = (SCREEN_H - png.getHeight()) / 2;
  canvas->fillScreen(BLACK);
  png.decode(NULL, 0);
  png.close();
  canvas->flush();
}

// ---------- main ----------
void setup() {
  Serial.begin(115200);

  if (PIN_BL >= 0) { pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, HIGH); }

  if (!canvas->begin(40000000)) { Serial.println("Display init failed"); while (1) delay(1000); }
  panel->fillScreen(BLACK);
  fb = canvas->getFramebuffer();
  showMessage("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  showMessage("Downloading...");
  if (!downloadImage(IMAGE_URL)) { showMessage("Download failed"); return; }

  if (imgLen > 3 && imgBuf[0] == 'G' && imgBuf[1] == 'I' && imgBuf[2] == 'F') playGIF();
  else if (imgLen > 2 && imgBuf[0] == 0xFF && imgBuf[1] == 0xD8)             showJPEG();
  else if (imgLen > 3 && imgBuf[0] == 0x89 && imgBuf[1] == 'P')               showPNG();
  else showMessage("Unsupported");
}

void loop() { delay(1000); }
