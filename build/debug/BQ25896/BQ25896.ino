/*                                                     https://oshwlab.com/ratti3
  _|_|_|                _|      _|      _|  _|_|_|     https://youtube.com/@Ratti3
  _|    _|    _|_|_|  _|_|_|_|_|_|_|_|            _|   https://projecthub.arduino.cc/Ratti3
  _|_|_|    _|    _|    _|      _|      _|    _|_|     https://ratti3.blogspot.com
  _|    _|  _|    _|    _|      _|      _|        _|   https://hackaday.io/Ratti3
  _|    _|    _|_|_|      _|_|    _|_|  _|  _|_|_|     https://www.hackster.io/Ratti3
.                                                      https://github.com/Ratti3

This file is part of Foobar.

Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as 
published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
Foobar is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with Foobar. If not, see <https://www.gnu.org/licenses/>.
*/

#include <Wire.h>
#include <BQ2589x.h>
#include <FastLED.h>           // v3.5.0  | https://github.com/FastLED/FastLED
#include <TimerOne.h>          // v1.1.1  | https://github.com/PaulStoffregen/TimerOne
#include <LowPower.h>          // v2.2    | https://github.com/LowPowerLab/LowPower
#include <AceButton.h>         // v1.9.2  | https://github.com/bxparks/AceButton
#include <BasicEncoder.h>      // v1.1.4  | https://github.com/micromouseonline/BasicEncoder
#include <Adafruit_GFX.h>      // v1.11.5 | https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_SSD1306.h>  // v2.5.7  | https://github.com/adafruit/Adafruit_SSD1306

/*  ____________
  -|            |-  A0 : TH_IC   [AI]         NTC for monitoring temperature close to the IC
  -|            |-  A2 : OTG     [DI]         Boost mode enable pin. The boost mode is activated when OTG_CONFIG = 1, OTG pin is high, and no input source is detected at VBUS.
  -|            |-  A3 : CE      [DI]         Active low Charge Enable pin. Battery charging is enabled when CHG_CONFIG = 1 and CE pin = Low. CE pin must be pulled High or Low.
  -|            |-  A4 : ENCA    [DI]         Rotary Encoder
  -|            |-  A5 : ENCB    [DI]         Rotary Encoder
  -| ATMEGA32U4 |-  ~5 : BUZ     [AO][PWM]    Buzzer
  -|            |-   7 : SW1     [DI][INT.6]  Rotary Encoder Switch, used to wake up from sleep mode.
  -|            |-  ~9 : INT     [DO][PCINT5] Open-drain Interrupt Output. The INT pin sends active low, 256-μs pulse to host to report charger device status and fault.
  -|            |- ~10 : PSEL    [DI]         Power source selection input. High indicates a USB host source and Low indicates an adapter source.
  -|            |- ~11 : WS2812  [DO]         WS2812B LEDs
  -|____________|- ~13 : D13_LED [DO]         Arduino LED_BUILTIN
  [DI] = Digital In, [DO] = Digital Out, [AI] = Analog In, [AO] = Analog Out
*/

byte verHigh = 1;
byte verLow = 0;

// BQ25896
#define PIN_OTG A2         // BQ25896 OTG Digital Input
#define PIN_CE A3          // BQ25896 CE Digital Input
#define PIN_INT 9          // BQ25896 INT Digital Input
#define PIN_PSEL 10        // BQ25896 PSEL Digital Input
#define BQ2589x_ADDR 0x6B  // BQ25896 I2C Address
bq2589x CHARGER;
int arrayVCHG[5] = { 3840, 4000, 4096, 4192, 4208 };            // Charge volage values
byte menuPositionVCHG = 3;                                      // Set default charge voltage to 4.92V
int arrayICHG[6] = { 512, 1024, 1536, 2048, 2560, 3072 };       // Charge current values
byte menuPositionICHG = 5;                                      // Set default charge current to 3A
int arrayVOTG[3] = { 4998, 5062, 5126 };                        // Boost voltage values
byte menuPositionVOTG = 1;                                      // Set default boost voltage to 5.062V
int arrayIOTG[5] = { 500, 750, 1200, 1650, 2150 };  // Boost current values
byte menuPositionIOTG = 4;                                      // Set default boost current to 2.15A

// AceButton
#define PIN_ENCODER_SW 7  // SW1 PIN
using namespace ace_button;
AceButton SW1(PIN_ENCODER_SW);
void handleEvent(AceButton*, uint8_t, uint8_t);

// FastLED
#define PIN_WS2812 11  // WS2812B Data PIN
#define NUM_LEDS 2     // Number of WS2812B LEDs
#define LED_BRIGHTNESS 15
CRGB leds[NUM_LEDS];
bool stateLED0 = 0;

// OLED
#define SCREEN_ADDRESS 0x3C  // OLED Address
Adafruit_SSD1306 OLED(128, 64, &Wire, -1);
unsigned long oled_sleep = 0;
byte oledRotation = 2;

// Rotary Encoder, Timer & Menu
#define PIN_ENCA A4  // Rotary Encoder PIN A
#define PIN_ENCB A5  // Rotary Encoder PIN B
BasicEncoder ENCODER(PIN_ENCA, PIN_ENCB, HIGH, 2);
unsigned long last_change = 0;
unsigned long now = 0;
int encoderPrev = 0;
bool menuMode = 0;
int menuPosition = 0;
bool justWokeUp = 0;
int arraySleep[3] = { 60, 120, 300 };
byte settingsSleep = 1;

// IC NTC Thermistor
#define PIN_THERMISTOR A0  // NTC Thermistor PIN
#define THERMISTORNOMINAL 10000
#define TEMPERATURENOMINAL 25
#define NUMSAMPLES 5
#define BCOEFFICIENT 3425
#define SERIESRESISTOR 10000
int samples[NUMSAMPLES];

// Buzzer
#define PIN_BUZZER 5  // Buzzer PIN

void setup() {

  Wire.begin();
  Serial.begin(9600);

  // Wait for Serial
  delay(2000);

  // D13 LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // INT
  pinMode(PIN_INT, INPUT);

  // OTG [LOW = Off, HIGH = Boost]
  pinMode(PIN_OTG, OUTPUT);
  digitalWrite(PIN_OTG, HIGH);

  // CE [LOW = Charge, HIGH = Idle]
  pinMode(PIN_CE, OUTPUT);
  digitalWrite(PIN_CE, LOW);

  // PSEL [LOW = Adapter, HIGH = USB]
  pinMode(PIN_PSEL, OUTPUT);
  digitalWrite(PIN_PSEL, LOW);

  // AceButton setup
  pinMode(PIN_ENCODER_SW, INPUT);
  ButtonConfig* buttonConfig = SW1.getButtonConfig();
  buttonConfig->setEventHandler(handleEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterClick);

  // FastLED setup
  FastLED.addLeds<WS2812, PIN_WS2812, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);

  // OLED setup
  OLED.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);

  // Timer1 setup
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timer_service);
  ENCODER.set_reverse();

  // BQ25896 setup
  CHARGER.begin(&Wire, BQ2589x_ADDR);
  CHARGER.disable_watchdog_timer();
  CHARGER.adc_start(0);
  CHARGER.disable_charger();
  setChargeVoltage(3);
  setChargeCurrent(6);
  setOTGVoltage(1);
  setOTGCurrent(5);

  setupDisplay();
}

void loop() {
  // Check for SW1 (Encoder Switch) presses
  SW1.check();

  // Check for Encoder turns
  if (ENCODER.get_change()) {
    OLED.ssd1306_command(SSD1306_DISPLAYON);
    oled_sleep = 0;
    tone(PIN_BUZZER, 6000, 100);
    menuOption(ENCODER.get_count());
  }

  now = millis();

  if (now - last_change > 1000 && oled_sleep <= arraySleep[settingsSleep]) {
    last_change = now;
    oled_sleep++;

    if (menuMode == 0) {
      displayStatus();
    } else {
      displaySetupMenu();
    }
    chargeLED();

  } else if (oled_sleep > arraySleep[settingsSleep] && (!CHARGER.is_charge_enabled() || !CHARGER.is_otg_enabled())) {
    OLED.ssd1306_command(SSD1306_DISPLAYOFF);

    // Allow wake up pin to trigger interrupt on low.
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_SW), wakeUp, LOW);

    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

    // Disable external pin interrupt on wake up pin.
    detachInterrupt(digitalPinToInterrupt(PIN_ENCODER_SW));

    OLED.ssd1306_command(SSD1306_DISPLAYON);
    oled_sleep = 0;
  } else if (oled_sleep > arraySleep[settingsSleep]) {
    OLED.ssd1306_command(SSD1306_DISPLAYOFF);
  }
}

void wakeUp() {
  justWokeUp = 1;
}

void timer_service() {
  ENCODER.service();
}

// Default Status Display
void displayStatus() {
  OLED.clearDisplay();
  OLED.setTextSize(1);
  OLED.setTextColor(SSD1306_WHITE);
  // VBUS input status
  OLED.setCursor(0, 0);
  OLED.print("Input: ");
  OLED.println(CHARGER.get_vbus_type_text());
  // Charge status
  OLED.setCursor(0, 10);
  OLED.print("Charge:");
  OLED.println(CHARGER.get_charging_status_text());
  // Line
  OLED.drawLine(1, 20, 126, 20, SSD1306_WHITE);
  // VBUS Voltage
  OLED.setCursor(0, 23);
  OLED.print("VBUS:");
  OLED.println(float(CHARGER.adc_read_vbus_volt() * 0.001), 3);
  // VSYS Voltage
  OLED.setCursor(68, 23);
  OLED.print("VSYS:");
  OLED.println(float(CHARGER.adc_read_sys_volt() * 0.001), 3);
  // VBAT Voltage
  OLED.setCursor(0, 33);
  OLED.print("VBAT:");
  OLED.println(float(CHARGER.adc_read_battery_volt() * 0.001), 3);
  // Charge Current
  OLED.setCursor(68, 33);
  OLED.print("ICHG:");
  OLED.println(float(CHARGER.adc_read_charge_current() * 0.001), 3);
  // IC Temp
  OLED.setCursor(0, 43);
  OLED.print("IC");
  OLED.print(char(247));
  OLED.print("C:");
  OLED.println(ntcIC(), 1);
  // IDPM
  OLED.setCursor(68, 43);
  OLED.print("IDPM:");
  OLED.println(float(CHARGER.read_idpm_limit() * 0.001), 2);
  // Options
  OLED.drawLine(1, 53, 126, 53, SSD1306_WHITE);
  OLED.setCursor(7, 56);
  if (CHARGER.is_charge_enabled()) {
    OLED.print("STOP");
  } else {
    OLED.print("CHARGE");
  }
  OLED.setCursor(61, 56);
  if (CHARGER.is_otg_enabled()) {
    OLED.print("STOP");
    leds[1] = CRGB::Blue;
  } else {
    OLED.print("OTG");
    leds[1] = CRGB::Black;
  }
  FastLED.show();
  OLED.setCursor(97, 56);
  OLED.print("SETUP");
  switch (menuPosition) {
    case 0:
      OLED.setCursor(0, 56);
      break;
    case 1:
      OLED.setCursor(54, 56);
      break;
    case 2:
      OLED.setCursor(90, 56);
      break;
  }
  OLED.print(">");
  OLED.display();
}

// Runs when the encoder is turned
void menuOption(int encoderCurr) {
  int menuMax = 0;
  if (menuMode == 0) {
    menuMax = 2;
  } else {
    menuMax = 6;
  }

  if (encoderCurr < encoderPrev) {
    --menuPosition;
    if (menuPosition < 0) menuPosition = menuMax;
  } else if (encoderCurr > encoderPrev) {
    ++menuPosition;
    if (menuPosition > menuMax) menuPosition = 0;
  }
  encoderPrev = encoderCurr;
}

// The event handler for the button.
void handleEvent(AceButton* /* button */, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventReleased:
      tone(PIN_BUZZER, 4000, 100);
      if (justWokeUp) {
        justWokeUp = 0;
        break;
      }
      if (menuMode) {
        switch (menuPosition) {
          case 0:
            // Set Charge Voltage
            ++menuPositionVCHG;
            if (menuPositionVCHG > 4) menuPositionVCHG = 0;
            setChargeVoltage(menuPositionVCHG);
            break;
          case 1:
            // Set Charge Current
            ++menuPositionICHG;
            if (menuPositionICHG > 5) menuPositionICHG = 0;
            setChargeCurrent(menuPositionICHG);
            break;
          case 2:
            // Set Boost Voltage
            ++menuPositionVOTG;
            if (menuPositionVOTG > 2) menuPositionVOTG = 0;
            setOTGVoltage(menuPositionVOTG);
            break;
          case 3:
            // Set Boost Current
            ++menuPositionIOTG;
            if (menuPositionIOTG > 4) menuPositionIOTG = 0;
            setOTGCurrent(menuPositionIOTG);
            break;
          case 4:
            if (oledRotation == 2) {
              oledRotation = 0;
            } else {
              oledRotation = 2;
            }
            OLED.setRotation(oledRotation);
            break;
          case 5:
            ++settingsSleep;
            if (settingsSleep > 2) settingsSleep = 0;
            arraySleep[settingsSleep];
            break;
          case 6:
            // Exit
            menuMode = 0;
            menuPosition = 0;
            displayStatus();
            break;
        }
      } else {
        if (menuPosition == 0) {
          // Start
          if (CHARGER.is_charge_enabled()) {
            CHARGER.disable_charger();
          } else {
            CHARGER.enable_charger();
          }
        } else if (menuPosition == 1) {
          // OTG
          if (CHARGER.is_otg_enabled()) {
            CHARGER.disable_otg();
            leds[1] = CRGB::Black;
          } else {
            CHARGER.enable_otg();
            leds[1] = CRGB::Blue;
          }
          //FastLED.show();
        } else if (menuPosition == 2) {
          // Menu
          menuMode = 1;
          menuPosition = 0;
          displaySetupMenu();
        }
      }
      break;
  }
}

void displaySetupMenu() {
  OLED.clearDisplay();
  OLED.setCursor(34, 0);
  OLED.println("SETUP MENU");
  OLED.drawLine(1, 9, 126, 9, SSD1306_WHITE);
  OLED.setCursor(0, 12);
  OLED.print("CHG:  ");
  OLED.print(float(CHARGER.get_charge_voltage() * 0.001), 3);
  OLED.print("V  ");
  OLED.print(float(CHARGER.get_charge_current() * 0.001), 3);
  OLED.println("A");
  OLED.setCursor(0, 22);
  OLED.print("OTG:  ");
  OLED.print(float(CHARGER.get_otg_voltage() * 0.001), 3);
  OLED.print("V  ");
  OLED.print(float(CHARGER.get_otg_current() * 0.001), 3);
  OLED.println("A");
  OLED.setCursor(7, 32);
  OLED.print("Rotate Display: ");
  if (oledRotation == 0) {
    OLED.print("0");
  } else {
    OLED.print("180");
  }
  OLED.println(char(247));
  OLED.setCursor(7, 42);
  OLED.print("Sleep Timer: ");
  OLED.print(arraySleep[settingsSleep]);
  OLED.println("s");
  // Options
  OLED.setCursor(53, 57);
  OLED.print("EXIT");
  switch (menuPosition) {
    case 0:
      OLED.setCursor(29, 12);
      break;
    case 1:
      OLED.setCursor(77, 12);
      break;
    case 2:
      OLED.setCursor(29, 22);
      break;
    case 3:
      OLED.setCursor(77, 22);
      break;
    case 4:
      OLED.setCursor(0, 32);
      break;
    case 5:
      OLED.setCursor(0, 42);
      break;
    case 6:
      OLED.setCursor(46, 57);
      break;
  }
  OLED.print(">");
  OLED.display();
}

void setChargeVoltage(int volt) {
  CHARGER.set_charge_voltage(arrayVCHG[volt]);
}

void setChargeCurrent(int curr) {
  CHARGER.set_charge_current(arrayICHG[curr]);
}

void setOTGVoltage(int volt) {
  CHARGER.set_otg_voltage(arrayVOTG[volt]);
}

void setOTGCurrent(int curr) {
  CHARGER.set_otg_current(arrayIOTG[curr]);
}

float ntcIC() {
  uint8_t i;
  float average;

  // take N samples in a row, with a slight delay
  for (i = 0; i < NUMSAMPLES; i++) {
    samples[i] = analogRead(PIN_THERMISTOR);
    delay(10);
  }

  // average all the samples out
  average = 0;
  for (i = 0; i < NUMSAMPLES; i++) {
    average += samples[i];
  }
  average /= NUMSAMPLES;

  // convert the value to resistance
  average = 1023 / average - 1;
  average = SERIESRESISTOR / average;

  float steinhart;
  steinhart = average / THERMISTORNOMINAL;           // (R/Ro)
  steinhart = log(steinhart);                        // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                         // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15);  // + (1/To)
  steinhart = 1.0 / steinhart;                       // Invert
  steinhart -= 273.15;                               // convert absolute temp to C

  return steinhart;
}

void chargeLED() {
  stateLED0 = !stateLED0;
  FastLED.show();
  switch (CHARGER.get_charging_status()) {
    case 0:
      leds[0] = CRGB::Black;
      break;
    case 1:
      if (stateLED0) {
        leds[0] = CRGB::Black;
      } else {
        leds[0] = CRGB::DarkOrange;
      }
      break;
    case 2:
      if (stateLED0) {
        leds[0] = CRGB::Black;
      } else {
        leds[0] = CRGB::DarkRed;
      }
      break;
    case 3:
      if (stateLED0) {
        leds[0] = CRGB::Black;
      } else {
        leds[0] = CRGB::LimeGreen;
      }
      break;
  }
}

void setupDisplay() {
  leds[0] = CRGB::Red;
  leds[1] = CRGB::Red;
  FastLED.show();

  OLED.setRotation(oledRotation);
  OLED.clearDisplay();
  OLED.setTextSize(1);
  OLED.setTextColor(SSD1306_WHITE);
  OLED.setCursor(43, 0);
  OLED.println("BQ25896");
  OLED.setCursor(20, 10);
  OLED.println("Battery Charger");
  OLED.setCursor(16, 33);
  OLED.println("Ratti3 Tech Corp");
  OLED.setCursor(50, 56);
  OLED.print("v");
  OLED.print(verHigh);
  OLED.print(".");
  OLED.println(verLow);
  OLED.display();
  delay(3000);
  OLED.clearDisplay();

  leds[0] = CRGB::Black;
  leds[1] = CRGB::Black;
  FastLED.show();

  tone(PIN_BUZZER, 3000, 200);
  delay(500);
  tone(PIN_BUZZER, 4000, 200);
  delay(500);
}