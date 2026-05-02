# MC8P Mini MIDI Controller

---

# **8-Pot Mini MIDI Controller**

A compact, open-source MIDI controller with **8 potentiometers**, each with **fully assignable MIDI CCs**, built for **Arduino NANO R3/R4 (Rev1)** or the **Pro Micro (Rev2)**.

<img src="https://github.com/joebmz98/MC8P-Mini-MIDI-Controller/blob/main/images/photos/P1155458.jpg?raw=true" alt="Photo of the assembled MC8P mini MIDI Controller" width="600"/>

---

## **Features**
- **8x Potentiometers** with smooth analog control via 16-channel MUX
- **Arduino NANO / Pro Micro (Rev2) Compatible** – Easy to source and program
- **SSD1306 I2C OLED (128x64)** for the UI
- **Per-Pot CC Assignment** – Full MIDI CC customization
- **4x Tactile Switches** for UI navigation/configuration
- **Standalone Operation** – No computer needed after setup
- **EEPROM Settings Storage** – Retains configurations after power-off
- **Temporary Override Mode** – Make temporary adjustments that snap back
- **MIDI over USB** (Pro Micro Rev ONLY) – Class-compliant, plug-and-play
- **Open-Source Firmware** (Arduino IDE-compatible)

---

## **Bill of Materials (BOM)**

| Component                  | Qty | Notes                          | Links / Part Numbers |
|----------------------------|-----|--------------------------------|----------------------|
| Arduino NANO / Pro Micro   | 1   | Main microcontroller           | [Arduino NANO R3](https://www.aliexpress.com/w/wholesale-arduino-nano.html?spm=a2g0o.productlist.search.0) / [Pro Micro](https://www.aliexpress.com/w/wholesale-pro-micro-arduino.html?spm=a2g0o.best.search.0) |
| 16-Channel MUX (74HC4067)  | 1   | For reading 8 potentiometers   | [AliExpress](https://www.aliexpress.com/w/wholesale-cd74hc4067.html?spm=a2g0o.productlist.search.0) / CD74HC4067M96 (JLCPCB) |
| SSD1306 OLED (128×64)      | 1   | I²C interface                  | [AliExpress](https://www.aliexpress.com/w/wholesale-ssd1306.html?spm=a2g0o.productlist.search.0) |
| B100K Potentiometers       | 8   | Linear taper                   | [AliExpress](https://www.aliexpress.com/item/1005003523799166.html?spm=a2g0o.productlist.main.4.41b16412N16EC4&aem_p4p_detail=2026050202175910656752474918880005794025&algo_pvid=bb6ca2a0-bb84-4b86-8104-57e7c6a92d56&algo_exp_id=bb6ca2a0-bb84-4b86-8104-57e7c6a92d56-3&pdp_ext_f=%7B"order"%3A"368"%2C"eval"%3A"1"%2C"fromPage"%3A"search"%7D&pdp_npi=6%40dis%21GBP%210.33%210.33%21%21%210.43%210.43%21%402151e6dc17777134792648341e4c8f%2112000026166615223%21sea%21UK%216042796089%21X%211%210%21n_tag%3A-29919%3Bd%3A582ad943%3Bm03_new_user%3A-29895&curPageLogUid=hZYrDWLC06u8&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A1005003523799166%7C_p_origin_prod%3A&search_p4p_id=2026050202175910656752474918880005794025_1) |
| Tactile Switches           | 4   | For menu navigation (ASSIGN, ENTER, PREV, NEXT) | K2-6639DP-B4SW-04 (JLCPCB) |
| 10k Resistors (SMD)        | 7   | Pull-up for buttons and misc   | 0603WAF1002T5E (JLCPCB) |
| 100nF SMD Capacitor        | 1   | Power rail stabilisation       | 1206B104K101CT (JLCPCB) |
| 10nF SMD Capacitors        | 8   | Potentiometer filtering        | 1206B103K500NT (JLCPCB) |
| 220Ω SMD Resistors         | 2   | For MIDI circuitry             | 0603WAF2200T5E (JLCPCB) |
| PJ307 Stereo Headphone Socket | 1 | 3.5mm MIDI TRS Out (Type A) | — |
| Reset Button (SMD)         | 1   | Rev2 only - Reset button       | KMR221GLFS (JLCPCB) |
| 0.1" Header Pins           | 1   | For connecting components      | — |
| PCB                        | 1   | Custom design                  | — |
| Potentiometer Knobs        | 8   | For B100K potentiometers       | — |

---

## **Available Purchase Options**
**DIY Kits** - Only NANO version (rev1) available → [**Purchase from my Tindie store**](https://www.tindie.com/stores/axsinstruments/)

---

## **Installation & Setup**
### **1. Flashing the Firmware**
- Install **Arduino IDE**
- Install required libraries (see Building from Source section)
- Open the `.ino` file in Arduino IDE
- Select your board (**Arduino NANO R3/R4 or ProMicro**)
- Upload the firmware

### **2. Hardware Assembly**
- Refer to the *Build Guide* (coming soon)

### **3. First Boot**
- Power via **USB** on the Arduino NANO
- Use the **OLED menu** to assign CCs

---

## **Configuration**

### Button Functions

| Button | Primary Function |
|--------|-----------------|
| ASSIGN | Enter/exit MIDI assignment menu |
| ENTER | Enter temporary override mode (hold), confirm selections |
| PREV | Decrease values, navigate up |
| NEXT | Increase values, navigate down |
| PREV + NEXT (hold 2 sec) | Factory reset |

### Screen Hierarchy

The interface is organized into two main screens:

**MAIN SCREEN** → **ASSIGN SCREEN** → parameter editing

### Main Screen

This is your default operating view. The screen displays:
- Current selected pot number (1-8)
- A horizontal bar graph showing the current MIDI value (0-127)
- The numeric MIDI value centered on screen

From the main screen:
- Press **ASSIGN** to enter the assign menu
- Press and hold **ENTER** to activate temporary override mode

### Temporary Override Mode

When you press and hold ENTER on the main screen, the controller enters temporary override mode:
- The display inverts as visual feedback
- All potentiometer adjustments override the stored values
- When you release ENTER, the controller:
  - Returns to the stored MIDI values
  - Activates catch-up mode
  - The display returns to normal

### Catch-Up Mode

After exiting temporary override mode, catch-up mode is activated:
- Potentiometers must pass through their stored position before taking control
- This prevents abrupt jumps in your MIDI parameters
- Once all pots have caught up, normal operation resumes

### Assign Screen (MIDI ASSIGN)

Press ASSIGN from the main screen to enter the configuration menu. Use PREV/NEXT to navigate through edit modes:

**Navigation flow:**
1. **SELECT POT** – Choose which pot (1-8) to configure
2. **EDIT CHANNEL** – Set MIDI channel (1-16) or OMNI
3. **EDIT CC** – Set Control Change number (0-127)
4. **EDIT MIN** – Minimum output value (0-127)
5. **EDIT MAX** – Maximum output value (0-127)
6. **EDIT DIR** – Normal (0-127) or Inverted (127-0)

**Editing values:**
- **PREV/NEXT** – Adjust values incrementally
- **Potentiometer 8** – Smooth continuous adjustment across full range - faster when selecting MIDI CC's and custom ranges
- **ENTER** – Cycle through edit modes
- **ASSIGN** – Save settings and return to main screen

### Factory Reset

From the main screen, press and hold **PREV + NEXT** for 2 seconds:
- All settings reset to defaults
- Device confirms reset on screen
- Resets to:
  - Channel 1-8 (one per pot)
  - CC 7 for all pots
  - Min 0, Max 127
  - Normal direction

---

## **MIDI Implementation**
- **USB MIDI** – Class-compliant in rev2, no drivers needed
- **Per-Pot Configuration** – Each pot has independent:
  - MIDI Channel (1-16 or OMNI)
  - CC Number (0-127)
  - Value Range (min/max 0-127)
  - Direction (Normal/Inverted)
- **EEPROM Storage** – Settings persist after power-off

---

## **Building from Source**
- Requires **Arduino IDE**
- Libraries:
  - `Adafruit_SSD1306` – OLED display driver
  - `Adafruit_GFX` – Graphics library for UI
  - `MIDI` – MIDI protocol handling
  - `EEPROM` – Settings storage (built-in)

---

## **Hardware Pin Connections**

| Component        | Arduino NANO Pins                    |
|------------------|--------------------------------------|
| MUX SIG          | A0                                   |
| MUX S0           | D8                                   |
| MUX S1           | D9                                   |
| MUX S2           | D10                                  |
| MUX S3           | D16                                  |
| ASSIGN Button    | D4 (INPUT_PULLUP)                    |
| ENTER Button     | D5 (INPUT_PULLUP)                    |
| PREV Button      | D6 (INPUT_PULLUP)                    |
| NEXT Button      | D7 (INPUT_PULLUP)                    |
| OLED SDA         | D2 (SDA)                             |
| OLED SCL         | D3 (SCL)                             |
| Pot 1-8          | MUX Channels 0-7                     |

---

## **Differences from MC8P**

| Feature           | MC8P (Teensy 4.0)    | MC8P mini (Arduino)      |
|-------------------|----------------------|---------------------------|
| Microcontroller   | Teensy 4.0           | Arduino NANO / Pro Micro  |
| MIDI Output       | USB + TRS 3.5mm      | Rev1 TRS Only / Rev2 USB + TRS 3.5mm |
| Max CCs per Pot   | Up to 10             | 1                         |
| EEPROM            | Yes                  | Yes                       |
| Catch-up Mode     | Yes                  | Yes                       |
| Temp Override     | Yes                  | Yes                       |

---

## **License**
**MIT License** – Open-source for personal & commercial use

---

## **Support & Contributions**
- **Issues:** Open a GitHub ticket
- **Custom Requests:** Start a thread in the discussions page or email: axs.instruments@gmail.com
- **Want to improve it?** PRs welcome!

---

## **Version History**
- **v1.0** – Initial release
  - 8-pot control via 74HC4067 MUX
  - Full MIDI CC assignment per pot
  - OLED UI with assign menu
  - EEPROM settings storage
  - Temporary override and catch-up modes
  - Factory reset function
