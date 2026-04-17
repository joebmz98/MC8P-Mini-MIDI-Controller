// ********************* //
// MC8P mini             //
// MIDI Controller       //
// firmware version 1.0  //
// ********************* //
// controller by         //
// .axs instruments      //
// ********************* //

// -- LIBRARIES --
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <MIDI.h>
#include <Wire.h>
#include <EEPROM.h>

// -- OLED SETUP --
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// -- MUX --
#define MUX_SIG A0
#define MUX_S0 9
#define MUX_S1 8
#define MUX_S2 7
#define MUX_S3 6

// -- POTS --
// -- POTS ARE CONNECTED TO MUX
#define POT_1 0
#define POT_2 1
#define POT_3 2
#define POT_4 3
#define POT_5 4
#define POT_6 5
#define POT_7 6
#define POT_8 7

// -- BUTTONS --
#define ASSIGN_BUTTON 2
#define ENTER_BUTTON 3
#define PREV_BUTTON 4
#define NEXT_BUTTON 5

// -- EEPROM --
const int EEPROM_ADDRESS = 0;

// -- MIDI INSTANCE --
MIDI_CREATE_INSTANCE(HardwareSerial, Serial, MIDI);

// -- MIDI STRUCT
struct MIDIMessage {
  uint8_t cc = 7;
  uint8_t channel = 1;
  bool omni = false;
  uint8_t minValue = 0;
  uint8_t maxValue = 127;
  bool isInverted = false;
};

MIDIMessage potMessages[8];

// -- FIRMWARE VERSION --
const char FIRMWARE_VERSION[] = "1.0";

// -- VARIABLES --
int lastRawValues[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
int lastMidiValues[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
int lastEditedPot = 0;
unsigned long lastReadTime[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
const unsigned long readDelay = 5;

// Fixed threshold for stability (2% of 127 = ~3)
const int CHANGE_THRESHOLD = 2;

// -- SCREENS --
enum ScreenState { MAIN_SCREEN,
                   ASSIGN_SCREEN,
                   CONFIRM_SCREEN };
ScreenState currentScreen = MAIN_SCREEN;

// -- ASSIGN SCREEN VARIABLES --
int selectedPot = 0;
int assignEditMode = 0;
unsigned long lastFlashTime = 0;
const unsigned long flashInterval = 500;
bool showPotNumber = true;

// -- BUTTON DEBOUNCING --
unsigned long lastButtonPressTime = 0;
const unsigned long buttonDebounceDelay = 100;
bool lastAssignButtonState = HIGH;
bool lastEnterButtonState = HIGH;
bool lastPrevButtonState = HIGH;
bool lastNextButtonState = HIGH;
unsigned long bothButtonsPressTime = 0;
bool bothButtonsHeld = false;

// -- TEMP MODE VARIABLES --
bool tempModeActive = false;
int storedMidiValues[8];  // Store original values when entering temp mode
unsigned long enterButtonPressTime = 0;
const unsigned long longPressTime = 300;  // 300ms to detect hold vs click
bool catchupModeActive = false;

// -- EEPROM FUNCTIONS --
// -- SAVE EEPROM
void saveSettings() {
  EEPROM.put(EEPROM_ADDRESS, potMessages);
}

// -- LOAD FROM EEPROM
void loadSettings() {
  // We read the data into our array
  EEPROM.get(EEPROM_ADDRESS, potMessages);

  // Sanity Check: If EEPROM is empty (new chip), it returns 255.
  // We check if the first channel is within valid range (1-16) or OMNI.
  // If not, we reset to defaults to avoid weird behavior.
  if (potMessages[0].channel > 16 && !potMessages[0].omni) {
    for (int i = 0; i < 8; i++) {
      potMessages[i].cc = 7;
      potMessages[i].channel = i + 1;
      potMessages[i].omni = false;
      potMessages[i].minValue = 0;
      potMessages[i].maxValue = 127;
      potMessages[i].isInverted = false;
    }
    saveSettings();  // Save these defaults
  }
}

// -- FUNCTION TO READ MUX CHANNEL --
int readMUXChannel(int channel) {
  digitalWrite(MUX_S0, channel & 1);
  digitalWrite(MUX_S1, (channel >> 1) & 1);
  digitalWrite(MUX_S2, (channel >> 2) & 1);
  digitalWrite(MUX_S3, (channel >> 3) & 1);
  delayMicroseconds(10);
  return analogRead(MUX_SIG);
}

// -- FUNCTION TO MAP RAW VALUE TO MIDI VALUE --
int mapRawToMIDI(int potNumber, int rawValue) {
  uint8_t minVal = potMessages[potNumber].minValue;
  uint8_t maxVal = potMessages[potNumber].maxValue;

  // Ensure min is less than max
  if (minVal > maxVal) {
    uint8_t temp = minVal;
    minVal = maxVal;
    maxVal = temp;
  }

  int mappedValue;
  if (potMessages[potNumber].isInverted) {
    mappedValue = map(rawValue, 0, 1023, maxVal, minVal);
  } else {
    mappedValue = map(rawValue, 0, 1023, minVal, maxVal);
  }

  return constrain(mappedValue, 0, 127);
}

// -- FUNCTION TO SEND MIDI CC MESSAGE --
void sendMIDICC(int potNumber, int midiValue) {
  if (potMessages[potNumber].omni) {
    for (int ch = 1; ch <= 16; ch++) {
      MIDI.sendControlChange(potMessages[potNumber].cc, midiValue, ch);
    }
  } else {
    MIDI.sendControlChange(potMessages[potNumber].cc, midiValue, potMessages[potNumber].channel);
  }
}

// -- FUNCTION TO ENTER TEMP MODE --
void enterTempMode() {
  if (!tempModeActive && currentScreen == MAIN_SCREEN) {
    tempModeActive = true;

    // Store the current MIDI values (these are the values we'll return to)
    for (int i = 0; i < 8; i++) {
      storedMidiValues[i] = lastMidiValues[i];
    }

    // Also store the current raw values to know where the pots physically are
    for (int i = 0; i < 8; i++) {
      lastRawValues[i] = readMUXChannel(i);
    }

    // Invert display for visual feedback
    display.invertDisplay(true);
  }
}

// -- FUNCTION TO EXIT TEMP MODE --
void exitTempMode() {
  if (tempModeActive) {
    tempModeActive = false;
    
    // Activate catch-up mode
    catchupModeActive = true;
    
    // Restore display
    display.invertDisplay(false);
    
    // Force lastMidiValues to the stored values (what we want to return to)
    // This ensures the display shows the target values
    for (int i = 0; i < 8; i++) {
      lastMidiValues[i] = storedMidiValues[i];
    }
    
    // Send the stored MIDI values immediately (this jumps back to original state)
    for (int i = 0; i < 8; i++) {
      sendMIDICC(i, storedMidiValues[i]);
    }
    
    // Redraw main screen with the stored values
    drawMainScreen(lastEditedPot, lastMidiValues[lastEditedPot]);
  }
}

// -- FUNCTION TO HANDLE CATCH-UP AFTER TEMP MODE --
void handleCatchup() {
  if (!catchupModeActive) return;
  
  bool allCaughtUp = true;
  
  // Check each pot to see if it's caught up to its stored value
  for (int i = 0; i < 8; i++) {
    // Read the current physical position of the pot
    int rawValue = readMUXChannel(i);
    int currentPhysicalValue = mapRawToMIDI(i, rawValue);
    int targetValue = storedMidiValues[i];
    
    // Has the physical pot reached the target value?
    bool hasReachedTarget = false;
    
    // Check if the physical pot position matches the target value
    // We need to allow for a small tolerance because of analog readings
    if (abs(currentPhysicalValue - targetValue) <= CHANGE_THRESHOLD) {
      hasReachedTarget = true;
    }
    
    if (hasReachedTarget) {
      // This pot has caught up - update lastMidiValues to the physical value
      lastMidiValues[i] = currentPhysicalValue;
      lastRawValues[i] = rawValue;
      
      // Send the current MIDI value (which is now the target value)
      sendMIDICC(i, currentPhysicalValue);
      
      // Update display if this is the last edited pot
      if (i == lastEditedPot && currentScreen == MAIN_SCREEN) {
        drawMainScreen(lastEditedPot, lastMidiValues[lastEditedPot]);
      }
    } else {
      // This pot hasn't caught up yet
      allCaughtUp = false;
      
      // IMPORTANT: Force lastMidiValues[i] to stay at the target value
      // This prevents any MIDI messages from being sent while in catch-up mode
      if (lastMidiValues[i] != targetValue) {
        lastMidiValues[i] = targetValue;
      }
    }
  }
  
  // If all pots have caught up, exit catch-up mode
  if (allCaughtUp) {
    catchupModeActive = false;
  }
}

// -- FACTORY RESET FUNCTION --
void factoryReset() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(35, 28);
  display.print(F("MIDI reset"));
  display.display();

  for (int i = 0; i < 8; i++) {
    potMessages[i].cc = 7;
    potMessages[i].channel = i + 1;
    potMessages[i].omni = false;
    potMessages[i].minValue = 0;
    potMessages[i].maxValue = 127;
    potMessages[i].isInverted = false;
  }
  saveSettings();

  // Reset last MIDI values to match new settings
  for (int i = 0; i < 8; i++) {
    int rawValue = readMUXChannel(i);
    lastRawValues[i] = rawValue;
    lastMidiValues[i] = mapRawToMIDI(i, rawValue);
    sendMIDICC(i, lastMidiValues[i]);
  }

  delay(1500);
  currentScreen = MAIN_SCREEN;
  drawMainScreen(lastEditedPot, lastMidiValues[lastEditedPot]);
}

// -- FUNCTION FOR BUTTON HANDLING --
void readButtons() {
  unsigned long currentTime = millis();

  bool assignButtonState = digitalRead(ASSIGN_BUTTON);
  bool enterButtonState = digitalRead(ENTER_BUTTON);
  bool prevButtonState = digitalRead(PREV_BUTTON);
  bool nextButtonState = digitalRead(NEXT_BUTTON);

  // Check for both PREV and NEXT buttons held in MAIN screen
  if (currentScreen == MAIN_SCREEN && prevButtonState == HIGH && nextButtonState == HIGH) {
    if (!bothButtonsHeld) {
      bothButtonsPressTime = currentTime;
      bothButtonsHeld = true;
    } else if ((currentTime - bothButtonsPressTime) >= 2000) {
      // Both buttons held for 2 seconds - perform factory reset
      factoryReset();
      bothButtonsHeld = false;
      lastButtonPressTime = currentTime;
      // Reset button states to prevent immediate re-triggering
      lastPrevButtonState = prevButtonState;
      lastNextButtonState = nextButtonState;
      return;
    }
  } else {
    bothButtonsHeld = false;
  }

  // ASSIGN BUTTON
  if (assignButtonState == LOW && lastAssignButtonState == HIGH) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {
      if (currentScreen == MAIN_SCREEN) {
        currentScreen = ASSIGN_SCREEN;
        selectedPot = lastEditedPot;
        assignEditMode = 0;
        lastFlashTime = currentTime;
        //Serial.println("-- ASSIGN_BUTTON PRESSED >> Moving to ASSIGN_SCREEN");
        drawAssignScreen();
      } else {
        saveSettings();  // SAVE ON EXIT
        currentScreen = MAIN_SCREEN;
        //Serial.println("-- ASSIGN_BUTTON PRESSED >> Moving to MAIN_SCREEN");
        drawMainScreen(lastEditedPot, lastMidiValues[lastEditedPot]);
      }
      lastButtonPressTime = currentTime;
    }
  }
  lastAssignButtonState = assignButtonState;

  // ENTER BUTTON
  if (enterButtonState == HIGH && lastEnterButtonState == LOW) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {
      if (currentScreen == ASSIGN_SCREEN) {
        assignEditMode = (assignEditMode + 1) % 6;
        drawAssignScreen();
      } else if (currentScreen == MAIN_SCREEN) {
        //Serial.println(F("-- ENTER BUTTON HELD >> ACTIVATING TEMP MODE"));
        enterTempMode();
      }
      lastButtonPressTime = currentTime;
    }
  }

  if (enterButtonState == LOW && lastEnterButtonState == HIGH) {
    if (currentScreen == MAIN_SCREEN && tempModeActive) {
      //Serial.println(F("-- ENTER BUTTON RELEASED >> EXITING TEMP MODE"));
      exitTempMode();
    }
  }

  lastEnterButtonState = enterButtonState;

  // Only process other buttons in ASSIGN_SCREEN
  if (currentScreen != ASSIGN_SCREEN) {
    lastPrevButtonState = prevButtonState;
    lastNextButtonState = nextButtonState;
    return;
  }

  // PREV BUTTON
  if (prevButtonState == LOW && lastPrevButtonState == HIGH) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {
      if (assignEditMode == 0) {
        selectedPot = (selectedPot - 1 + 8) % 8;
        drawAssignScreen();
      } else if (assignEditMode == 1) {
        if (potMessages[selectedPot].omni) {
          potMessages[selectedPot].omni = false;
          potMessages[selectedPot].channel = 16;
        } else {
          if (potMessages[selectedPot].channel > 1) {
            potMessages[selectedPot].channel--;
          } else {
            potMessages[selectedPot].omni = true;
          }
        }
        drawAssignScreen();
      } else if (assignEditMode == 2) {
        if (potMessages[selectedPot].cc > 0) {
          potMessages[selectedPot].cc--;
        } else {
          potMessages[selectedPot].cc = 127;
        }
        drawAssignScreen();
      } else if (assignEditMode == 3) {
        if (potMessages[selectedPot].minValue > 0) {
          potMessages[selectedPot].minValue--;
        } else {
          potMessages[selectedPot].minValue = 127;
        }
        drawAssignScreen();
      } else if (assignEditMode == 4) {
        if (potMessages[selectedPot].maxValue > 0) {
          potMessages[selectedPot].maxValue--;
        } else {
          potMessages[selectedPot].maxValue = 127;
        }
        drawAssignScreen();
      } else if (assignEditMode == 5) {
        potMessages[selectedPot].isInverted = !potMessages[selectedPot].isInverted;
        drawAssignScreen();
      }
      lastButtonPressTime = currentTime;
    }
  }
  lastPrevButtonState = prevButtonState;

  // NEXT BUTTON
  if (nextButtonState == LOW && lastNextButtonState == HIGH) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {
      if (assignEditMode == 0) {
        selectedPot = (selectedPot + 1) % 8;
        drawAssignScreen();
      } else if (assignEditMode == 1) {
        if (potMessages[selectedPot].omni) {
          potMessages[selectedPot].omni = false;
          potMessages[selectedPot].channel = 1;
        } else {
          if (potMessages[selectedPot].channel < 16) {
            potMessages[selectedPot].channel++;
          } else {
            potMessages[selectedPot].omni = true;
          }
        }
        drawAssignScreen();
      } else if (assignEditMode == 2) {
        if (potMessages[selectedPot].cc < 127) {
          potMessages[selectedPot].cc++;
        } else {
          potMessages[selectedPot].cc = 0;
        }
        drawAssignScreen();
      } else if (assignEditMode == 3) {
        if (potMessages[selectedPot].minValue < 127) {
          potMessages[selectedPot].minValue++;
        } else {
          potMessages[selectedPot].minValue = 0;
        }
        drawAssignScreen();
      } else if (assignEditMode == 4) {
        if (potMessages[selectedPot].maxValue < 127) {
          potMessages[selectedPot].maxValue++;
        } else {
          potMessages[selectedPot].maxValue = 0;
        }
        drawAssignScreen();
      } else if (assignEditMode == 5) {
        potMessages[selectedPot].isInverted = !potMessages[selectedPot].isInverted;
        drawAssignScreen();
      }
      lastButtonPressTime = currentTime;
    }
  }
  lastNextButtonState = nextButtonState;
}

// -- EDIT PARAMETER WITH POT 8
void handlePot8Editing() {
  if (currentScreen != ASSIGN_SCREEN) return;

  // Read Pot 8 (Mux Channel 7)
  int pot8Raw = readMUXChannel(7);

  // Use a higher threshold for Pot 8 to prevent screen flickering
  // while you are navigating other menus
  if (abs(pot8Raw - lastRawValues[7]) > 10) {
    lastRawValues[7] = pot8Raw;
    bool valueChanged = false;

    switch (assignEditMode) {
      case 0:  // Select Pot (0-7)
        {
          int newPot = map(pot8Raw, 0, 1023, 0, 7);
          if (newPot != selectedPot) {
            selectedPot = newPot;
            valueChanged = true;
          }
        }
        break;

      case 1:  // Channel (1-16 + OMNI)
        {
          int newVal = map(pot8Raw, 0, 1023, 1, 17);
          if (newVal == 17) {
            if (!potMessages[selectedPot].omni) {
              potMessages[selectedPot].omni = true;
              valueChanged = true;
            }
          } else {
            if (potMessages[selectedPot].omni || potMessages[selectedPot].channel != newVal) {
              potMessages[selectedPot].omni = false;
              potMessages[selectedPot].channel = newVal;
              valueChanged = true;
            }
          }
        }
        break;

      case 2:  // CC (0-127)
        {
          int newCC = map(pot8Raw, 0, 1023, 0, 127);
          if (newCC != potMessages[selectedPot].cc) {
            potMessages[selectedPot].cc = newCC;
            valueChanged = true;
          }
        }
        break;

      case 3:  // Min Value (0-127)
        {
          int newMin = map(pot8Raw, 0, 1023, 0, 127);
          if (newMin != potMessages[selectedPot].minValue) {
            potMessages[selectedPot].minValue = newMin;
            valueChanged = true;
          }
        }
        break;

      case 4:  // Max Value (0-127)
        {
          int newMax = map(pot8Raw, 0, 1023, 0, 127);
          if (newMax != potMessages[selectedPot].maxValue) {
            potMessages[selectedPot].maxValue = newMax;
            valueChanged = true;
          }
        }
        break;

      case 5:  // Direction (NOR/INV)
        {
          bool newInv = (pot8Raw > 512);
          if (newInv != potMessages[selectedPot].isInverted) {
            potMessages[selectedPot].isInverted = newInv;
            valueChanged = true;
          }
        }
        break;
    }

    if (valueChanged) {
      drawAssignScreen();
    }
  }
}

// -- SETUP --
void setup() {

  // Initialize Serial for debugging
  //Serial.begin(9600);
  while (!Serial && millis() < 4000)
    ;

  //Serial.println(F("\n\nMC8P mini Debug Mode"));
  //Serial.println(F("Firmware v1.0 with debugging"));


  // Initialize pot messages with default values
  for (int i = 0; i < 8; i++) {
    potMessages[i].cc = 7;
    potMessages[i].channel = i + 1;
    potMessages[i].omni = false;
    potMessages[i].minValue = 0;
    potMessages[i].maxValue = 127;
    potMessages[i].isInverted = false;
  }

  // Initialize MUX control pins
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX_SIG, INPUT);

  // Initialize BUTTONS with INPUT_PULLUP
  pinMode(ASSIGN_BUTTON, INPUT_PULLUP);
  pinMode(ENTER_BUTTON, INPUT_PULLUP);
  pinMode(PREV_BUTTON, INPUT_PULLUP);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);

  lastAssignButtonState = digitalRead(ASSIGN_BUTTON);
  lastEnterButtonState = digitalRead(ENTER_BUTTON);
  lastPrevButtonState = digitalRead(PREV_BUTTON);
  lastNextButtonState = digitalRead(NEXT_BUTTON);

  // Initialize display
  Wire.begin();
  Wire.setClock(100000);  // Set to Standard 100kHz for stability
  delay(100);
  //Wire.setWireTimeout(3000, true);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    //Serial.println(F("ERROR: Display not found!"));
  } else {

    //Serial.println(F("Display initialized"));
    display.clearDisplay();
    display.display();
  }

  // Initialize MIDI
  MIDI.begin(MIDI_CHANNEL_OMNI);
  delay(100);
  MIDI.turnThruOff();


  // -- LOAD SETTINGS --
  loadSettings();
  //Serial.println(F("Settings loaded"));

  drawLoadingScreen();
  delay(2000);

  // Read initial pot values
  for (int i = 0; i < 8; i++) {
    int rawValue = readMUXChannel(i);
    lastRawValues[i] = rawValue;
    lastMidiValues[i] = mapRawToMIDI(i, rawValue);
    sendMIDICC(i, lastMidiValues[i]);
    delay(10);
  }

  currentScreen = MAIN_SCREEN;
  drawMainScreen(0, lastMidiValues[0]);
  //Serial.println(F("Setup complete, entering main loop"));
}

// -- LOOP --
void loop() {
  // Handle flashing for assign screen
  if (currentScreen == ASSIGN_SCREEN && assignEditMode == 0) {
    unsigned long currentTime = millis();
    if ((currentTime - lastFlashTime) > flashInterval) {
      showPotNumber = !showPotNumber;
      drawAssignScreen();
      lastFlashTime = currentTime;
    }
  }

  // Read potentiometers 1-7
  for (int pot = 0; pot < 7; pot++) {
    unsigned long currentTime = millis();
    if ((currentTime - lastReadTime[pot]) > readDelay) {
      int rawValue = readMUXChannel(pot);
      int midiValue = mapRawToMIDI(pot, rawValue);

      // In catch-up mode, only update values when passing stored position
      if (catchupModeActive) {
        // Don't automatically update - let handleCatchup() manage it
        lastReadTime[pot] = currentTime;
        continue;
      }

      // Normal operation (not in catch-up mode)
      if (abs(midiValue - lastMidiValues[pot]) >= CHANGE_THRESHOLD) {
        lastRawValues[pot] = rawValue;
        lastMidiValues[pot] = midiValue;
        lastEditedPot = pot;

        if (currentScreen == MAIN_SCREEN) {
          sendMIDICC(pot, midiValue);
          drawMainScreen(lastEditedPot, midiValue);
        }
      }
      lastReadTime[pot] = currentTime;
    }
  }

  // Handle Pot 8 specifically
  if (currentScreen == ASSIGN_SCREEN) {
    handlePot8Editing();
  } else {
    // Standard MIDI behavior for Pot 8 when on Main Screen
    int rawValue = readMUXChannel(7);
    int midiValue = mapRawToMIDI(7, rawValue);

    // In catch-up mode, handle Pot 8 separately
    if (catchupModeActive) {
      // Let handleCatchup() manage it
    } else if (abs(midiValue - lastMidiValues[7]) >= CHANGE_THRESHOLD) {
      lastRawValues[7] = rawValue;
      lastMidiValues[7] = midiValue;
      lastEditedPot = 7;
      sendMIDICC(7, midiValue);
      drawMainScreen(7, midiValue);
    }
  }

  // Handle catch-up mode after reading pots
  handleCatchup();

  readButtons();
  delay(2);
}

// -- DISPLAY SCREENS --
void drawLoadingScreen() {

  display.clearDisplay();
  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(41, 14);
  display.print(F("MC8P"));
  display.setTextSize(1);
  display.setCursor(53, 31);
  display.print(F("mini"));
  display.setCursor(55, 45);
  display.print(FIRMWARE_VERSION);
  display.display();
}

void drawMainScreen(int potNumber, int midiValue) {
  display.clearDisplay();
  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.drawRoundRect(6, 47, 115, 10, 1, SSD1306_WHITE);

  int barWidth = map(midiValue, 0, 127, 0, 111);
  if (barWidth > 0) {
    display.fillRect(8, 49, barWidth, 6, SSD1306_WHITE);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(50, 8);
  display.print(F("POT "));
  display.print(potNumber + 1);

  display.setTextSize(2);

  // Calculate horizontal center for the MIDI value
  // Text size 2 means each character is approximately 12 pixels wide (6x2 for width)
  // Get the number of digits in the MIDI value
  int numDigits;
  if (midiValue < 10) numDigits = 1;
  else if (midiValue < 100) numDigits = 2;
  else numDigits = 3;

  // Calculate starting X position to center the number
  // Screen width is 128, text width is roughly (numDigits * 12)
  int textWidth = numDigits * 12;
  int startX = (128 - textWidth) / 2;

  display.setCursor(startX, 25);
  display.print(midiValue);

  display.display();
}

void drawAssignScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setCursor(8, 5);
  display.print(F("MIDI ASSIGN"));

  // Show current edit mode
  display.setCursor(8, 17);
  if (assignEditMode == 0) display.print(F("SELECT POT"));
  else if (assignEditMode == 1) display.print(F("EDIT CHANNEL"));
  else if (assignEditMode == 2) display.print(F("EDIT CC"));
  else if (assignEditMode == 3) display.print(F("EDIT MIN"));
  else if (assignEditMode == 4) display.print(F("EDIT MAX"));
  else display.print(F("EDIT DIR"));

  // Pot number
  display.setCursor(93, 5);
  if (assignEditMode == 0 && !showPotNumber) {
    display.print(F("     "));
  } else {
    display.print(F("POT "));
    display.print(selectedPot + 1);
  }

  // Channel display
  display.setCursor(8, 30);
  if (assignEditMode == 1) display.print(F("CH ["));
  else display.print(F("CH "));

  if (potMessages[selectedPot].omni) display.print(F("OMNI"));
  else display.print(potMessages[selectedPot].channel);

  if (assignEditMode == 1) display.print(F("]"));
  else display.print(F("   "));

  // CC display
  display.setCursor(8, 42);
  if (assignEditMode == 2) display.print(F("CC ["));
  else display.print(F("CC "));

  display.print(potMessages[selectedPot].cc);

  if (assignEditMode == 2) display.print(F("]"));
  else display.print(F("   "));

  // Range display
  display.setCursor(66, 30);
  if (assignEditMode == 3) display.print(F("MIN ["));
  else if (assignEditMode == 4) display.print(F("MAX ["));

  if (assignEditMode == 3) display.print(potMessages[selectedPot].minValue);
  else if (assignEditMode == 4) display.print(potMessages[selectedPot].maxValue);
  else {
    display.print(potMessages[selectedPot].minValue);
    display.print(F("-"));
    display.print(potMessages[selectedPot].maxValue);
  }

  if (assignEditMode == 3 || assignEditMode == 4) display.print(F("]"));

  // Direction display
  display.setCursor(66, 42);
  if (assignEditMode == 5) display.print(F("DIR ["));
  else display.print(F("DIR "));

  if (potMessages[selectedPot].isInverted) display.print(F("INV"));
  else display.print(F("NOR"));

  if (assignEditMode == 5) display.print(F("]"));

  display.display();
}