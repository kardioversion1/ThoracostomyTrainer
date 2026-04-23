// ═══════════════════════════════════════════════════════════════
// Thoracostomy Trainer — Firmware v5.6
// Display:  SH1106 128x64 via U8g2
// Sol1:     GPIO 19 (needle zone)
// Sol2:     GPIO 21 (finger zone)
// Pump:     GPIO 18
// FSR1:     GPIO 34 (needle zone)
// FSR2:     GPIO 35 (finger zone)
// Encoder:  CLK=32, DT=33, SW=25
//
// v5.6 changes:
//   - Dominant sensor logic: higher reading wins, lower is suppressed
//   - Both sensors must return to idle before either can re-trigger
//   - Independent sensitivity settings for needle and finger zones
// ═══════════════════════════════════════════════════════════════

#include <Wire.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include "esp_system.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Preferences prefs;

#define PIN_PUMP        18
#define PIN_SOL1        19
#define PIN_SOL2        21
#define PIN_FSR1        34
#define PIN_FSR2        35
#define PIN_ENC_CLK     32
#define PIN_ENC_DT      33
#define PIN_ENC_SW      25

const int   FSR_THRESHOLDS[] = { 800, 1500, 2500, 3500, 5000 };
const char* FSR_LABELS[]     = { "XLOW", "LOW", "MED", "HIGH", "STIFF" };
const int   FSR_SENS_COUNT   = 5;
int fsrSens1Index = 2;  // needle — default MED
int fsrSens2Index = 2;  // finger — default MED

const int   PUMP_TIMES[]  = { 1000, 3000, 5000, 10000, 20000, 30000, 60000 };
const char* PUMP_LABELS[] = { "1s", "3s", "5s", "10s", "20s", "30s", "60s" };
const int   PUMP_COUNT    = 7;
int pumpTimeIndex = 1;

const int   SOL_TIMES[]  = { 150, 350, 500, 750, 1000, 1500, 2000, 3000, 4000, 5000, 6000 };
const char* SOL_LABELS[] = { "150ms","350ms","500ms","750ms","1s","1.5s","2s","3s","4s","5s","6s" };
const int   SOL_COUNT    = 11;
int solTimeIndex = 1;

const int   READY_DELAYS[] = { 800, 3000, 5000, 10000, 30000 };
const char* READY_LABELS[] = { "0.8s", "3s", "5s", "10s", "30s" };
const int   READY_COUNT    = 5;
int readyDelayIndex = 0;

const int   PUMP_PAUSES[]       = { 0, 10000, 20000, 30000, 45000, 60000, 90000 };
const char* PUMP_PAUSE_LABELS[] = { "OFF", "10s", "20s", "30s", "45s", "60s", "90s" };
const int   PUMP_PAUSE_COUNT    = 7;
int pumpPauseIndex = 0;

enum SystemState {
  STATE_READY,
  STATE_FIRING,
  STATE_PAUSING,
  STATE_PUMPING,
  STATE_COOLDOWN
};
SystemState sysState = STATE_READY;

int fireCountNeedle = 0;
int fireCountFinger = 0;

// Edge detection
bool fsr1WasPressed = false;
bool fsr2WasPressed = false;

// Lock-out: true when that sensor was part of a trigger event
// Both must go idle before either can fire again
bool fsr1Locked = false;
bool fsr2Locked = false;

bool sol1Active = false;
bool sol2Active = false;
unsigned long sol1OffTime = 0;
unsigned long sol2OffTime = 0;

unsigned long pumpStartTime = 0;
unsigned long pumpOffTime   = 0;
unsigned long cooldownEnd   = 0;

bool flashActive       = false;
unsigned long flashEnd = 0;
#define FLASH_DURATION_MS 300

enum MenuState {
  MENU_HOME, MENU_MAIN,
  MENU_SENS_NEEDLE, MENU_SENS_FINGER,
  MENU_PUMP_TIME, MENU_SOL_TIME,
  MENU_READY_DELAY, MENU_PUMP_PAUSE
};
MenuState menuState = MENU_HOME;

const char* MAIN_ITEMS[] = {
  "NDL sensitivity",
  "FNG sensitivity",
  "Pump time",
  "Sol. time",
  "Ready delay",
  "Pump pause",
  "Reset counters"
};
const int MAIN_COUNT = 7;
int mainCursor = 0;
int subCursor  = 0;

static const int8_t QEM[4][4] = {
  { 0, -1,  1,  0},
  { 1,  0,  0, -1},
  {-1,  0,  0,  1},
  { 0,  1, -1,  0}
};
int encLastState = 0;
int encAccum     = 0;
#define STEPS_PER_DETENT 2

bool     btnWasPressed  = false;
bool     btnPending     = false;
unsigned long btnPressTime = 0;
#define DEBOUNCE_MS 30

// ════════════════════════════════════════════════════════════════

void saveSettings() {
  prefs.begin("trainer", false);
  prefs.putInt("fsrSens1",   fsrSens1Index);
  prefs.putInt("fsrSens2",   fsrSens2Index);
  prefs.putInt("pumpTime",   pumpTimeIndex);
  prefs.putInt("solTime",    solTimeIndex);
  prefs.putInt("readyDelay", readyDelayIndex);
  prefs.putInt("pumpPause",  pumpPauseIndex);
  prefs.end();
}

void loadSettings() {
  prefs.begin("trainer", true);
  fsrSens1Index   = prefs.getInt("fsrSens1",   2);
  fsrSens2Index   = prefs.getInt("fsrSens2",   2);
  pumpTimeIndex   = prefs.getInt("pumpTime",   1);
  solTimeIndex    = prefs.getInt("solTime",    1);
  readyDelayIndex = prefs.getInt("readyDelay", 0);
  pumpPauseIndex  = prefs.getInt("pumpPause",  0);
  prefs.end();
}

int readEncoder() {
  int clk = digitalRead(PIN_ENC_CLK);
  int dt  = digitalRead(PIN_ENC_DT);
  int cur = (clk << 1) | dt;
  int delta = QEM[encLastState][cur];
  encLastState = cur;

  encAccum += delta;
  int steps = 0;
  while (encAccum >= STEPS_PER_DETENT)  { steps++;  encAccum -= STEPS_PER_DETENT; }
  while (encAccum <= -STEPS_PER_DETENT) { steps--;  encAccum += STEPS_PER_DETENT; }
  return steps;
}

bool buttonPressed() {
  bool raw = (digitalRead(PIN_ENC_SW) == LOW);
  unsigned long now = millis();
  if (raw && !btnWasPressed) {
    btnWasPressed = true;
    btnPressTime  = now;
    btnPending    = true;
  } else if (!raw) {
    btnWasPressed = false;
    if (btnPending && (now - btnPressTime) >= DEBOUNCE_MS) {
      btnPending = false;
      return true;
    }
    btnPending = false;
  }
  return false;
}

void formatCountdown(unsigned long msRemaining, char* buf, int bufLen) {
  if (msRemaining == 0) {
    snprintf(buf, bufLen, "0s");
  } else if (msRemaining < 10000) {
    unsigned int tenths = (msRemaining + 50) / 100;
    snprintf(buf, bufLen, "%d.%ds", tenths / 10, tenths % 10);
  } else {
    unsigned int secs = (msRemaining + 500) / 1000;
    snprintf(buf, bufLen, "%ds", secs);
  }
}

void drawHomeScreen() {
  unsigned long now = millis();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "THORACOSTOMY");

  if (flashActive) {
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.drawStr(20, 45, "FIRE!");
  } else {
    u8g2.setFont(u8g2_font_6x10_tf);

    char countBuf[10] = "";
    char stateLine[24] = "";

    switch (sysState) {
      case STATE_READY:
        snprintf(stateLine, sizeof(stateLine), "READY");
        break;
      case STATE_FIRING:
        snprintf(stateLine, sizeof(stateLine), "FIRING");
        break;
      case STATE_PAUSING: {
        unsigned long remaining = (now < pumpStartTime) ? (pumpStartTime - now) : 0;
        formatCountdown(remaining, countBuf, sizeof(countBuf));
        snprintf(stateLine, sizeof(stateLine), "PAUSE %s", countBuf);
        break;
      }
      case STATE_PUMPING: {
        unsigned long remaining = (now < pumpOffTime) ? (pumpOffTime - now) : 0;
        formatCountdown(remaining, countBuf, sizeof(countBuf));
        snprintf(stateLine, sizeof(stateLine), "PUMP %s", countBuf);
        break;
      }
      case STATE_COOLDOWN: {
        unsigned long remaining = (now < cooldownEnd) ? (cooldownEnd - now) : 0;
        formatCountdown(remaining, countBuf, sizeof(countBuf));
        snprintf(stateLine, sizeof(stateLine), "READY %s", countBuf);
        break;
      }
    }

    u8g2.drawStr(0, 24, stateLine);

    char buf[24];
    snprintf(buf, sizeof(buf), "NDL:%d  FNG:%d", fireCountNeedle, fireCountFinger);
    u8g2.drawStr(0, 38, buf);

    snprintf(buf, sizeof(buf), "S:%s P:%s", SOL_LABELS[solTimeIndex], PUMP_LABELS[pumpTimeIndex]);
    u8g2.drawStr(0, 52, buf);
  }

  // ── FSR pressure bars (right side) ──────────────────────────
  int fsr1Val = analogRead(PIN_FSR1);
  int fsr2Val = analogRead(PIN_FSR2);

  int bar1W = map(constrain(fsr1Val, 0, 4095), 0, 4095, 0, 55);
  int bar2W = map(constrain(fsr2Val, 0, 4095), 0, 4095, 0, 55);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(70, 24, "NDL");
  u8g2.drawStr(70, 38, "FNG");

  u8g2.drawFrame(90, 15, 55, 8);
  u8g2.drawFrame(90, 29, 55, 8);

  if (bar1W > 0) u8g2.drawBox(90, 15, bar1W, 8);
  if (bar2W > 0) u8g2.drawBox(90, 29, bar2W, 8);

  // Independent threshold ticks per zone
  int tick1X = 90 + map(FSR_THRESHOLDS[fsrSens1Index], 0, 4095, 0, 55);
  int tick2X = 90 + map(FSR_THRESHOLDS[fsrSens2Index], 0, 4095, 0, 55);
  u8g2.drawVLine(tick1X, 13, 12);
  u8g2.drawVLine(tick2X, 27, 12);

  u8g2.sendBuffer();
}

void drawListMenu(const char* title, const char** items, int count, int cursor) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, title);
  u8g2.drawHLine(0, 12, 128);

  int startItem = max(0, cursor - 3);
  for (int i = startItem; i < min(count, startItem + 5); i++) {
    int y = 24 + (i - startItem) * 10;
    if (i == cursor) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(4, y, items[i]);
    u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

void handleRotation(int steps) {
  if (steps == 0) return;

  switch (menuState) {
    case MENU_HOME:
      break;
    case MENU_MAIN:
      mainCursor = constrain(mainCursor + steps, 0, MAIN_COUNT - 1);
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
    case MENU_SENS_NEEDLE:
      subCursor = constrain(subCursor + steps, 0, FSR_SENS_COUNT - 1);
      drawListMenu("NDL SENSITIVITY", FSR_LABELS, FSR_SENS_COUNT, subCursor);
      break;
    case MENU_SENS_FINGER:
      subCursor = constrain(subCursor + steps, 0, FSR_SENS_COUNT - 1);
      drawListMenu("FNG SENSITIVITY", FSR_LABELS, FSR_SENS_COUNT, subCursor);
      break;
    case MENU_PUMP_TIME:
      subCursor = constrain(subCursor + steps, 0, PUMP_COUNT - 1);
      drawListMenu("PUMP TIME", PUMP_LABELS, PUMP_COUNT, subCursor);
      break;
    case MENU_SOL_TIME:
      subCursor = constrain(subCursor + steps, 0, SOL_COUNT - 1);
      drawListMenu("SOL. TIME", SOL_LABELS, SOL_COUNT, subCursor);
      break;
    case MENU_READY_DELAY:
      subCursor = constrain(subCursor + steps, 0, READY_COUNT - 1);
      drawListMenu("READY DELAY", READY_LABELS, READY_COUNT, subCursor);
      break;
    case MENU_PUMP_PAUSE:
      subCursor = constrain(subCursor + steps, 0, PUMP_PAUSE_COUNT - 1);
      drawListMenu("PUMP PAUSE", PUMP_PAUSE_LABELS, PUMP_PAUSE_COUNT, subCursor);
      break;
  }
}

void handleButtonPress() {
  switch (menuState) {
    case MENU_HOME:
      menuState  = MENU_MAIN;
      mainCursor = 0;
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
    case MENU_MAIN:
      if (mainCursor == 0) {
        menuState = MENU_SENS_NEEDLE;
        subCursor = fsrSens1Index;
        drawListMenu("NDL SENSITIVITY", FSR_LABELS, FSR_SENS_COUNT, subCursor);
      } else if (mainCursor == 1) {
        menuState = MENU_SENS_FINGER;
        subCursor = fsrSens2Index;
        drawListMenu("FNG SENSITIVITY", FSR_LABELS, FSR_SENS_COUNT, subCursor);
      } else if (mainCursor == 2) {
        menuState = MENU_PUMP_TIME;
        subCursor = pumpTimeIndex;
        drawListMenu("PUMP TIME", PUMP_LABELS, PUMP_COUNT, subCursor);
      } else if (mainCursor == 3) {
        menuState = MENU_SOL_TIME;
        subCursor = solTimeIndex;
        drawListMenu("SOL. TIME", SOL_LABELS, SOL_COUNT, subCursor);
      } else if (mainCursor == 4) {
        menuState = MENU_READY_DELAY;
        subCursor = readyDelayIndex;
        drawListMenu("READY DELAY", READY_LABELS, READY_COUNT, subCursor);
      } else if (mainCursor == 5) {
        menuState = MENU_PUMP_PAUSE;
        subCursor = pumpPauseIndex;
        drawListMenu("PUMP PAUSE", PUMP_PAUSE_LABELS, PUMP_PAUSE_COUNT, subCursor);
      } else if (mainCursor == 6) {
        fireCountNeedle = 0;
        fireCountFinger = 0;
        menuState = MENU_HOME;
        drawHomeScreen();
      }
      break;
    case MENU_SENS_NEEDLE:
      fsrSens1Index = subCursor;
      saveSettings();
      menuState = MENU_MAIN;
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
    case MENU_SENS_FINGER:
      fsrSens2Index = subCursor;
      saveSettings();
      menuState = MENU_MAIN;
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
    case MENU_PUMP_TIME:
      pumpTimeIndex = subCursor;
      saveSettings();
      menuState = MENU_MAIN;
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
    case MENU_SOL_TIME:
      solTimeIndex = subCursor;
      saveSettings();
      menuState = MENU_MAIN;
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
    case MENU_READY_DELAY:
      readyDelayIndex = subCursor;
      saveSettings();
      menuState = MENU_MAIN;
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
    case MENU_PUMP_PAUSE:
      pumpPauseIndex = subCursor;
      saveSettings();
      menuState = MENU_MAIN;
      drawListMenu("SETTINGS", MAIN_ITEMS, MAIN_COUNT, mainCursor);
      break;
  }
}

void firezone(int zone) {
  unsigned long now = millis();
  int solMs = SOL_TIMES[solTimeIndex];

  if (zone == 1) {
    fireCountNeedle++;
    sol1Active  = true;
    sol1OffTime = now + solMs;
    digitalWrite(PIN_SOL1, HIGH);
    Serial.println("FIRE zone 1 (needle)");
  } else {
    fireCountFinger++;
    sol2Active  = true;
    sol2OffTime = now + solMs;
    digitalWrite(PIN_SOL2, HIGH);
    Serial.println("FIRE zone 2 (finger)");
  }

  // Lock both zones — neither can re-trigger until both go idle
  fsr1Locked = true;
  fsr2Locked = true;

  sysState    = STATE_FIRING;
  flashActive = true;
  flashEnd    = now + FLASH_DURATION_MS;

  drawHomeScreen();
}

// ════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Boot v5.6");

  pinMode(PIN_SOL1,    OUTPUT); digitalWrite(PIN_SOL1, LOW);
  pinMode(PIN_SOL2,    OUTPUT); digitalWrite(PIN_SOL2, LOW);
  pinMode(PIN_PUMP,    OUTPUT); digitalWrite(PIN_PUMP, LOW);
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);

  int clk = digitalRead(PIN_ENC_CLK);
  int dt  = digitalRead(PIN_ENC_DT);
  encLastState = (clk << 1) | dt;

  Wire.begin(23, 22);
  u8g2.begin();
  Serial.println("Display OK");

  loadSettings();
  Serial.println("Settings loaded");

  drawHomeScreen();
  Serial.println("Ready");
}

void loop() {
  unsigned long now = millis();

  int fsr1Val = analogRead(PIN_FSR1);
  int fsr2Val = analogRead(PIN_FSR2);
  int thresh1 = FSR_THRESHOLDS[fsrSens1Index];
  int thresh2 = FSR_THRESHOLDS[fsrSens2Index];

  bool fsr1Above = (fsr1Val >= thresh1);
  bool fsr2Above = (fsr2Val >= thresh2);

  // ── Release lock when both sensors go idle ───────────────────
  if (fsr1Locked || fsr2Locked) {
    if (!fsr1Above && !fsr2Above) {
      fsr1Locked = false;
      fsr2Locked = false;
      Serial.println("Both FSRs idle — locks cleared");
    }
  }

  // ── Dominant sensor trigger logic ────────────────────────────
  // Only fires if: STATE_READY, sensor is above threshold,
  // it wasn't already pressed, it's not locked,
  // AND its reading is >= the other sensor's reading
  if (sysState == STATE_READY) {
    bool fsr1Rising = fsr1Above && !fsr1WasPressed && !fsr1Locked;
    bool fsr2Rising = fsr2Above && !fsr2WasPressed && !fsr2Locked;

    if (fsr1Rising && fsr2Rising) {
      // Both crossing threshold simultaneously — higher value wins
      if (fsr1Val >= fsr2Val) {
        firezone(1);
      } else {
        firezone(2);
      }
    } else if (fsr1Rising) {
      // Only fire needle if it's dominant (higher or other is idle)
      if (fsr1Val >= fsr2Val) {
        firezone(1);
      } else {
        Serial.println("NDL suppressed — FNG reading higher");
      }
    } else if (fsr2Rising) {
      // Only fire finger if it's dominant
      if (fsr2Val >= fsr1Val) {
        firezone(2);
      } else {
        Serial.println("FNG suppressed — NDL reading higher");
      }
    }
  }

  fsr1WasPressed = fsr1Above;
  fsr2WasPressed = fsr2Above;

  // Solenoid off timers
  if (sol1Active && now >= sol1OffTime) {
    sol1Active = false;
    digitalWrite(PIN_SOL1, LOW);
    Serial.println("Sol1 off");
  }
  if (sol2Active && now >= sol2OffTime) {
    sol2Active = false;
    digitalWrite(PIN_SOL2, LOW);
    Serial.println("Sol2 off");
  }

  switch (sysState) {

    case STATE_FIRING:
      if (!sol1Active && !sol2Active) {
        unsigned long pauseMs = PUMP_PAUSES[pumpPauseIndex];
        if (pauseMs == 0) {
          digitalWrite(PIN_PUMP, HIGH);
          pumpOffTime = now + PUMP_TIMES[pumpTimeIndex];
          sysState = STATE_PUMPING;
          Serial.println("-> PUMPING (no pause)");
        } else {
          pumpStartTime = now + pauseMs;
          sysState = STATE_PAUSING;
          Serial.println("-> PAUSING");
        }
        drawHomeScreen();
      }
      break;

    case STATE_PAUSING:
      if (now >= pumpStartTime) {
        digitalWrite(PIN_PUMP, HIGH);
        pumpOffTime = now + PUMP_TIMES[pumpTimeIndex];
        sysState = STATE_PUMPING;
        Serial.println("-> PUMPING (after pause)");
        drawHomeScreen();
      }
      break;

    case STATE_PUMPING:
      if (now >= pumpOffTime) {
        digitalWrite(PIN_PUMP, LOW);
        cooldownEnd = now + READY_DELAYS[readyDelayIndex];
        sysState = STATE_COOLDOWN;
        Serial.println("-> COOLDOWN");
        drawHomeScreen();
      }
      break;

    case STATE_COOLDOWN:
      if (now >= cooldownEnd) {
        sysState = STATE_READY;
        Serial.println("-> READY");
        drawHomeScreen();
      }
      break;

    default:
      break;
  }

  if (flashActive && now >= flashEnd) {
    flashActive = false;
    if (menuState == MENU_HOME) drawHomeScreen();
  }

  int steps = readEncoder();
  if (steps != 0) handleRotation(steps);
  if (buttonPressed()) handleButtonPress();

  static unsigned long lastRefresh = 0;
  unsigned long refreshInterval = 500;
  if (sysState == STATE_PAUSING || sysState == STATE_PUMPING || sysState == STATE_COOLDOWN) {
    refreshInterval = 100;
  }
  if (menuState == MENU_HOME && now - lastRefresh > refreshInterval) {
    lastRefresh = now;
    drawHomeScreen();
  }
}