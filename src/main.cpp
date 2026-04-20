#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ── WiFi credentials ─────────────────────────────────────────────────────────
#include "wifi-credentials.h"

// ── Matrix config ─────────────────────────────────────────────────────────────
#define LEFT_PIN  5
#define RIGHT_PIN 18
#define WIDTH     8
#define HEIGHT    24
#define NUM_LEDS  (WIDTH * HEIGHT)
// Global dimming uses 8-bit scale: low values crush pastels (only ~N distinct steps per channel).
#define BRIGHTNESS 128

static uint8_t throbHue = 0;
static uint8_t throbPhase = 0;

CRGB leftLeds[NUM_LEDS];
CRGB rightLeds[NUM_LEDS];

// ── WebSocket server on port 81 ───────────────────────────────────────────────
WebSocketsServer ws(81);

// ── Mode state ────────────────────────────────────────────────────────────────
enum Mode { MODE_STATIC, MODE_ANIMATION, MODE_OFF };
Mode currentMode = MODE_OFF;
uint8_t animationType = 0;   // 0=pixel sweep, 1=row, 2=col, 3=rainbow
unsigned long lastFrame = 0;
int animStep = 0;

// true: right strip mirrors left (face symmetry). false: separate 8×24 buffers (1152-byte frames).
bool mirrorPair = true;

// ── Serpentine mapper ─────────────────────────────────────────────────────────
uint16_t XY(uint8_t x, uint8_t y) {
  return (y % 2 == 0) ? y * WIDTH + x : y * WIDTH + (WIDTH - 1 - x);
}
uint16_t XY_MIRROR(uint8_t x, uint8_t y) {
  return XY((WIDTH - 1 - x), y);
}

void fillBoth(CRGB color) {
  fill_solid(leftLeds, NUM_LEDS, color);
  fill_solid(rightLeds, NUM_LEDS, color);
}

// ── Apply pixel buffer: 576 B = mirror (left + mirrored right), 1152 B = independent ─
void applyPixelBuffer(uint8_t* buf, size_t len) {
  const size_t need = mirrorPair ? (size_t)(WIDTH * HEIGHT * 3)
                                 : (size_t)(WIDTH * HEIGHT * 3 * 2);
  if (len < need) {
    Serial.printf("WS BIN: expected %u bytes, got %u (mirror=%d)\n",
                  (unsigned)need, (unsigned)len, mirrorPair ? 1 : 0);
    return;
  }
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      int i = (y * WIDTH + x) * 3;
      CRGB c(buf[i], buf[i + 1], buf[i + 2]);
      leftLeds[XY(x, y)] = c;
      if (mirrorPair) {
        rightLeds[XY_MIRROR(x, y)] = c;
      }
    }
  }
  if (!mirrorPair) {
    const size_t off = (size_t)(WIDTH * HEIGHT * 3);
    for (int y = 0; y < HEIGHT; y++) {
      for (int x = 0; x < WIDTH; x++) {
        size_t j = off + (size_t)((y * WIDTH + x) * 3);
        CRGB c(buf[j], buf[j + 1], buf[j + 2]);
        rightLeds[XY(x, y)] = c;
      }
    }
  }
  FastLED.show();
}

// ── WebSocket message handler ─────────────────────────────────────────────────
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) return;

    const char* cmd = doc["cmd"];

    if (strcmp(cmd, "mode") == 0) {
      const char* m = doc["mode"];
      if      (strcmp(m, "off")       == 0) { currentMode = MODE_OFF; fillBoth(CRGB::Black); FastLED.show(); }
      else if (strcmp(m, "animation") == 0) { currentMode = MODE_ANIMATION; animStep = 0; }
      else if (strcmp(m, "static")    == 0) { currentMode = MODE_STATIC; }
    }
    else if (strcmp(cmd, "anim") == 0) {
      animationType = doc["type"] | 0;
      animStep = 0;
    }
    else if (strcmp(cmd, "pair") == 0) {
      const char* pm = doc["mode"];
      if (pm && strcmp(pm, "independent") == 0) mirrorPair = false;
      else mirrorPair = true;
    }
    else if (strcmp(cmd, "brightness") == 0) {
      FastLED.setBrightness(doc["value"] | BRIGHTNESS);
      FastLED.show();
    }
    else if (strcmp(cmd, "fill") == 0) {
      currentMode = MODE_STATIC;
      CRGB c(doc["r"] | 0, doc["g"] | 0, doc["b"] | 0);
      fillBoth(c);
      FastLED.show();
    }
  }
  else if (type == WStype_BIN) {
    // Binary: 576 B (mirror) or 1152 B (independent left then right)
    currentMode = MODE_STATIC;
    applyPixelBuffer(payload, length);
  }
}

// ── Animation tick ────────────────────────────────────────────────────────────
void tickAnimation() {
  unsigned long now = millis();
  int interval = (animationType == 0) ? 15 : (animationType == 4) ? 20 : 100;
  if (now - lastFrame < (unsigned long)interval) return;
  lastFrame = now;

  if (animationType == 0) { // pixel sweep
    int total = WIDTH * HEIGHT;
    fillBoth(CRGB::Black);
    int x = animStep % WIDTH, y = animStep / WIDTH;
    leftLeds[XY(x, y)] = CRGB::White;
    if (mirrorPair) rightLeds[XY_MIRROR(x, y)] = CRGB::White;
    else            rightLeds[XY(x, y)]         = CRGB::White;
    animStep = (animStep + 1) % total;
  }
  else if (animationType == 1) { // row sweep
    fillBoth(CRGB::Black);
    int y = animStep % HEIGHT;
    for (int x = 0; x < WIDTH; x++) {
      leftLeds[XY(x, y)] = CRGB::Red;
      if (mirrorPair) rightLeds[XY_MIRROR(x, y)] = CRGB::Red;
      else            rightLeds[XY(x, y)]         = CRGB::Red;
    }
    animStep = (animStep + 1) % HEIGHT;
  }
  else if (animationType == 2) { // column sweep
    fillBoth(CRGB::Black);
    int x = animStep % WIDTH;
    for (int y = 0; y < HEIGHT; y++) {
      leftLeds[XY(x, y)] = CRGB::Blue;
      if (mirrorPair) rightLeds[XY_MIRROR(x, y)] = CRGB::Blue;
      else            rightLeds[XY(x, y)]         = CRGB::Blue;
    }
    animStep = (animStep + 1) % WIDTH;
  }
  else if (animationType == 3) { // rainbow
    for (int y = 0; y < HEIGHT; y++)
      for (int x = 0; x < WIDTH; x++) {
        CRGB c = CHSV((x * 10) + (y * 5) + animStep, 255, 255);
        leftLeds[XY(x, y)] = c;
        if (mirrorPair) rightLeds[XY_MIRROR(x, y)] = c;
        else            rightLeds[XY(x, y)]         = c;
      }
    animStep = (animStep + 2) % 256;
    lastFrame = now - 70; // ~14fps
  } else if (animationType == 4) { // throbber
    const uint8_t cx = WIDTH / 2;
    const uint8_t cy = HEIGHT / 2;
    for (uint8_t y = 0; y < HEIGHT; y++) {
      for (uint8_t x = 0; x < WIDTH; x++) {
        float dx = (float)x - (float)cx + 0.5f;
        float dy = (float)y - (float)cy + 0.5f;
        uint8_t dist = (uint8_t)sqrtf(dx * dx + dy * dy);
        uint8_t wave = dist * 16;
        uint8_t diff = wave - (uint8_t)animStep;
        uint8_t bri  = (diff < 128) ? (255 - diff) : 0;
        CRGB c = CHSV(throbHue + dist * 12, 220, bri);
        leftLeds[XY(x, y)] = c;
        if (mirrorPair) rightLeds[XY_MIRROR(x, y)] = c;
        else            rightLeds[XY(x, y)]         = c;
      }
    }
    throbHue += 1;
    animStep = (animStep + 6) % 256;
  }
  FastLED.show();
}

// ── Rainbow throbber ──────────────────────────────────────────────────────────
void startupThrobber() {
  const uint8_t cx = WIDTH / 2;   // 4  (between cols 3 and 4)
  const uint8_t cy = HEIGHT / 2;  // 12 (between rows 11 and 12)
  // Max Manhattan distance from centre to a corner
  const uint8_t maxDist = cx + cy; // 4+12 = 16

  for (uint8_t y = 0; y < HEIGHT; y++) {
    for (uint8_t x = 0; x < WIDTH; x++) {
      // Manhattan distance gives a diamond pulse; use float sqrt for circular
      float dx = (float)x - (float)cx + 0.5f;
      float dy = (float)y - (float)cy + 0.5f;
      uint8_t dist = (uint8_t)sqrtf(dx * dx + dy * dy);

      // Wave front: pixels near (dist - throbPhase/16) are bright
      uint8_t wave = dist * 16;                  // scale dist into 8-bit space
      uint8_t diff = wave - throbPhase;          // wraps naturally in uint8
      // Brightness: full at wave front, falls off behind it
      uint8_t bri = 255 - diff;                  // front=255, trailing edge=0
      // Soft-clip: keep only the leading half of each pulse
      bri = (diff < 128) ? bri : 0;

      CRGB c = CHSV(throbHue + dist * 12, 220, bri);
      leftLeds[XY(x, y)] = c;
      rightLeds[XY_MIRROR(x, y)] = c;
    }
  }

  throbPhase += 6;   // pulse speed — increase to speed up
  throbHue  += 1;    // hue drift — increase for faster colour rotation

  FastLED.show();
  delay(20);         // ~50 fps cap
}

void setup() {
  Serial.begin(115200);
  // 5050 SMD package + WS2812B IC (same as BTF-LIGHTING “WS2812B / 5050SMD” listings).
  // Protocol: 800 kHz, GRB on the wire (CRGB in code is still R,G,B).
  FastLED.addLeds<WS2812B, LEFT_PIN,  GRB>(leftLeds,  NUM_LEDS);
  FastLED.addLeds<WS2812B, RIGHT_PIN, GRB>(rightLeds, NUM_LEDS);
  // Same correction as TypicalLEDStrip — tuned for green-heavy 5050 RGB modules.
  FastLED.setCorrection(TypicalSMD5050);
  FastLED.setBrightness(BRIGHTNESS);
  fillBoth(CRGB::Black);
  FastLED.show();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    startupThrobber();
    Serial.print(".");
  }

  Serial.println();
  fillBoth(CRGB::Black);
  FastLED.show();
 
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  ws.begin();
  ws.onEvent(onWsEvent);
  Serial.println("WebSocket server started on port 81");
}

void loop() {
  ws.loop();
  if (currentMode == MODE_ANIMATION) tickAnimation();
}