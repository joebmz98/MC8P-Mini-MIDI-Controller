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
bool lastAssignButtonState = HIGH;
bool lastEnterButtonState = HIGH;
bool lastPrevButtonState = HIGH;
bool lastNextButtonState = HIGH;

// -- FUNCTION TO READ MUX CHANNEL --
int
readMUXChannel(int channel) {
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

// -- SETUP --
void setup() {

  // Initialize MUX control pins
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(MUX_SIG, INPUT);

  // Initialise BUTTONS
  pinMode(ASSIGN_BUTTON, INPUT);
  pinMode(ENTER_BUTTON, INPUT);
  pinMode(PREV_BUTTON, INPUT);
  pinMode(NEXT_BUTTON, INPUT);

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
  updateDisplay(0, lastPotValues[0]);
}

// -- LOOP --
void loop() {
  // Loop through all 8 potentiometers
  for (int pot = 0; pot < 8; pot++) {
    unsigned long currentTime = millis();

    // Rate limit reads to prevent noise and reduce MUX switching
    if ((currentTime - lastReadTime[pot]) > readDelay) {

      // Read current pot value
      int rawValue = readMUXChannel(pot);
      int midiValue = map(rawValue, 0, 1023, 0, 127);

      // Check if value has changed by more than the threshold (2%)
      if (abs(midiValue - lastPotValues[pot]) >= CHANGE_THRESHOLD) {
        // Update last known value
        lastPotValues[pot] = midiValue;

        // Send MIDI CC message
        sendMIDICC(pot, midiValue);

        // Update display with last edited pot
        lastEditedPot = pot;
        updateDisplay(lastEditedPot, midiValue);
      }

      // Update read timer regardless of whether value changed
      lastReadTime[pot] = currentTime;
    }
  }

  // Small delay to prevent overwhelming the MUX
  delay(2);
}

// -- BUTTON HANDLING --
void readButtons() {
}

// -- DISPLAY SCREENS --
// -- LOADING SCREEN
void drawLoadingScreen() {

  // CLEAR
  display.clearDisplay();

  display.drawRoundRect(1, 1, 125, 62, 4, 1);

  display.setTextColor(1);
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setCursor(41, 14);
  display.print("MC8P");

  display.setTextSize(1);
  display.setCursor(53, 31);
  display.print("mini");

  display.setCursor(52, 45);
  display.print(FIRMWARE_VERSION);

  display.display();
}

// -- MAIN SCREEN
void drawMainScreen(int potNumber, int midiValue) {
  // CLEAR
  display.clearDisplay();

  display.drawRoundRect(1, 1, 125, 62, 4, 1);

  display.drawRoundRect(6, 47, 115, 10, 1, 1);

  // Draw progress bar based on MIDI value (0-127)
  int barWidth = map(midiValue, 0, 127, 0, 111);
  display.fillRect(8, 49, barWidth, 6, 1);

  display.setTextColor(1);
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setCursor(50, 8);

  // Print pot number (1-8)
  display.print("POT ");
  display.print(potNumber + 1);

  display.setTextSize(2);
  display.setCursor(47, 25);

  // Print MIDI value (0-127) with leading spaces for consistent positioning
  if (midiValue < 10) {
    display.print("  ");
    display.print(midiValue);
  } else if (midiValue < 100) {
    display.print(" ");
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

  display.drawRoundRect(1, 1, 125, 62, 4, 1);

  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(5, 5);
  display.print("MIDI ASSIGN");

  display.setCursor(20, 53);
  display.print("+ ADD");

  display.setCursor(61, 53);
  display.print("- REMOVE");

  display.setCursor(93, 5);
  display.print("POT 8");

  display.setCursor(81, 15);
  display.print("MSG 3/3");

  display.setCursor(4, 28);
  display.print("Ch OMNI ");

  display.setCursor(4, 40);
  display.print("126-127");

  display.setCursor(66, 28);
  display.print("CC 127");

  display.setCursor(66, 40);
  display.print("NOR");

  display.display();
}