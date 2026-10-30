/*
  ═══════════════════════════════════════════════════════════════
   FORGE NODE  ·  ALERT EDUCATION  ·  GEOTTO COMMAND NODE

   Developed by Dominic Kata

   Forge program at Grapesyard School in Partnership 
   with Alice for Children(Italy), Alert Edu(Brazil)
   and AI That Matters using Microsoft Tools

   Sun Oct 18 2026
   
   ESP8266 + SSD1306 OLED 128x64 + 3 Buttons + PIR + RGB + Buzzer

   Hardware Pinout
   ─────────
   OLED     SDA=D2  SCL=D1
   RGB      R=D0    G=D3    B=D4     (common-cathode, PWM)
   Buttons  B1=D5   B2=D6   B3=D7    (INPUT_PULLUP, to GND)
   Buzzer   D8                       (PASSIVE buzzer — tone())
   PIR      A0                       (analog 0–1023)

   Libraries
   ─────────
   Adafruit SSD1306, Adafruit GFX, ArduinoJson 6.x

   Run the Server + UI
   ──────────────────
   cd ~/forge/server
   python3 app.py

   git remote add origin https://github.com/dkmna007/Forge-Sample-Repo

   * Serving Flask app 'app'
   * Running on all addresses (0.0.0.0)
   * Running on http://127.0.0.1:5000
   * Running on http://192.168.x.x:5000
     
   ═══════════════════════════════════════════════════════════════
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ═══════════════════════════════════════════════════════════════
// CONFIGURATION — EDIT THESE
// ═══════════════════════════════════════════════════════════════
const char* WIFI_SSID = "Airtel_X25A_8844";
const char* WIFI_PASS = "CB222357";
const char* SERVER    = "http://192.168.1.157:5000";
#define NODE_NAME "Dandora Phase 5"

// ═══════════════════════════════════════════════════════════════
// PINOUT
// ═══════════════════════════════════════════════════════════════
#define OLED_SDA  D2
#define OLED_SCL  D1
#define OLED_ADDR 0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

const int RGB_R = D0;
const int RGB_G = D3;
const int RGB_B = D4;

const int BTN_PINS[3] = {D5, D6, D7};
const int BUZZER  = D8;
const int PIR_PIN = A0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

// ═══════════════════════════════════════════════════════════════
// STATIONS
// ═══════════════════════════════════════════════════════════════
struct Station {
  const char* id;
  const char* name;
};
Station stations[3] = {
  {"st-01", "Duka Ya Kona"},
  {"st-02", "Mama Njeri Shop"},
  {"st-03", "Kinyozi Corner"}
};

// ═══════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════
String deviceId;

enum St { IDLE, WAITING, ACCEPTED, DISPATCHED };
St state = IDLE;
int activeStation = -1;

int currentPIR = 0;
int currentRSSI = 0;

unsigned long lastHeartbeat = 0;
unsigned long lastPoll      = 0;
unsigned long lastPir       = 0;
unsigned long lastDisplay   = 0;
unsigned long lastReconnect = 0;
unsigned long lastBtnTime[3] = {0, 0, 0};

const unsigned long HEARTBEAT_MS   = 10000;
const unsigned long POLL_IDLE_MS   = 3000;
const unsigned long POLL_ACTIVE_MS = 1000;
const unsigned long PIR_IDLE_MS    = 5000;
const unsigned long PIR_ACTIVE_MS  = 500;
const unsigned long DEBOUNCE       = 60;
const unsigned long DISPLAY_MS     = 400;   // ← slower, smoother frames

// Button edge detection
bool lastBtn[3]   = {HIGH, HIGH, HIGH};
bool stableBtn[3] = {HIGH, HIGH, HIGH};

// ── Non-blocking siren / reset timers ───────────────────────
bool          sirenActive     = false;
unsigned long sirenStart      = 0;
unsigned long sirenDuration   = 0;
uint8_t       sirenPhase      = 0;    // 0 = WEE (960 Hz), 1 = WOO (770 Hz)
unsigned long sirenPhaseStart = 0;

// ── Dispatch RGB pattern timing ─────────────────────────────
unsigned long dispatchStartAt = 0;

unsigned long denyResetAt     = 0;
unsigned long denyStartedAt   = 0;
unsigned long dispatchResetAt = 0;

// ═══════════════════════════════════════════════════════════════
// ANIMATION HELPERS — slow, calm, visible
// ═══════════════════════════════════════════════════════════════

// Value oscillating 0..maxVal..0 over periodMs  (slower = calmer)
int oscillate(int periodMs, int maxVal) {
  int phase = (millis() % periodMs) * 2;
  int norm  = (phase * maxVal) / periodMs;
  return (norm <= maxVal) ? norm : (2 * maxVal - norm);
}

// 0..7 spinner frame — 250 ms per step (was 120)
uint8_t spinnerFrame() { return (millis() / 250) & 0x07; }

// Toggle true/false — used for banners
bool flashOn(int periodMs) { return ((millis() / periodMs) & 1) == 0; }

// ═══════════════════════════════════════════════════════════════
// RGB COLOR HELPERS
// ═══════════════════════════════════════════════════════════════
void setLED(int r, int g, int b) {
  // Clamp to valid PWM range
  if (r < 0) r = 0; if (r > 255) r = 255;
  if (g < 0) g = 0; if (g > 255) g = 255;
  if (b < 0) b = 0; if (b > 255) b = 255;
  analogWrite(RGB_R, r);
  analogWrite(RGB_G, g);
  analogWrite(RGB_B, b);
}

// HSV → RGB conversion for smooth rainbow transitions
// h: 0-359, s: 0-255, v: 0-255
void hsvToRgb(int h, int s, int v, int* r, int* g, int* b) {
  int region = h / 60;
  int rem    = (h % 60) * 255 / 60;
  int p = (v * (255 - s)) / 255;
  int q = (v * (255 - (s * rem) / 255)) / 255;
  int t = (v * (255 - (s * (255 - rem)) / 255)) / 255;
  switch (region) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    case 5: *r = v; *g = p; *b = q; break;
    default: *r = v; *g = p; *b = q; break;
  }
}

// ═══════════════════════════════════════════════════════════════
// COLOR / GRAPHICS HELPERS
// ═══════════════════════════════════════════════════════════════
void clearOLED() { if (oledReady) display.clearDisplay(); }

void centerText(const String& text, int y, uint8_t size) {
  if (!oledReady) return;
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text.c_str(), 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

void drawDivider(int y) {
  display.drawFastHLine(0, y, SCREEN_WIDTH, SSD1306_WHITE);
}

// Signal bars — subtle bounce on the tallest lit bar
void drawSignalBars(int x, int y, int rssi) {
  int bars = 0;
  if      (rssi >= -55) bars = 4;
  else if (rssi >= -67) bars = 3;
  else if (rssi >= -75) bars = 2;
  else if (rssi >= -85) bars = 1;

  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    bool on = (i < bars);
    if (on && i == bars - 1) h += oscillate(1600, 1);   // slow bounce
    if (on) display.fillRect(x + i * 5, y + 12 - h, 3, h, SSD1306_WHITE);
    else    display.drawRect(x + i * 5, y + 12 - h, 3, h, SSD1306_WHITE);
  }
}

// Ring spinner
void drawSpinner(int cx, int cy, uint8_t frame) {
  const int radius = 7;
  for (int i = 0; i < 8; i++) {
    float angle = (i + frame) * 0.785398f;
    int x = cx + (int)(cos(angle) * radius);
    int y = cy + (int)(sin(angle) * radius);
    if (i == 0) display.fillCircle(x, y, 2, SSD1306_WHITE);
    else        display.drawPixel(x, y, SSD1306_WHITE);
  }
}

// Radar sweep — one revolution every ~4.5 s
void drawRadar(int cx, int cy, int radius) {
  float angle = (millis() % 4500) * 0.001396f;   // 2π / 4500
  int x = cx + (int)(cos(angle) * radius);
  int y = cy + (int)(sin(angle) * radius);
  display.drawCircle(cx, cy, radius, SSD1306_WHITE);
  display.drawCircle(cx, cy, radius / 2, SSD1306_WHITE);
  display.fillCircle(cx, cy, 1, SSD1306_WHITE);
  display.drawLine(cx, cy, x, y, SSD1306_WHITE);
  display.fillCircle(x, y, 1, SSD1306_WHITE);
}

const char* stateName() {
  switch (state) {
    case IDLE:       return "READY";
    case WAITING:    return "ALERT";
    case ACCEPTED:   return "REVIEW";
    case DISPATCHED: return "DISPATCH";
  }
  return "UNKNOWN";
}

// ═══════════════════════════════════════════════════════════════
// SOUND — PASSIVE buzzer via tone()
// Frequency-tunable: real musical pitches + real ambulance siren
// ═══════════════════════════════════════════════════════════════

// ALERT — panic rise: three ascending tones
void soundAlert() {
  tone(BUZZER,  800, 100); delay(120);
  tone(BUZZER, 1100, 100); delay(120);
  tone(BUZZER, 1400, 260); delay(290);
  noTone(BUZZER);
}

// ACCEPT — pleasant triad rise (C · E · G · C)
void soundAccept() {
  tone(BUZZER,  523,  90); delay(110);   // C5
  tone(BUZZER,  659,  90); delay(110);   // E5
  tone(BUZZER,  784,  90); delay(110);   // G5
  tone(BUZZER, 1047, 280); delay(310);   // C6 (octave up)
  noTone(BUZZER);
}

// DENY — descending negative
void soundDeny() {
  tone(BUZZER, 700, 200); delay(230);
  tone(BUZZER, 500, 200); delay(230);
  tone(BUZZER, 320, 450); delay(480);
  noTone(BUZZER);
}

// ERROR — harsh triple high beep (unmistakable)
void soundError() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER, 2000, 100); delay(130);
  }
  noTone(BUZZER);
}

// ── Real ambulance wee-woo — two-tone European siren ────────
//   WEEE (960 Hz) → WOO (770 Hz) → WEEE → WOO → ...
//   Alternates every 400 ms. Cycle = 800 ms → ~12.5 cycles
//   over a 10-second siren.
void startSiren(unsigned long durationMs) {
  sirenActive     = true;
  sirenStart      = millis();
  sirenDuration   = durationMs;
  sirenPhase      = 0;
  sirenPhaseStart = millis();
  tone(BUZZER, 960);
}

void updateSiren() {
  if (!sirenActive) return;

  if (millis() - sirenStart >= sirenDuration) {
    noTone(BUZZER);
    sirenActive = false;
    return;
  }

  unsigned long elapsed = millis() - sirenPhaseStart;
  if (elapsed >= 400) {
    sirenPhase      = (sirenPhase + 1) & 0x01;
    sirenPhaseStart = millis();
    tone(BUZZER, sirenPhase == 0 ? 960 : 770);
  }
}

// ═══════════════════════════════════════════════════════════════
// STARTUP RGB LIGHT SHOW — 10 second blocking pattern
// Called in setup() BEFORE WiFi connect.
//   Phase 1 (0.0–2.0s)  Rainbow chase
//   Phase 2 (2.0–3.0s)  White pulse
//   Phase 3 (3.0–5.0s)  Rapid RGB cycle (red → green → blue)
//   Phase 4 (5.0–7.0s)  Breathing magenta
//   Phase 5 (7.0–9.0s)  Fast red/white stutter
//   Phase 6 (9.0–9.5s)  Finale buildup (alternating white/red)
//   Phase 7 (9.5–10.0s) Fade to ready blue
// ═══════════════════════════════════════════════════════════════
void startupRGBShow() {
  const unsigned long DURATION = 10000;
  unsigned long start = millis();

  // Show calibration screen
  if (oledReady) {
    display.clearDisplay();
    centerText("FORGE", 4, 2);
    centerText("LIGHT CALIBRATION", 26, 1);
    drawDivider(38);
    centerText("10 second sequence", 44, 1);
    display.display();
  }
  Serial.println("Starting 10s RGB light show...");

  while (millis() - start < DURATION) {
    unsigned long t = millis() - start;
    int r = 0, g = 0, b = 0;

    if (t < 2000) {
      // Phase 1: Rainbow chase
      int hue = (t * 180 / 2000) % 360;
      hsvToRgb(hue, 255, 255, &r, &g, &b);

    } else if (t < 3000) {
      // Phase 2: White pulse
      int pulse = oscillate(500, 255);
      r = g = b = pulse;

    } else if (t < 5000) {
      // Phase 3: Rapid RGB cycle
      int phase = ((t - 3000) / 200) % 3;
      if      (phase == 0) r = 255;
      else if (phase == 1) g = 255;
      else                 b = 255;

    } else if (t < 7000) {
      // Phase 4: Breathing magenta
      int breath = oscillate(2000, 255);
      r = breath;
      b = breath;

    } else if (t < 9000) {
      // Phase 5: Fast red/white stutter
      int phase  = ((t - 7000) / 150) % 2;
      int bright = 128 + oscillate(300, 127);
      if (phase == 0) {
        r = bright;
      } else {
        r = bright; g = bright; b = bright;
      }

    } else if (t < 9500) {
      // Phase 6: Finale buildup
      int phase = ((t - 9000) / 100) % 2;
      if (phase == 0) {
        r = 255; g = 255; b = 255;   // white
      } else {
        r = 255;                      // red
      }

    } else {
      // Phase 7: Fade white → ready blue
      int fade = 255 - ((t - 9500) * 255 / 500);
      r = fade;
      g = fade;
      b = 255;
    }

    setLED(r, g, b);
    delay(15);
    yield();
  }

  // Settle to ready blue
  setLED(0, 0, 255);
  Serial.println("RGB show complete.");
}

// ═══════════════════════════════════════════════════════════════
// DISPATCH RGB LIGHT SHOW — non-blocking, driven by loop()
// Runs while state == DISPATCHED (≈12 s total)
//   Phase 1 (0–3s)   Fast red/blue siren (pairs with wee-woo sound)
//   Phase 2 (3–6s)   Rainbow sweep
//   Phase 3 (6–9s)   Rapid red/blue double-flash
//   Phase 4 (9–12s)  Fade out to ready blue
// ═══════════════════════════════════════════════════════════════
void updateDispatchRGB() {
  if (dispatchStartAt == 0) return;

  unsigned long elapsed = millis() - dispatchStartAt;
  int r = 0, g = 0, b = 0;

  if (elapsed < 3000) {
    // Phase 1: Fast red/blue siren
    bool red = (elapsed / 250) % 2 == 0;
    if (red) { r = 255; } else { b = 255; }

  } else if (elapsed < 6000) {
    // Phase 2: Rainbow sweep
    int hue = ((elapsed - 3000) * 360 / 3000) % 360;
    hsvToRgb(hue, 255, 255, &r, &g, &b);

  } else if (elapsed < 9000) {
    // Phase 3: Rapid red/blue double-flash
    int phase = (elapsed / 120) % 4;
    if (phase < 2) { r = 255; } else { b = 255; }

  } else {
    // Phase 4: Fade to ready blue
    int fade = 255 - ((elapsed - 9000) * 255 / 3000);
    if (fade < 0) fade = 0;
    r = 0;
    g = fade;
    b = 255;
  }

  setLED(r, g, b);
}

// ═══════════════════════════════════════════════════════════════
// STARTUP ANIMATIONS (all timing doubled)
// ═══════════════════════════════════════════════════════════════
void bootSplash() {
  if (!oledReady) return;

  // ── Frame 1: rotating hexagon + FORGE ──
  for (int frame = 0; frame < 30; frame++) {
    display.clearDisplay();

    int inset = (millis() / 200) % 4;
    display.drawRoundRect(inset, inset,
                          SCREEN_WIDTH - inset * 2,
                          SCREEN_HEIGHT - inset * 2,
                          7, SSD1306_WHITE);

    int cx = 64, cy = 18, r = 12;
    for (int i = 0; i < 6; i++) {
      float a1 = (i + frame * 0.12f) * 1.047f;
      float a2 = (i + 1 + frame * 0.12f) * 1.047f;
      int x1 = cx + (int)(cos(a1) * r);
      int y1 = cy + (int)(sin(a1) * r);
      int x2 = cx + (int)(cos(a2) * r);
      int y2 = cy + (int)(sin(a2) * r);
      display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
    }
    display.fillCircle(cx, cy, 3, SSD1306_WHITE);

    centerText("FORGE", 36, 2);
    centerText("ALERT EDUCATION", 55, 1);
    display.display();
    delay(90);
    yield();
  }

  // ── Frame 2: text wave ──
  for (int i = 0; i < 20; i++) {
    display.clearDisplay();
    const char* txt = "ALERT EDUCATION";
    int visible = min(15, i + 1);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 22);
    for (int k = 0; k < visible; k++) display.print(txt[k]);
    centerText("GEOTTO COMMAND NODE", 38, 1);
    int x = (i * 6) % 128;
    display.drawFastHLine(x - 20, 50, 40, SSD1306_WHITE);
    display.display();
    delay(120);
    yield();
  }

  // ── Frame 3: progress bar with scanning dot ──
  for (int p = 0; p <= 100; p += 4) {
    display.clearDisplay();
    centerText("INITIALIZING", 12, 1);
    centerText("FORGE NODE", 25, 2);

    display.drawRoundRect(9, 44, 110, 9, 3, SSD1306_WHITE);
    int w = (p * 106) / 100;
    if (w > 0) display.fillRect(11, 46, w, 5, SSD1306_WHITE);

    int dotX = 9 + ((millis() / 25) % 110);
    display.fillCircle(dotX, 41, 1, SSD1306_WHITE);

    display.setCursor(50, 55);
    display.print(p);
    display.print("%");
    display.display();
    delay(80);
    yield();
  }
}

// ═══════════════════════════════════════════════════════════════
// NETWORK ANIMATIONS
// ═══════════════════════════════════════════════════════════════
void networkScreen() {
  if (!oledReady) return;
  display.clearDisplay();

  centerText("Powered By Geotto", 3, 1);
  drawDivider(15);
  drawSpinner(64, 31, spinnerFrame());
  centerText(
    WiFi.status() == WL_CONNECTED ? "CONNECTED" : "CONNECTING...",
    43, 1
  );

  display.setCursor(4, 55);
  display.print("NODE ");
  display.print(deviceId.substring(5));
  display.display();
}

void matrixAnimation(unsigned long maxMs) {
  if (!oledReady) return;

  int head[SCREEN_WIDTH];
  int trail[SCREEN_WIDTH];
  for (int x = 0; x < SCREEN_WIDTH; x++) { head[x] = -1; trail[x] = 3; }

  unsigned long start     = millis();
  unsigned long lastFrame = 0;

  while (millis() - start < maxMs && WiFi.status() != WL_CONNECTED) {
    if (millis() - lastFrame < 130) { delay(5); continue; }   // slower rain
    lastFrame = millis();

    for (int x = 0; x < SCREEN_WIDTH; x++) {
      if (head[x] < 0 && random(0, 18) == 0) {
        head[x]  = 0;
        trail[x] = random(5, 18);
      }
      if (head[x] >= 0) {
        head[x]++;
        if (head[x] > SCREEN_HEIGHT + trail[x]) head[x] = -1;
      }
    }

    display.clearDisplay();
    for (int x = 0; x < SCREEN_WIDTH; x += 3) {
      if (head[x] >= 0) {
        int y = head[x];
        if (y >= 0 && y < SCREEN_HEIGHT) display.drawPixel(x, y, SSD1306_WHITE);
        int ty = y - trail[x];
        if (ty >= 0 && ty < SCREEN_HEIGHT) display.drawPixel(x, ty, SSD1306_WHITE);
      }
    }

    display.fillRect(15, 24, 98, 18, SSD1306_BLACK);
    display.drawRoundRect(15, 24, 98, 18, 4, SSD1306_WHITE);
    centerText("CONNECTING", 29, 1);
    display.display();
    yield();
  }
}

// ═══════════════════════════════════════════════════════════════
// OLED SCREEN RENDERERS (build buffer only, no display() call)
// ═══════════════════════════════════════════════════════════════

// ── IDLE ──
void drawIdle() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);   display.print("FORGE");
  display.setCursor(38, 0);  display.print("| ALERT EDU");
  drawSignalBars(105, 0, currentRSSI);
  drawDivider(12);

  // Slow pulsing status dot
  int pulse = oscillate(3000, 3);
  display.drawCircle(7, 23, 4 + pulse, SSD1306_WHITE);
  display.fillCircle(7, 23, 2, SSD1306_WHITE);

  display.setCursor(18, 18); display.print("NYUMBA KUMI");
  display.setCursor(18, 29);
  display.print(WiFi.status() == WL_CONNECTED
                ? "NETWORK ONLINE" : "NETWORK OFFLINE");

  drawDivider(40);

  display.setCursor(0, 44);
  display.print("PIR ");
  display.print(currentPIR);
  display.setCursor(52, 44);
  display.print(currentRSSI);
  display.print("dBm");

  drawRadar(112, 52, 8);

  display.setCursor(0, 55);
  display.print("UP ");
  display.print(millis() / 1000);
  display.print("s");
  display.setCursor(56, 55);
  display.print("dv");
  display.print(deviceId.substring(5));
}

// ── ALERT (WAITING / ACCEPTED) ──
void drawAlert() {
  bool accepted = (state == ACCEPTED);
  bool flash    = flashOn(700);              // slow blink

  if (flash) {
    display.fillRect(0, 0, 128, 15, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRect(0, 0, 128, 15, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setTextSize(1);
  display.setCursor(5, 4);
  display.print(accepted ? "* UNDER REVIEW" : "! NEW ALERT");
  display.setTextColor(SSD1306_WHITE);

  if (activeStation >= 0 && activeStation < 3) {
    centerText(stations[activeStation].name, 21, 1);
  }
  drawDivider(33);

  // Slow bouncing warning icon
  int bounce = oscillate(1400, 3);
  display.drawCircle(15, 46 + bounce, 6, SSD1306_WHITE);
  display.drawLine(15, 42 + bounce, 15, 47 + bounce, SSD1306_WHITE);
  display.drawPixel(15, 50 + bounce, SSD1306_WHITE);

  display.setCursor(28, 39);
  display.print("PIR: ");
  display.print(currentPIR);
  display.setCursor(28, 51);
  display.print(accepted ? "Help in progress" : "Awaiting 911...");

  drawSpinner(118, 55, spinnerFrame());
}

// ── DISPATCHED (clean single layout) ──
void drawDispatched() {
  // Top siren bars — 8 bars × 16px, up to 10px tall
  for (int i = 0; i < 8; i++) {
    int phase = (millis() / 200 + i * 2) % 16;
    int h     = (phase < 8) ? phase + 1 : 16 - phase;
    if (h > 10) h = 10;
    display.fillRect(i * 16, 0, 14, h, SSD1306_WHITE);
  }

  // Slow pulsing frame — inward only, stays inside screen
  int p = oscillate(1400, 1);
  display.drawRoundRect(1 + p, 14 + p, 126 - 2 * p, 48 - 2 * p, 6, SSD1306_WHITE);

  // Static large title
  centerText("DISPATCHED", 20, 2);

  // Station name
  if (activeStation >= 0 && activeStation < 3) {
    centerText(stations[activeStation].name, 38, 1);
  }

  // "HELP IS COMING" with animated dots — 700 ms per dot
  int dots = (millis() / 700) % 4;
  String msg = "HELP IS COMING";
  for (int i = 0; i < dots; i++) msg += ".";
  centerText(msg, 52, 1);
}

// ── DENIED ──
void drawDenied() {
  // Pulsing X icon
  int pulse = oscillate(800, 2);
  int cx = 64, cy = 14, r = 6 + pulse;
  display.drawLine(cx - r, cy - r, cx + r, cy + r, SSD1306_WHITE);
  display.drawLine(cx - r, cy + r, cx + r, cy - r, SSD1306_WHITE);

  centerText("HELP DENIED", 28, 2);
  centerText("Returning to ready", 50, 1);
}

// ═══════════════════════════════════════════════════════════════
// SINGLE ENTRY POINT — clear + route + display
// ═══════════════════════════════════════════════════════════════
void updateOLED() {
  if (!oledReady) return;
  display.clearDisplay();

  if (denyResetAt > 0) {
    drawDenied();
  } else {
    switch (state) {
      case IDLE:                    drawIdle();       break;
      case WAITING:
      case ACCEPTED:                drawAlert();      break;
      case DISPATCHED:              drawDispatched(); break;
    }
  }

  display.display();
}

// ═══════════════════════════════════════════════════════════════
// PIR
// ═══════════════════════════════════════════════════════════════
int readPIR() {
  int maxV = 0;
  for (int i = 0; i < 5; i++) {
    int v = analogRead(PIR_PIN);
    if (v > maxV) maxV = v;
    delay(2);
  }
  return maxV;
}

// ═══════════════════════════════════════════════════════════════
// NETWORK — HEARTBEAT
// ═══════════════════════════════════════════════════════════════
void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;
  currentRSSI = WiFi.RSSI();
  currentPIR  = readPIR();

  WiFiClient c;
  HTTPClient h;
  if (!h.begin(c, String(SERVER) + "/api/heartbeat")) return;
  h.setTimeout(1500);
  h.addHeader("Content-Type", "application/json");

  StaticJsonDocument<600> doc;
  doc["device_id"] = deviceId;
  doc["name"]      = NODE_NAME;
  doc["rssi"]      = currentRSSI;
  doc["uptime"]    = millis() / 1000;
  doc["pir"]       = currentPIR;

  JsonArray arr = doc.createNestedArray("stations");
  for (int i = 0; i < 3; i++) {
    JsonObject s = arr.createNestedObject();
    s["id"]   = stations[i].id;
    s["name"] = stations[i].name;
    s["kind"] = "button";
  }

  String body; serializeJson(doc, body);
  int code = h.POST(body);
  h.end();

  Serial.printf("[%6.1fs] HEARTBEAT %d RSSI=%d STATE=%s\n",
                millis() / 1000.0, code, currentRSSI, stateName());
}

// ═══════════════════════════════════════════════════════════════
// NETWORK — PIR
// ═══════════════════════════════════════════════════════════════
void sampleAndPostPIR() {
  currentPIR = readPIR();
  Serial.printf("[%6.1fs] PIR=%4d\n", millis() / 1000.0, currentPIR);

  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient c;
  HTTPClient h;
  if (!h.begin(c, String(SERVER) + "/api/pir")) return;
  h.setTimeout(1500);
  h.addHeader("Content-Type", "application/json");

  StaticJsonDocument<100> doc;
  doc["device_id"] = deviceId;
  doc["value"]     = currentPIR;
  String body; serializeJson(doc, body);
  h.POST(body);
  h.end();
}

// ═══════════════════════════════════════════════════════════════
// ALERT TRIGGER
// ═══════════════════════════════════════════════════════════════
void triggerAlert(int idx) {
  if (idx < 0 || idx > 2) return;

  activeStation = idx;
  state         = WAITING;
  setLED(255, 0, 0);
  currentPIR = readPIR();
  updateOLED();

  Serial.printf("[%6.1fs] BTN%d ALERT %s PIR=%d\n",
                millis() / 1000.0, idx + 1,
                stations[idx].name, currentPIR);

  soundAlert();

  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient c;
  HTTPClient h;
  if (!h.begin(c, String(SERVER) + "/api/alert")) return;
  h.setTimeout(2000);
  h.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["device_id"]  = deviceId;
  doc["station_id"] = stations[idx].id;
  doc["pir"]        = currentPIR;
  doc["event"]      = "button";
  String body; serializeJson(doc, body);
  int code = h.POST(body);
  h.end();

  Serial.printf("[%6.1fs] ALERT POST %d\n", millis() / 1000.0, code);

  // If the alert did not reach the server, play the error chirp
  if (code != 200) soundError();
}

// ═══════════════════════════════════════════════════════════════
// ACK
// ═══════════════════════════════════════════════════════════════
void sendAck(long cmdId, const char* status) {
  if (cmdId < 0 || WiFi.status() != WL_CONNECTED) return;
  WiFiClient c;
  HTTPClient h;
  if (!h.begin(c, String(SERVER) + "/api/ack")) return;
  h.setTimeout(1500);
  h.addHeader("Content-Type", "application/json");

  StaticJsonDocument<100> doc;
  doc["command_id"] = cmdId;
  doc["ack"]        = status;
  String body; serializeJson(doc, body);
  int code = h.POST(body);
  h.end();
  Serial.printf("[%6.1fs] ACK %d\n", millis() / 1000.0, code);
}

// ═══════════════════════════════════════════════════════════════
// RESET
// ═══════════════════════════════════════════════════════════════
void resetIdle() {
  state         = IDLE;
  activeStation = -1;
  dispatchStartAt = 0;
  setLED(0, 0, 255);
  updateOLED();
}

// ═══════════════════════════════════════════════════════════════
// POLL
// ═══════════════════════════════════════════════════════════════
void pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient c;
  HTTPClient h;
  if (!h.begin(c, String(SERVER) + "/api/command/" + deviceId)) return;
  h.setTimeout(1500);
  int code = h.GET();
  if (code != 200) { h.end(); return; }

  String payload = h.getString();
  h.end();

  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, payload)) return;

  String cmd = doc["command"] | "NONE";
  if (cmd == "NONE") return;

  long cmdId = doc["id"] | -1;
  Serial.printf("[%6.1fs] CMD %s id=%ld\n",
                millis() / 1000.0, cmd.c_str(), cmdId);

  if (cmd == "ACCEPT") {
    state = ACCEPTED;
    setLED(0, 255, 0);
    sendAck(cmdId, "executed");
    soundAccept();
    updateOLED();

  } else if (cmd == "DENY") {
    setLED(255, 255, 0);
    sendAck(cmdId, "executed");
    soundDeny();
    denyStartedAt = millis();
    denyResetAt   = millis() + 2200;    // longer hold on denied screen
    updateOLED();

  } else if (cmd == "DISPATCH") {
    state = DISPATCHED;
    sendAck(cmdId, "executed");
    startSiren(10000);                   // 10-second wee-woo siren
    dispatchStartAt = millis();          // start RGB light show
    dispatchResetAt = millis() + 12000;  // 12 s total before reset
    updateOLED();
  }
}

// ═══════════════════════════════════════════════════════════════
// BUTTONS
// ═══════════════════════════════════════════════════════════════
void handleButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    bool reading = digitalRead(BTN_PINS[i]);
    if (reading != lastBtn[i]) {
      lastBtnTime[i] = now;
      lastBtn[i]     = reading;
    }
    if (now - lastBtnTime[i] > DEBOUNCE) {
      if (reading != stableBtn[i]) {
        stableBtn[i] = reading;
        if (stableBtn[i] == LOW && state == IDLE) triggerAlert(i);
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// WIFI CONNECT
// ═══════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    networkScreen();
    delay(80);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    currentRSSI = WiFi.RSSI();
    Serial.printf("WiFi OK. IP=%s RSSI=%d\n",
                  WiFi.localIP().toString().c_str(), currentRSSI);

    display.clearDisplay();
    centerText("NETWORK ONLINE", 5, 1);
    drawDivider(17);
    centerText(WiFi.localIP().toString(), 24, 1);
    centerText("NODE REGISTERED", 39, 1);
    display.setCursor(12, 54);
    display.print("RSSI ");
    display.print(currentRSSI);
    display.print(" dBm");
    display.display();
    delay(2500);                       // longer so it can be read
  } else {
    Serial.println("WiFi unavailable; will retry.");
    soundError();                       // error chirp on failed connect
    display.clearDisplay();
    centerText("OFFLINE MODE", 12, 2);
    centerText("Retrying network", 38, 1);
    display.display();
    delay(1800);
  }
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println("=======================================================================");
  Serial.println("    FORGE NODE | ALERT EDUCATION | AI THAT MATTERS | ALICE FOR CHILDREN");
  Serial.println("=======================================================================");
 
  randomSeed(ESP.getChipId());

  for (int i = 0; i < 3; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    lastBtn[i]   = digitalRead(BTN_PINS[i]);
    stableBtn[i] = lastBtn[i];
  }

  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
  setLED(0, 0, 0);

  pinMode(BUZZER, OUTPUT);
  noTone(BUZZER);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledReady = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    bootSplash();
  } else {
    Serial.println("OLED not detected. Check wiring/address.");
  }

  // ── 10 s RGB LED light show, before WiFi connect ──
  startupRGBShow();

  deviceId = "node-" + String(ESP.getChipId(), HEX);

  if (oledReady) {
    display.clearDisplay();
    centerText("STARTING NETWORK", 20, 1);
    display.display();
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  matrixAnimation(15000);

  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  else currentRSSI = WiFi.RSSI();

  setLED(0, 0, 255);
  currentPIR = readPIR();
  updateOLED();

  if (WiFi.status() == WL_CONNECTED) sendHeartbeat();

  lastHeartbeat = millis();
  lastPoll      = millis();
  lastPir       = millis();
  lastDisplay   = millis();

  Serial.println("FORGE NODE READY.");
  Serial.printf("Device ID: %s\n", deviceId.c_str());
}

// ═══════════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // Non-blocking siren (buzzer)
  updateSiren();

  // Non-blocking RGB light show (only during dispatch)
  if (state == DISPATCHED) updateDispatchRGB();

  // Non-blocking auto-resets
  if (denyResetAt > 0 && now > denyResetAt) {
    denyResetAt = 0;
    resetIdle();
  }
  if (dispatchResetAt > 0 && now > dispatchResetAt) {
    dispatchResetAt = 0;
    resetIdle();
  }

  // Offline branch
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastReconnect >= 15000) {
      lastReconnect = now;
      Serial.println("WiFi disconnected. Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    if (now - lastDisplay >= 800) {
      lastDisplay = now;
      networkScreen();
    }
    handleButtons();
    yield();
    return;
  }

  handleButtons();

  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    sendHeartbeat();
  }

  unsigned long pollInterval =
    (state == IDLE) ? POLL_IDLE_MS : POLL_ACTIVE_MS;
  if (now - lastPoll >= pollInterval) {
    lastPoll = now;
    pollCommands();
  }

  unsigned long pirInterval =
    (state == IDLE) ? PIR_IDLE_MS : PIR_ACTIVE_MS;
  if (now - lastPir >= pirInterval) {
    lastPir = now;
    sampleAndPostPIR();
  }

  // Single, clean display refresh
  if (now - lastDisplay >= DISPLAY_MS) {
    lastDisplay = now;
    updateOLED();
  }

  yield();
}
