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

// -- MIDI INSTANCE --
MIDI_CREATE_INSTANCE(HardwareSerial, Serial, MIDI);

// -- MIDI STRUCT
struct MIDIMessage {
  int cc = 7;
  int channel = 1;
  bool omni = false;  // OMNI mode flag
};

MIDIMessage potMessages[8]; // 8 pots, each with its own MIDI message
int activeMessages[8] = {1, 1, 1, 1, 1, 1, 1, 1}; // How many CCs each pot has

// -- FIRMWARE VERSION --
const char* FIRMWARE_VERSION = "1.0";

// -- VARIABLES --
int lastPotValues[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
int lastEditedPot = 0;
unsigned long lastReadTime[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
unsigned long readDelay = 5;  // milliseconds between reads for each pot

// -- POTS
// 2% threshold in MIDI value terms (2% of 127 = ~2.54, so we'll use 3)
const int CHANGE_THRESHOLD = 3;  // 2.36% of 127, close enough to 2%

// -- SCREENS --
enum ScreenState { MAIN_SCREEN,
                   ASSIGN_SCREEN,
                   CONFIRM_SCREEN };

ScreenState currentScreen = MAIN_SCREEN;

// -- ASSIGN SCREEN VARIABLES --
int selectedPot = 0;     // Currently selected pot (0-7)
int assignEditMode = 0;  // 0 = selecting pot, 1 = editing Channel, 2 = editing CC
unsigned long lastFlashTime = 0;
const unsigned long flashInterval = 500;  // Flash every 500ms
bool showPotNumber = true;                // For flashing effect

// -- BUTTONS
// -- BUTTON DEBOUNCING
unsigned long lastButtonPressTime = 0;
const unsigned long buttonDebounceDelay = 200;  // milliseconds
bool lastAssignButtonState = LOW;
bool lastEnterButtonState = HIGH;
bool lastPrevButtonState = HIGH;
bool lastNextButtonState = HIGH;

// -- FUNCTION TO READ MUX CHANNEL --
int readMUXChannel(int channel) {
  // Set MUX selection pins based on channel (0-7)
  digitalWrite(MUX_S0, channel & 1);
  digitalWrite(MUX_S1, (channel >> 1) & 1);
  digitalWrite(MUX_S2, (channel >> 2) & 1);
  digitalWrite(MUX_S3, (channel >> 3) & 1);

  // Small delay for MUX to settle
  delayMicroseconds(10);

  // Read analog value (0-1023)
  return analogRead(MUX_SIG);
}

// -- FUNCTION TO UPDATE DISPLAY --
void updateDisplay(int potNumber, int midiValue) {
  drawMainScreen(potNumber, midiValue);
}

// -- FUNCTION TO SEND MIDI CC MESSAGE --
void sendMIDICC(int potNumber, int midiValue) {
  // Send MIDI Control Change message based on the pot's configuration
  if (potMessages[potNumber].omni) {
    // OMNI mode: send to all 16 channels
    for (int ch = 1; ch <= 16; ch++) {
      MIDI.sendControlChange(potMessages[potNumber].cc, midiValue, ch);
    }
  } else {
    // Normal mode: send to specific channel
    MIDI.sendControlChange(potMessages[potNumber].cc, midiValue, potMessages[potNumber].channel);
  }
}

// -- FUNCTION FOR BUTTON HANDLING --
void readButtons() {
  unsigned long currentTime = millis();

  // BUTTON STATES
  bool assignButtonState = digitalRead(ASSIGN_BUTTON);
  bool enterButtonState = digitalRead(ENTER_BUTTON);
  bool prevButtonState = digitalRead(PREV_BUTTON);
  bool nextButtonState = digitalRead(NEXT_BUTTON);

  // ASSIGN BUTTON
  if (assignButtonState == HIGH && lastAssignButtonState == LOW) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {

      if (currentScreen == MAIN_SCREEN) {
        currentScreen = ASSIGN_SCREEN;
        selectedPot = lastEditedPot;  // Start with last edited pot
        assignEditMode = 0;           // Start in pot selection mode
        lastFlashTime = currentTime;
        drawAssignScreen();
      } else {
        currentScreen = MAIN_SCREEN;
        drawMainScreen(lastEditedPot, lastPotValues[lastEditedPot]);
      }

      lastButtonPressTime = currentTime;
    }
  }

  // ENTER BUTTON - Cycle through edit modes (0->1->2->0)
  if (enterButtonState == HIGH && lastEnterButtonState == LOW) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {

      if (currentScreen == ASSIGN_SCREEN) {
        assignEditMode = (assignEditMode + 1) % 3;  // Cycle: 0->1->2->0
        drawAssignScreen();                         // Refresh screen to show new mode
      }

      lastButtonPressTime = currentTime;
    }
  }

  // PREV BUTTON
  if (prevButtonState == HIGH && lastPrevButtonState == LOW) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {

      if (currentScreen == ASSIGN_SCREEN) {
        if (assignEditMode == 0) {
          // Selecting pot - go to previous pot
          selectedPot = (selectedPot - 1 + 8) % 8;
          drawAssignScreen();
        } else if (assignEditMode == 1) {
          // Editing Channel - cycle through: 1-16, then OMNI
          if (potMessages[selectedPot].omni) {
            // If OMNI is on, turn it off and go to channel 16
            potMessages[selectedPot].omni = false;
            potMessages[selectedPot].channel = 16;
          } else {
            // Decrease channel (1-16)
            if (potMessages[selectedPot].channel > 1) {
              potMessages[selectedPot].channel--;
            } else {
              // At channel 1, going previous enables OMNI
              potMessages[selectedPot].omni = true;
            }
          }
          drawAssignScreen();
        } else if (assignEditMode == 2) {
          // Editing CC - decrease CC value (0-127)
          if (potMessages[selectedPot].cc > 0) {
            potMessages[selectedPot].cc--;
          } else {
            potMessages[selectedPot].cc = 127; // Wrap around
          }
          drawAssignScreen();
        }
      }

      lastButtonPressTime = currentTime;
    }
  }

  // NEXT BUTTON
  if (nextButtonState == HIGH && lastNextButtonState == LOW) {
    if ((currentTime - lastButtonPressTime) > buttonDebounceDelay) {

      if (currentScreen == ASSIGN_SCREEN) {
        if (assignEditMode == 0) {
          // Selecting pot - go to next pot
          selectedPot = (selectedPot + 1) % 8;
          drawAssignScreen();
        } else if (assignEditMode == 1) {
          // Editing Channel - cycle through: OMNI, then 1-16
          if (potMessages[selectedPot].omni) {
            // If OMNI is on, turn it off and go to channel 1
            potMessages[selectedPot].omni = false;
            potMessages[selectedPot].channel = 1;
          } else {
            // Increase channel (1-16)
            if (potMessages[selectedPot].channel < 16) {
              potMessages[selectedPot].channel++;
            } else {
              // At channel 16, going next enables OMNI
              potMessages[selectedPot].omni = true;
            }
          }
          drawAssignScreen();
        } else if (assignEditMode == 2) {
          // Editing CC - increase CC value (0-127)
          if (potMessages[selectedPot].cc < 127) {
            potMessages[selectedPot].cc++;
          } else {
            potMessages[selectedPot].cc = 0; // Wrap around
          }
          drawAssignScreen();
        }
      }

      lastButtonPressTime = currentTime;
    }
  }

  // UPDATE LAST BUTTON STATES
  lastAssignButtonState = assignButtonState;
  lastEnterButtonState = enterButtonState;
  lastPrevButtonState = prevButtonState;
  lastNextButtonState = nextButtonState;
}

// -- SETUP --
void setup() {
  // Initialize pot messages with default values
  for (int i = 0; i < 8; i++) {
    potMessages[i].cc = 7;           // Default CC #7 (Volume)
    potMessages[i].channel = i + 1;  // Channels 1-8
    potMessages[i].omni = false;     // OMNI off by default
  }

  // Initialize MUX control pins
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX_SIG, INPUT);

  // Initialise BUTTONS with INPUT_PULLUP
  pinMode(ASSIGN_BUTTON, INPUT_PULLUP);
  pinMode(ENTER_BUTTON, INPUT_PULLUP);
  pinMode(PREV_BUTTON, INPUT_PULLUP);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);

  // Initialize MIDI
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.turnThruOff();

  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    for (;;)
      ;  // Don't proceed, loop forever
  }

  drawLoadingScreen();
  delay(2000);

  // Read initial pot values
  for (int i = 0; i < 8; i++) {
    int rawValue = readMUXChannel(i);
    lastPotValues[i] = map(rawValue, 0, 1023, 0, 127);
    sendMIDICC(i, lastPotValues[i]);
    delay(10);
  }

  // Show initial display
  currentScreen = MAIN_SCREEN;
  updateDisplay(0, lastPotValues[0]);
}

// -- LOOP --
void loop() {
  // Handle flashing for assign screen
  if (currentScreen == ASSIGN_SCREEN && assignEditMode == 0) {
    unsigned long currentTime = millis();
    if ((currentTime - lastFlashTime) > flashInterval) {
      showPotNumber = !showPotNumber;
      drawAssignScreen();  // Refresh to show/hide pot number
      lastFlashTime = currentTime;
    }
  }

  // Read potentiometers
  for (int pot = 0; pot < 8; pot++) {
    unsigned long currentTime = millis();
    if ((currentTime - lastReadTime[pot]) > readDelay) {
      int rawValue = readMUXChannel(pot);
      int midiValue = map(rawValue, 0, 1023, 0, 127);

      if (abs(midiValue - lastPotValues[pot]) >= CHANGE_THRESHOLD) {
        lastPotValues[pot] = midiValue;
        lastEditedPot = pot;

        // Only send MIDI message if we're on MAIN_SCREEN
        if (currentScreen == MAIN_SCREEN) {
          sendMIDICC(pot, midiValue);
          drawMainScreen(lastEditedPot, midiValue);
        }
      }
      lastReadTime[pot] = currentTime;
    }
  }

  readButtons();
  delay(5);
}

// -- DISPLAY SCREENS --
// -- LOADING SCREEN
void drawLoadingScreen() {
  display.clearDisplay();
  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setCursor(41, 14);
  display.print(F("MC8P"));
  display.setTextSize(1);
  display.setCursor(53, 31);
  display.print(F("mini"));
  display.setCursor(55, 45);
  display.print(FIRMWARE_VERSION);
  display.display();
}

// -- MAIN SCREEN
void drawMainScreen(int potNumber, int midiValue) {
  display.clearDisplay();
  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.drawRoundRect(6, 47, 115, 10, 1, SSD1306_WHITE);

  int barWidth = map(midiValue, 0, 127, 0, 111);
  if (barWidth > 0) {
    display.fillRect(8, 49, barWidth, 6, SSD1306_WHITE);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setCursor(50, 8);
  display.print(F("POT "));
  display.print(potNumber + 1);

  display.setTextSize(2);
  display.setCursor(47, 25);

  if (midiValue < 10) {
    display.print(F("  "));
    display.print(midiValue);
  } else if (midiValue < 100) {
    display.print(F(" "));
    display.print(midiValue);
  } else {
    display.print(midiValue);
  }

  display.display();
}

// -- ASSIGN SCREEN
void drawAssignScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  // Title
  display.setCursor(8, 5);
  display.print(F("MIDI ASSIGN"));

  // Show current edit mode
  display.setCursor(8, 17);
  if (assignEditMode == 0) {
    display.print(F("SELECT POT"));
  } else if (assignEditMode == 1) {
    display.print(F("EDIT CHANNEL"));
  } else {
    display.print(F("EDIT CC"));
  }

  // Pot number with flashing effect
  display.setCursor(93, 5);
  if (assignEditMode == 0) {
    // Flashing effect for pot selection
    if (showPotNumber) {
      display.print(F("POT "));
      display.print(selectedPot + 1);
    } else {
      display.print(F("     "));  // Clear the area (5 spaces)
    }
  } else {
    // Solid display when editing
    display.print(F("POT "));
    display.print(selectedPot + 1);
  }

  // Channel display 
  display.setCursor(8, 30);
  if (assignEditMode == 1) {
    // Highlight Channel when editing
    display.print(F("CH ["));
    if (potMessages[selectedPot].omni) {
      display.print(F("OMNI"));
    } else {
      display.print(potMessages[selectedPot].channel);
    }
    display.print(F("]"));
  } else {
    display.print(F("CH "));
    if (potMessages[selectedPot].omni) {
      display.print(F("OMNI"));
    } else {
      display.print(potMessages[selectedPot].channel);
    }
    display.print(F("   "));  // Padding
  }

  // CC value display 
  display.setCursor(8, 42);
  display.print(F("CC "));
  if (assignEditMode == 2) {
    // Highlight CC value when editing
    display.print(F("["));
    display.print(potMessages[selectedPot].cc);
    display.print(F("]"));
  } else {
    display.print(potMessages[selectedPot].cc);
    display.print(F("   "));  // Padding
  }

  // Range display
  display.setCursor(66, 30);
  display.print(F("0-127"));

  // Direction display
  display.setCursor(66, 42);
  display.print(F("NOR")); // NOR == Normal (low to high) // INV == Inverted (high to low)

  display.display();
}