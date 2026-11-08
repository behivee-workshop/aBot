#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <math.h>

#define SDA_PIN 19
#define SCL_PIN 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define WAKE_BUTTON_PIN 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define EYE_W 20
#define EYE_H 40
#define EYE_BOTTOM (SCREEN_HEIGHT / 2 + EYE_H / 2)
#define CX1 (26 + EYE_W / 2)
#define CX2 (SCREEN_WIDTH - 26 - EYE_W / 2)

struct Eye { float x, y, w, h; };

Eye mk(int cx, int w, int h) {
  Eye e = { cx - w / 2.0f, (float)(EYE_BOTTOM - h), (float)w, (float)h };
  return e;
}

Eye idleL = mk(CX1, EYE_W, EYE_H);
Eye idleR = mk(CX2, EYE_W, EYE_H);
Eye closedL = mk(CX1, EYE_W, 4);
Eye closedR = mk(CX2, EYE_W, 4);
Eye sleepyL = mk(CX1, EYE_W + 2, 6);
Eye sleepyR = mk(CX2, EYE_W + 2, 6);
#define CURIOUS_SLIDE 10
#define CURIOUS_CLOSER 6

Eye curL = idleL, curR = idleR;

float flerp(float a, float b, float t) { return a + (b - a) * t; }

void renderEyes(Eye l, Eye r) {
  curL = l;
  curR = r;
  display.clearDisplay();
  display.fillRoundRect((int)l.x, (int)l.y, (int)l.w, (int)l.h, min((int)l.w, (int)l.h) / 2, SSD1306_WHITE);
  display.fillRoundRect((int)r.x, (int)r.y, (int)r.w, (int)r.h, min((int)r.w, (int)r.h) / 2, SSD1306_WHITE);
  display.display();
}

void morphEyes(Eye targetL, Eye targetR, int steps, int stepDelay) {
  Eye startL = curL, startR = curR;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps;
    float e = t * t * (3 - 2 * t);  // smoothstep
    renderEyes(
      { flerp(startL.x, targetL.x, e), flerp(startL.y, targetL.y, e), flerp(startL.w, targetL.w, e), flerp(startL.h, targetL.h, e) },
      { flerp(startR.x, targetR.x, e), flerp(startR.y, targetR.y, e), flerp(startR.w, targetR.w, e), flerp(startR.h, targetR.h, e) });
    delay(stepDelay);
  }
}

void thickLine(int x0, int y0, int x1, int y1, int thickness, uint16_t color) {
  for (int t = -thickness / 2; t <= thickness / 2; t++) {
    display.drawLine(x0, y0 + t, x1, y1 + t, color);
  }
  display.fillCircle(x0, y0, thickness / 2, color);
  display.fillCircle(x1, y1, thickness / 2, color);
}

// Chevron/line expressions aren't rect-shaped, so they can't share the morph
// path above — they cut in instantly behind a closed-eye transition instead.
void drawExcited() {
  display.clearDisplay();
  thickLine(28, 20, 42, 32, 6, SSD1306_WHITE);
  thickLine(42, 32, 28, 44, 6, SSD1306_WHITE);
  thickLine(100, 20, 86, 32, 6, SSD1306_WHITE);
  thickLine(86, 32, 100, 44, 6, SSD1306_WHITE);
  thickLine(58, 44, 64, 50, 6, SSD1306_WHITE);
  thickLine(64, 50, 70, 44, 6, SSD1306_WHITE);
  display.display();
}

void drawWink() {
  display.clearDisplay();
  display.fillRoundRect(32, 14, 18, 36, 9, SSD1306_WHITE);
  thickLine(76, 32, 86, 42, 6, SSD1306_WHITE);
  thickLine(86, 42, 96, 32, 6, SSD1306_WHITE);
  thickLine(58, 44, 63, 48, 4, SSD1306_WHITE);
  thickLine(63, 48, 68, 44, 4, SSD1306_WHITE);
  thickLine(68, 44, 74, 40, 4, SSD1306_WHITE);
  display.display();
}

void showExcited() {
  Eye beforeL = curL, beforeR = curR;
  morphEyes(closedL, closedR, 4, 20);
  drawExcited();
  delay(1000);
  renderEyes(closedL, closedR);
  morphEyes(beforeL, beforeR, 4, 20);
}

void showWink() {
  Eye beforeL = curL, beforeR = curR;
  morphEyes(closedL, closedR, 4, 20);
  drawWink();
  delay(1000);
  renderEyes(closedL, closedR);
  morphEyes(beforeL, beforeR, 4, 20);
}

void blink() {
  Eye beforeL = curL, beforeR = curR;
  morphEyes(closedL, closedR, 4, 20);
  morphEyes(beforeL, beforeR, 4, 20);
  if (random(0, 100) < 15) {
    delay(80);
    morphEyes(closedL, closedR, 3, 15);
    morphEyes(beforeL, beforeR, 3, 15);
  }
}

// Tiny instant eye shift, like a saccade — cheaper than lookAround so it can
// fire more often and keep idle eyes from looking frozen.
void microSaccade() {
  int off = (random(0, 2) == 0 ? -1 : 1) * random(2, 5);
  morphEyes(mk(CX1 + off, EYE_W, EYE_H), mk(CX2 + off, EYE_W, EYE_H), 3, 12);
  delay(120 + random(0, 150));
  morphEyes(idleL, idleR, 3, 12);
}

// Leading eye widens with interest, trailing eye only squints mildly — both
// stay clearly open so this can't be mistaken for a one-eyed wink.
#define CURIOUS_WIDE_W (EYE_W + 4)
#define CURIOUS_WIDE_H (EYE_H + 6)
#define CURIOUS_NARROW_W (EYE_W - 2)
#define CURIOUS_NARROW_H (EYE_H - 10)

void showCurious() {
  int dir = random(0, 2) == 0 ? -1 : 1;
  Eye l = mk(CX1 + dir * CURIOUS_SLIDE + CURIOUS_CLOSER, dir < 0 ? CURIOUS_WIDE_W : CURIOUS_NARROW_W, dir < 0 ? CURIOUS_WIDE_H : CURIOUS_NARROW_H);
  Eye r = mk(CX2 + dir * CURIOUS_SLIDE - CURIOUS_CLOSER, dir > 0 ? CURIOUS_WIDE_W : CURIOUS_NARROW_W, dir > 0 ? CURIOUS_WIDE_H : CURIOUS_NARROW_H);
  morphEyes(l, r, 12, 35);
  delay(350);
  morphEyes(idleL, idleR, 12, 35);
}

// Slow sine pulse on idle eye height, like a resting breath — runs every
// idle tick between the discrete blink/jitter/look/curious events instead
// of leaving the screen static in the gaps.
void renderBreathing() {
  float pulse = sinf(millis() / 1800.0f) * 1.5f;
  Eye l = { idleL.x, EYE_BOTTOM - (EYE_H + pulse), idleL.w, EYE_H + pulse };
  Eye r = { idleR.x, EYE_BOTTOM - (EYE_H + pulse), idleR.w, EYE_H + pulse };
  renderEyes(l, r);
}

void lookAround() {
  int off = (random(0, 2) == 0 ? -1 : 1) * random(6, 12);
  morphEyes(mk(CX1 + off, EYE_W, EYE_H), mk(CX2 + off, EYE_W, EYE_H), 6, 15);
  delay(400 + random(0, 400));
  morphEyes(idleL, idleR, 6, 15);
}

void enterSleepy() {
  morphEyes(sleepyL, sleepyR, 8, 25);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(96, 18);
  display.print("Z");
  display.setTextSize(2);
  display.setCursor(104, 3);
  display.print("Z");
  display.display();
}

enum State { ST_IDLE, ST_SLEEPY };
State state = ST_IDLE;

const unsigned long SLEEP_AFTER_MS = 120000UL;
const unsigned long SLEEPY_SCREEN_MS = 20000UL;

unsigned long bootAt = 0;
unsigned long sleepyAt = 0;
unsigned long nextBlinkAt = 0;
unsigned long nextCuriousAt = 0;
unsigned long nextLookAt = 0;
unsigned long nextJitterAt = 0;
unsigned long nextBreathAt = 0;
bool midExcitedDone = false;

void goToDeepSleep() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  rtc_gpio_pullup_en(GPIO_NUM_4);
  rtc_gpio_pulldown_dis(GPIO_NUM_4);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);
  esp_deep_sleep_start();
}

void restartCycle() {
  renderEyes(idleL, idleR);
  showExcited();

  state = ST_IDLE;
  bootAt = millis();
  midExcitedDone = false;
  nextBlinkAt = millis() + random(2000, 6000);
  nextCuriousAt = millis() + random(8000, 20000);
  nextLookAt = millis() + random(4000, 9000);
  nextJitterAt = millis() + random(1500, 4000);
}

void setup() {
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  randomSeed(esp_random());

  restartCycle();
}

void loop() {
  static bool lastBtn = HIGH;
  bool btn = digitalRead(WAKE_BUTTON_PIN);
  if (btn == LOW && lastBtn == HIGH) {
    delay(30);
    btn = digitalRead(WAKE_BUTTON_PIN);
    if (btn == LOW) {
      lastBtn = btn;
      restartCycle();
      return;
    }
  }
  lastBtn = btn;

  if (state == ST_SLEEPY) {
    if (millis() - sleepyAt >= SLEEPY_SCREEN_MS) goToDeepSleep();
    return;
  }

  if (millis() - bootAt >= SLEEP_AFTER_MS) {
    state = ST_SLEEPY;
    sleepyAt = millis();
    enterSleepy();
    return;
  }

  if (!midExcitedDone && millis() - bootAt >= SLEEP_AFTER_MS / 2) {
    midExcitedDone = true;
    showWink();
    nextBlinkAt = millis() + random(2000, 6000);
    nextCuriousAt = millis() + random(8000, 20000);
    nextLookAt = millis() + random(4000, 9000);
    return;
  }

  if (millis() >= nextCuriousAt) {
    showCurious();
    nextCuriousAt = millis() + random(8000, 20000);
    nextBlinkAt = millis() + random(2000, 6000);
    return;
  }

  if (millis() >= nextLookAt) {
    lookAround();
    nextLookAt = millis() + random(4000, 9000);
    nextBlinkAt = millis() + random(2000, 6000);
    return;
  }

  if (millis() >= nextBlinkAt) {
    blink();
    nextBlinkAt = millis() + random(2000, 6000);
    return;
  }

  if (millis() >= nextJitterAt) {
    microSaccade();
    nextJitterAt = millis() + random(1500, 4000);
    return;
  }

  if (millis() >= nextBreathAt) {
    renderBreathing();
    nextBreathAt = millis() + 50;
  }
}
