// ********************* //
// MC8P mini             //
// MIDI Controller       //
// firmware version 1.1  //
// ********************* //
// controller by         //
// .axs instruments      //
// ********************* //
//
// v1.1 - stability & performance rewrite
//  - I2C timeout + automatic OLED recovery (a glitched I2C bus no longer hangs the Nano)
//  - OLED redraws rate-limited and batched (was a full 1 KB I2C transfer per pot change)
//  - Catch-up rewritten per pot: no more continuous MIDI/OLED flood, other pots stay live
//  - Proper per-button debounce (was one shared timer; release edges weren't debounced)
//  - Buttons read as active-HIGH with plain INPUT: the PCB has 10k pull-downs, and the
//    internal pull-up was fighting them (idle level ~0.8-1.7 V, right at the LOW threshold)
//  - Raw-domain hysteresis: full 0-127 resolution with no jitter (was effectively 0,2,4...)
//  - Faster ADC + throwaway read after MUX switch
//  - MIDI library SysEx buffer shrunk (~120 bytes RAM saved); free RAM shown on boot
//  - Nano R3 / R4 handled automatically - no more commenting lines in/out
//  - EEPROM layout unchanged, so existing saved settings are kept

// -- LIBRARIES --
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MIDI.h>
#include <EEPROM.h>

// ===================== CONFIG =====================
#define FW_VERSION      "1.1"
#define SHOW_FREE_RAM   1          // 1 = show free RAM on the boot screen (AVR only)
#define I2C_CLOCK       400000UL   // drop to 100000UL if the OLED wiring is long or flaky

const uint8_t  SCAN_INTERVAL_MS    = 4;     // how often all 8 pots are read
const uint8_t  DISPLAY_INTERVAL_MS = 40;    // max OLED refresh rate (~25 fps)
const uint8_t  DEBOUNCE_MS         = 15;
const uint16_t RESET_HOLD_MS       = 2000;
const uint16_t FLASH_INTERVAL_MS   = 500;
const uint8_t  RAW_HYSTERESIS      = 3;     // ADC counts past a bucket edge before a pot "moves"
const uint8_t  POT8_ARM_DELTA      = 2;     // steps pot 8 must move before it edits in ASSIGN

// -- OLED --
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET     -1
// clkDuring / clkAfter: the library sets the I2C clock itself on every transfer,
// so this (not Wire.setClock) is what actually controls the OLED bus speed.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, I2C_CLOCK, I2C_CLOCK);

// Wire.setWireTimeout(): AVR core 1.8.3+ has it but defines no feature macro, so check the arch
#if defined(ARDUINO_ARCH_AVR) || defined(WIRE_HAS_TIMEOUT)
#define WIRE_TIMEOUT_SUPPORTED 1
#else
#define WIRE_TIMEOUT_SUPPORTED 0
#endif

// -- MUX --
#define MUX_SIG A0
#define MUX_S0  6
#define MUX_S1  7
#define MUX_S2  8
#define MUX_S3  9

// -- BUTTONS --
// Switches go to 5V with 10k pull-downs to GND (R3-R6), so pressed = HIGH.
// No internal pull-up: it would fight the pull-down and leave the idle level marginal.
#define BUTTON_PRESSED HIGH
#define ASSIGN_BUTTON 2
#define ENTER_BUTTON  3
#define PREV_BUTTON   4
#define NEXT_BUTTON   5

// -- EEPROM --
const int EEPROM_ADDRESS = 0;

// -- MIDI --
// We only transmit, so the default 128-byte SysEx receive buffer is wasted RAM.
// Instantiated explicitly rather than with MIDI_CREATE_CUSTOM_INSTANCE: that macro
// passes the settings to a different template in MIDI Library 5.0.2 vs later versions
// (and in 5.0.2 it never reaches the MIDI interface at all). This form works on both.
struct MC8PMidiSettings : public midi::DefaultSettings {
  static const unsigned SysExMaxSize = 4;
};
midi::SerialMIDI<HardwareSerial> serialMIDI(Serial);
midi::MidiInterface<midi::SerialMIDI<HardwareSerial>, MC8PMidiSettings> MIDI(serialMIDI);

// -- POT CONFIG --
// Same 6-byte layout as v1.0 so settings already in EEPROM survive the update.
// omni / isInverted are uint8_t (not bool) so 0xFF from a blank EEPROM can be validated safely.
struct PotConfig {
  uint8_t cc;
  uint8_t channel;
  uint8_t omni;
  uint8_t minValue;
  uint8_t maxValue;
  uint8_t isInverted;
};
PotConfig cfg[8];

// -- POT STATE --
uint8_t potPos[8];      // de-jittered pot position, 0-127
uint8_t lastOut[8];     // last value sent (or the value being waited for during catch-up)
uint8_t storedOut[8];   // values captured when temp mode started
int8_t  pickupSide[8];  // 0 = live; -1/+1 = waiting for the pot to cross its target from below/above
uint8_t lastEditedPot = 0;

// -- UI STATE --
enum ScreenState : uint8_t { MAIN_SCREEN, ASSIGN_SCREEN };
ScreenState currentScreen = MAIN_SCREEN;
uint8_t selectedPot = 0;
uint8_t assignEditMode = 0;
bool showPotNumber = true;
bool tempModeActive = false;
bool displayOK = false;
bool displayDirty = true;
bool pot8Armed = false;
uint8_t pot8Anchor = 0;
bool resetArmed = false;
bool resetDone = false;

uint32_t lastScanTime = 0;
uint32_t lastDrawTime = 0;
uint32_t lastFlashTime = 0;
uint32_t resetHeldSince = 0;

// -- BUTTONS --
struct Button {
  uint8_t pin;
  uint8_t stable;        // debounced level
  uint8_t lastReading;
  uint32_t changedAt;
  bool pressed;          // one-shot edge flags, valid for one loop pass
  bool released;
};
Button btnAssign = { ASSIGN_BUTTON, !BUTTON_PRESSED, !BUTTON_PRESSED, 0, false, false };
Button btnEnter  = { ENTER_BUTTON,  !BUTTON_PRESSED, !BUTTON_PRESSED, 0, false, false };
Button btnPrev   = { PREV_BUTTON,   !BUTTON_PRESSED, !BUTTON_PRESSED, 0, false, false };
Button btnNext   = { NEXT_BUTTON,   !BUTTON_PRESSED, !BUTTON_PRESSED, 0, false, false };

// Forward declarations
void drawLoadingScreen();
void drawMainScreen();
void drawAssignScreen();

// ===================== UTIL =====================
#if defined(ARDUINO_ARCH_AVR)
extern int __heap_start, *__brkval;
int freeRam() {
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
#endif

// ===================== EEPROM =====================
void setDefaults() {
  for (uint8_t i = 0; i < 8; i++) {
    cfg[i].cc = 7;
    cfg[i].channel = i + 1;
    cfg[i].omni = 0;
    cfg[i].minValue = 0;
    cfg[i].maxValue = 127;
    cfg[i].isInverted = 0;
  }
}

bool configValid(const PotConfig &c) {
  return c.cc <= 127 && c.channel >= 1 && c.channel <= 16 && c.omni <= 1 &&
         c.minValue <= 127 && c.maxValue <= 127 && c.isInverted <= 1;
}

void saveSettings() {
  EEPROM.put(EEPROM_ADDRESS, cfg);  // put() only writes bytes that changed
}

void loadSettings() {
  EEPROM.get(EEPROM_ADDRESS, cfg);
  for (uint8_t i = 0; i < 8; i++) {
    if (!configValid(cfg[i])) {  // blank or corrupt EEPROM
      setDefaults();
      saveSettings();
      return;
    }
  }
}

// ===================== POTS =====================
uint16_t readMux(uint8_t ch) {
  digitalWrite(MUX_S0, ch & 1);
  digitalWrite(MUX_S1, (ch >> 1) & 1);
  digitalWrite(MUX_S2, (ch >> 2) & 1);
  digitalWrite(MUX_S3, (ch >> 3) & 1);
  delayMicroseconds(20);
  analogRead(MUX_SIG);  // throwaway: lets the ADC sample cap settle on the new channel
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogRead(MUX_SIG);
  return sum >> 2;
}

// Updates potPos[i] with hysteresis in the raw domain. Returns true if it changed.
bool updatePotPosition(uint8_t i) {
  uint16_t raw = readMux(i);
  uint8_t candidate = raw >> 3;  // 0-1023 -> 0-127
  if (candidate == potPos[i]) return false;
  int16_t lower = (int16_t)potPos[i] * 8 - RAW_HYSTERESIS;
  int16_t upper = (int16_t)potPos[i] * 8 + 7 + RAW_HYSTERESIS;
  if ((int16_t)raw >= lower && (int16_t)raw <= upper) return false;
  potPos[i] = candidate;
  return true;
}

// Pot position -> MIDI value using the pot's min/max/direction
uint8_t potOutput(uint8_t i) {
  uint8_t lo = cfg[i].minValue;
  uint8_t hi = cfg[i].maxValue;
  if (lo > hi) { uint8_t t = lo; lo = hi; hi = t; }
  uint8_t p = cfg[i].isInverted ? 127 - potPos[i] : potPos[i];
  return lo + ((uint16_t)p * (hi - lo) + 63) / 127;
}

void sendCC(uint8_t i, uint8_t value) {
  const PotConfig &c = cfg[i];
  if (c.omni) {
    for (uint8_t ch = 1; ch <= 16; ch++) MIDI.sendControlChange(c.cc, value, ch);
  } else {
    MIDI.sendControlChange(c.cc, value, c.channel);
  }
}

void disarmPot8() {
  pot8Armed = false;
  pot8Anchor = potPos[7];
}

// Pot 8 as a data-entry knob in ASSIGN. Only takes over after it has actually been moved,
// so entering a menu / changing mode doesn't instantly overwrite the value.
void handlePot8Editing() {
  uint8_t p = potPos[7];
  if (!pot8Armed) {
    int16_t d = (int16_t)p - pot8Anchor;
    if (abs(d) < POT8_ARM_DELTA) return;
    pot8Armed = true;
  }

  PotConfig &c = cfg[selectedPot];
  switch (assignEditMode) {
    case 0: selectedPot = (uint16_t)p * 8 / 128; break;           // 0-7
    case 1: {                                                      // 1-16, then OMNI
      uint8_t n = (uint16_t)p * 17 / 128;                          // 0-16
      if (n == 16) c.omni = 1;
      else { c.omni = 0; c.channel = n + 1; }
      break;
    }
    case 2: c.cc = p; break;
    case 3: c.minValue = p; break;
    case 4: c.maxValue = p; break;
    case 5: c.isInverted = (p >= 64); break;
  }
  displayDirty = true;
}

void scanPots() {
  for (uint8_t i = 0; i < 8; i++) {
    bool moved = updatePotPosition(i);

    if (currentScreen == ASSIGN_SCREEN) {
      if (i == 7 && moved) handlePot8Editing();
      continue;  // no MIDI out while assigning
    }
    if (!moved) continue;

    uint8_t out = potOutput(i);

    // Catch-up: ignore this pot until it reaches or crosses its stored value
    if (pickupSide[i] != 0) {
      int16_t d = (int16_t)out - lastOut[i];
      if (d != 0 && (d > 0 ? 1 : -1) == pickupSide[i]) continue;  // still on the starting side
      pickupSide[i] = 0;                                           // picked up - live again
    }

    if (out == lastOut[i]) continue;
    lastOut[i] = out;
    lastEditedPot = i;
    sendCC(i, out);
    displayDirty = true;
  }
}

// ===================== TEMP MODE =====================
void enterTempMode() {
  if (tempModeActive) return;
  tempModeActive = true;
  memcpy(storedOut, lastOut, sizeof(storedOut));
  for (uint8_t i = 0; i < 8; i++) pickupSide[i] = 0;  // everything live while held
  if (displayOK) display.invertDisplay(true);
}

void exitTempMode() {
  if (!tempModeActive) return;
  tempModeActive = false;

  for (uint8_t i = 0; i < 8; i++) {
    uint8_t target = storedOut[i];
    if (lastOut[i] != target) {  // snap back only what actually changed
      lastOut[i] = target;
      sendCC(i, target);
    }
    uint8_t cur = potOutput(i);
    pickupSide[i] = (cur == target) ? 0 : (cur < target ? -1 : 1);
  }

  if (displayOK) display.invertDisplay(false);
  displayDirty = true;
}

// ===================== FACTORY RESET =====================
void factoryReset() {
  if (tempModeActive) {
    tempModeActive = false;
    if (displayOK) display.invertDisplay(false);
  }

  if (displayOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(35, 28);
    display.print(F("MIDI reset"));
    display.display();
  }

  setDefaults();
  saveSettings();

  for (uint8_t i = 0; i < 8; i++) {
    pickupSide[i] = 0;
    lastOut[i] = potOutput(i);
    sendCC(i, lastOut[i]);
  }

  delay(1500);
  currentScreen = MAIN_SCREEN;
  displayDirty = true;
}

// ===================== BUTTONS =====================
void updateButton(Button &b, uint32_t now) {
  b.pressed = false;
  b.released = false;
  uint8_t r = digitalRead(b.pin);
  if (r != b.lastReading) {
    b.lastReading = r;
    b.changedAt = now;
  } else if (r != b.stable && (now - b.changedAt) >= DEBOUNCE_MS) {
    b.stable = r;
    if (r == BUTTON_PRESSED) b.pressed = true;
    else b.released = true;
  }
}

inline bool isDown(const Button &b) { return b.stable == BUTTON_PRESSED; }

void adjustParam(int8_t dir) {
  PotConfig &c = cfg[selectedPot];
  switch (assignEditMode) {
    case 0: selectedPot = (selectedPot + 8 + dir) % 8; break;
    case 1: {  // cycle 1..16 -> OMNI -> 1
      uint8_t idx = c.omni ? 16 : c.channel - 1;
      idx = (idx + 17 + dir) % 17;
      c.omni = (idx == 16);
      if (!c.omni) c.channel = idx + 1;
      break;
    }
    case 2: c.cc       = (c.cc + 128 + dir) & 0x7F; break;
    case 3: c.minValue = (c.minValue + 128 + dir) & 0x7F; break;
    case 4: c.maxValue = (c.maxValue + 128 + dir) & 0x7F; break;
    case 5: c.isInverted = !c.isInverted; break;
  }
  displayDirty = true;
}

void handleButtons(uint32_t now) {
  updateButton(btnAssign, now);
  updateButton(btnEnter, now);
  updateButton(btnPrev, now);
  updateButton(btnNext, now);

  // Factory reset: hold PREV + NEXT for 2 s on the main screen (fires once per hold)
  if (currentScreen == MAIN_SCREEN && isDown(btnPrev) && isDown(btnNext)) {
    if (!resetArmed) {
      resetArmed = true;
      resetHeldSince = now;
    } else if (!resetDone && (now - resetHeldSince) >= RESET_HOLD_MS) {
      resetDone = true;
      factoryReset();
      return;
    }
  } else {
    resetArmed = false;
    resetDone = false;
  }

  // ASSIGN: toggle screens (ignored while temp mode is held)
  if (btnAssign.pressed && !tempModeActive) {
    if (currentScreen == MAIN_SCREEN) {
      currentScreen = ASSIGN_SCREEN;
      selectedPot = lastEditedPot;
      assignEditMode = 0;
      showPotNumber = true;
      lastFlashTime = now;
      disarmPot8();
    } else {
      saveSettings();
      currentScreen = MAIN_SCREEN;
    }
    displayDirty = true;
    return;
  }

  if (currentScreen == MAIN_SCREEN) {
    if (btnEnter.pressed) enterTempMode();   // hold ENTER = temp override
    if (btnEnter.released) exitTempMode();   // release = snap back + catch-up
    return;
  }

  // ASSIGN screen
  if (btnEnter.pressed) {
    assignEditMode = (assignEditMode + 1) % 6;
    showPotNumber = true;
    disarmPot8();
    displayDirty = true;
  }
  if (btnPrev.pressed) { adjustParam(-1); disarmPot8(); }
  if (btnNext.pressed) { adjustParam(+1); disarmPot8(); }
}

// ===================== DISPLAY =====================
void renderDisplay() {
  if (!displayOK) return;

  if (currentScreen == MAIN_SCREEN) drawMainScreen();
  else drawAssignScreen();
  display.display();

#if WIRE_TIMEOUT_SUPPORTED
  // If the I2C bus glitched, the transfer timed out instead of hanging.
  // Re-initialise the OLED and redraw on the next frame.
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, false, false);
    if (tempModeActive) display.invertDisplay(true);
    displayDirty = true;
  }
#endif
}

// ===================== SETUP =====================
void setup() {
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX_SIG, INPUT);

  pinMode(ASSIGN_BUTTON, INPUT);
  pinMode(ENTER_BUTTON, INPUT);
  pinMode(PREV_BUTTON, INPUT);
  pinMode(NEXT_BUTTON, INPUT);
  Button *btns[4] = { &btnAssign, &btnEnter, &btnPrev, &btnNext };
  for (uint8_t i = 0; i < 4; i++) {
    btns[i]->stable = btns[i]->lastReading = digitalRead(btns[i]->pin);
  }

#if defined(ARDUINO_ARCH_AVR)
  // ADC clock 500 kHz (prescaler 32): ~4x faster conversions, still far more
  // accuracy than a 7-bit MIDI value needs.
  ADCSRA = (ADCSRA & ~0x07) | 0x05;
#endif

  // I2C first, with a timeout, so a stuck bus can never hang the loop
  Wire.begin();
#if WIRE_TIMEOUT_SUPPORTED
  Wire.setWireTimeout(3000, true);  // 3 ms, reset the TWI on timeout
#endif
  displayOK = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, true, false);
  if (displayOK) {
    display.setTextColor(SSD1306_WHITE);
    drawLoadingScreen();
  }

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.turnThruOff();

  loadSettings();

  delay(2000);

  for (uint8_t i = 0; i < 8; i++) {
    potPos[i] = readMux(i) >> 3;
    pickupSide[i] = 0;
    lastOut[i] = potOutput(i);
    sendCC(i, lastOut[i]);
  }

  lastEditedPot = 0;
  currentScreen = MAIN_SCREEN;
  displayDirty = true;
}

// ===================== LOOP =====================
void loop() {
  uint32_t now = millis();

  handleButtons(now);

  if ((now - lastScanTime) >= SCAN_INTERVAL_MS) {
    lastScanTime = now;
    scanPots();
  }

  if (currentScreen == ASSIGN_SCREEN && assignEditMode == 0 &&
      (now - lastFlashTime) >= FLASH_INTERVAL_MS) {
    lastFlashTime = now;
    showPotNumber = !showPotNumber;
    displayDirty = true;
  }

  // One redraw at most every DISPLAY_INTERVAL_MS, however many pots moved
  if (displayDirty && (now - lastDrawTime) >= DISPLAY_INTERVAL_MS) {
    lastDrawTime = now;
    displayDirty = false;
    renderDisplay();
  }
}

// ===================== SCREENS =====================
void drawLoadingScreen() {
  display.clearDisplay();
  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(41, 14);
  display.print(F("MC8P"));
  display.setTextSize(1);
  display.setCursor(53, 31);
  display.print(F("mini"));
  display.setCursor(55, 45);
  display.print(F(FW_VERSION));
#if SHOW_FREE_RAM && defined(ARDUINO_ARCH_AVR)
  display.setCursor(7, 5);
  display.print(F("RAM "));
  display.print(freeRam());
#endif
  display.display();
}

void drawMainScreen() {
  uint8_t value = lastOut[lastEditedPot];

  display.clearDisplay();
  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.drawRoundRect(6, 47, 115, 10, 1, SSD1306_WHITE);

  uint8_t barWidth = map(value, 0, 127, 0, 111);
  if (barWidth > 0) display.fillRect(8, 49, barWidth, 6, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(50, 10);
  display.print(F("POT "));
  display.print(lastEditedPot + 1);

  display.setTextSize(2);
  uint8_t numDigits = (value < 10) ? 1 : (value < 100) ? 2 : 3;
  display.setCursor((128 - numDigits * 12) / 2, 27);
  display.print(value);
}

void drawAssignScreen() {
  const PotConfig &c = cfg[selectedPot];

  display.clearDisplay();
  display.setTextSize(1);
  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);

  display.setCursor(8, 10);
  display.print(F("MIDI ASSIGN"));

  display.setCursor(8, 22);
  switch (assignEditMode) {
    case 0: display.print(F("SELECT POT")); break;
    case 1: display.print(F("EDIT CHANNEL")); break;
    case 2: display.print(F("EDIT CC")); break;
    case 3: display.print(F("EDIT MIN")); break;
    case 4: display.print(F("EDIT MAX")); break;
    default: display.print(F("EDIT DIR")); break;
  }

  if (!(assignEditMode == 0 && !showPotNumber)) {
    display.setCursor(93, 10);
    display.print(F("POT "));
    display.print(selectedPot + 1);
  }

  // Channel
  display.setCursor(8, 35);
  display.print(assignEditMode == 1 ? F("CH [") : F("CH "));
  if (c.omni) display.print(F("OMNI"));
  else display.print(c.channel);
  if (assignEditMode == 1) display.print(F("]"));

  // CC
  display.setCursor(8, 47);
  display.print(assignEditMode == 2 ? F("CC [") : F("CC "));
  display.print(c.cc);
  if (assignEditMode == 2) display.print(F("]"));

  // Range
  display.setCursor(66, 35);
  if (assignEditMode == 3) {
    display.print(F("MIN ["));
    display.print(c.minValue);
    display.print(F("]"));
  } else if (assignEditMode == 4) {
    display.print(F("MAX ["));
    display.print(c.maxValue);
    display.print(F("]"));
  } else {
    display.print(c.minValue);
    display.print(F("-"));
    display.print(c.maxValue);
  }

  // Direction
  display.setCursor(66, 47);
  display.print(assignEditMode == 5 ? F("DIR [") : F("DIR "));
  display.print(c.isInverted ? F("INV") : F("NOR"));
  if (assignEditMode == 5) display.print(F("]"));
}
