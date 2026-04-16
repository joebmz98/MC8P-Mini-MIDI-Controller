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

// -- MIDI
// MIDI Control Change (CC) numbers for each pot
int midiCC[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };  // Customize these CC numbers as needed

// -- SCREENS --
enum ScreenState { MAIN_SCREEN,
                   ASSIGN_SCREEN,
                   CONFIRM_SCREEN };

ScreenState currentScreen = MAIN_SCREEN;

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
  // Send MIDI Control Change message
  MIDI.sendControlChange(midiCC[potNumber], midiValue, 1);  // Channel 1
}

// -- FUNCTION FOR BUTTON HANDLING --
void readButtons() {

  // BUTTON STATES
  bool assignButtonState = digitalRead(ASSIGN_BUTTON);
  bool enterButtonState = digitalRead(ENTER_BUTTON);
  bool prevButtonState = digitalRead(PREV_BUTTON);
  bool nextButtonState = digitalRead(NEXT_BUTTON);

  // BUTTON FUNCTIONS
  // ASSIGN
  // EDGE DETECTION: Only trigger on the "Rising Edge" (Press)
  if (assignButtonState == HIGH && lastAssignButtonState == LOW) {
    
    // DEBOUNCE: Ignore noise that happens faster than 200ms
    if (millis() - lastButtonPressTime > buttonDebounceDelay) {
      
      if (currentScreen == MAIN_SCREEN) {
        currentScreen = ASSIGN_SCREEN;
        drawAssignScreen();
      } else {
        currentScreen = MAIN_SCREEN;
        // Draw the main screen with the last known pot values
        drawMainScreen(lastEditedPot, lastPotValues[lastEditedPot]);
      }
      
      lastButtonPressTime = millis();
    }
  }

  // UPDATE LAST BUTTON STATES
  lastAssignButtonState = assignButtonState;
}

// -- SETUP --
void setup() {

  // -- DEBUG (Comment out for final build to save memory)
  // Serial.begin(9600);
  // while (!Serial); // Wait for Serial Monitor to open
  // Serial.println(F("MC8P Debug Mode Starting..."));

  // Initialize MUX control pins
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX_SIG, INPUT);

  // Initialise BUTTONS with INPUT_PULLUP for proper operation
  pinMode(ASSIGN_BUTTON, INPUT_PULLUP);
  pinMode(ENTER_BUTTON, INPUT_PULLUP);
  pinMode(PREV_BUTTON, INPUT_PULLUP);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);

  // Initialize MIDI
  MIDI.begin(1);  // Start MIDI on channel 1

  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    // Display initialization failed
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
  }

  // Show initial display
  currentScreen = MAIN_SCREEN;
  updateDisplay(0, lastPotValues[0]);
}

// -- LOOP --
void loop() {
  for (int pot = 0; pot < 8; pot++) {
    unsigned long currentTime = millis();
    if ((currentTime - lastReadTime[pot]) > readDelay) {
      int rawValue = readMUXChannel(pot);
      int midiValue = map(rawValue, 0, 1023, 0, 127);

      if (abs(midiValue - lastPotValues[pot]) >= CHANGE_THRESHOLD) {
        lastPotValues[pot] = midiValue;
        lastEditedPot = pot;

        if (currentScreen == MAIN_SCREEN) {
          // Debug output (comment out for final build)
          // Serial.print(F("Pot ")); Serial.print(pot);
          // Serial.print(F(" changed to: ")); Serial.println(midiValue);
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
  // CLEAR
  display.clearDisplay();

  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setCursor(41, 14);
  display.print(F("MC8P"));  // F() macro added

  display.setTextSize(1);
  display.setCursor(53, 31);
  display.print(F("mini"));  // F() macro added

  display.setCursor(52, 45);
  display.print(FIRMWARE_VERSION);

  display.display();
}

// -- MAIN SCREEN
void drawMainScreen(int potNumber, int midiValue) {
  // CLEAR
  display.clearDisplay();

  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);
  display.drawRoundRect(6, 47, 115, 10, 1, SSD1306_WHITE);

  // Draw progress bar based on MIDI value (0-127)
  int barWidth = map(midiValue, 0, 127, 0, 111);
  if (barWidth > 0) {
    display.fillRect(8, 49, barWidth, 6, SSD1306_WHITE);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setCursor(50, 8);

  // Print pot number (1-8)
  display.print(F("POT "));  // F() macro added
  display.print(potNumber + 1);

  display.setTextSize(2);
  display.setCursor(47, 25);

  // Print MIDI value (0-127) with leading spaces for consistent positioning
  if (midiValue < 10) {
    display.print(F("  "));  // F() macro added
    display.print(midiValue);
  } else if (midiValue < 100) {
    display.print(F(" "));   // F() macro added
    display.print(midiValue);
  } else {
    display.print(midiValue);
  }

  display.display();
}

// -- ASSIGN SCREEN
void drawAssignScreen(void) {
  // CLEAR
  display.clearDisplay();
  display.setTextSize(1);

  display.drawRoundRect(1, 1, 125, 62, 4, SSD1306_WHITE);

  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setCursor(5, 5);
  display.print(F("MIDI ASSIGN"));  // F() macro added

  display.setCursor(20, 53);
  display.print(F("+ ADD"));  // F() macro added

  display.setCursor(61, 53);
  display.print(F("- REMOVE"));  // F() macro added

  display.setCursor(93, 5);
  display.print(F("POT 8"));  // F() macro added

  display.setCursor(81, 15);
  display.print(F("MSG 3/3"));  // F() macro added

  display.setCursor(4, 28);
  display.print(F("Ch OMNI "));  // F() macro added

  display.setCursor(4, 40);
  display.print(F("126-127"));  // F() macro added

  display.setCursor(66, 28);
  display.print(F("CC 127"));  // F() macro added

  display.setCursor(66, 40);
  display.print(F("NOR"));  // F() macro added

  display.display();
}