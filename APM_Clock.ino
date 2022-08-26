// Program to implement a Clock display using Analog Panel Meters.
// by Marco Colli
//
// Auust 2022 - version 1.0
// - Initial release
//
// Hardware Requirements
// ---------------------
// - DS3231 clock modules with a I2C interface.
// - 5 Analog panel meters
// - 2 momentary-on tact switches
//
// Description:
// ------------
// Time is kept by the DS3231 Real Time Clock (RTC) module.
//
// The display is implemented using 5 analog panel meters, usually used to display
// Voltage or Current (eg, for a power supply). The 5 meters display Hours, Minutes,
// Day of Week, Date and Month with the faces relabelled to show:
// - Hour: 0 thru 12
// - Minute: 0 thru 60
// - Day of week: Monday thru Sunday
// - Date: 1 thru 31
// - Month: Jan thru Dec
// 
// Tact (MODE and SET) switches are used to set the time on the display.
//
// Functions:
// ----------
// To set up the time:
//  + Click the MODE switch
//  + Click the SET switch to advance the pointer for the enabled element
//  + Click the MODE switch to change element and repeat step above 
//     - elements progress MM, DD, DW, H (am/pm indicator in minutes display), M
//  + Clicking the MODE switch after the last element (M) sets the new time.
// 
// Setup mode has a SET_TIMEOUT for no inactivity. On timeout it abandons any changes.
// 
// Long press the MODE switch toggles summer time mode for easy set/reset.
// Press the SET switch to show full scale on all meters, release to show time.
//
// Library dependencies:
// ---------------------
// MD_DS3231 RTC libraries found at https://github.com/MajicDesigns/DS3231 or the Arduino
// IDE Library Manager. Any other RTC may be substituted with few changes as the current 
// time is passed to all display functions.
//
// MD_UISwitch library is found at https://github.com/MajicDesigns/MD_UISwitch or the 
// Arduino IDE Library Manager.
//

#ifndef DEBUG
#define DEBUG 0   // turn debugging output on (1) or off (0)
#endif

#include <Wire.h>         // I2C library for RTC comms
#include <EEPROM.h>       // Summer time status stored in EEPROM
#include <MD_UISwitch.h>  // switch manager
#include <MD_DS3231.h>    // RTC module manager

// --------------------------------------
// Hardware definitions

const uint8_t PIN_MODE = 7; // setup mode switch
const uint8_t PIN_SET = 8;  // setup settng switch

// all the group below must be PWM pins
const uint8_t PIN_TIME_M = 3;
const uint8_t PIN_TIME_H = 5;
const uint8_t PIN_TIME_DW = 6;
const uint8_t PIN_TIME_DD = 9;
const uint8_t PIN_TIME_MM = 10;

const uint8_t EE_SUMMER_FLAG = 0; // EEPROM address for this value

// --------------------------------------
// Miscellaneous defines

const uint8_t  SEC_PER_MIN = 60;   // seconds in a minute
const uint8_t  MIN_PER_HR = 60;    // minutes in an hour
const uint8_t  HR_PER_DAY = 24;    // hours per day
const uint8_t  DAY_PER_WK = 7;     // days per week
const uint8_t  MTH_PER_YEAR = 12;  // months per year

#if DEBUG
const uint32_t CLOCK_UPDATE_TIME = 10000; // in milliseconds - time resolution to nearest minute does not need rapid updates!
#else 
const uint32_t CLOCK_UPDATE_TIME = 1000;  // in milliseconds - time resolution to nearest minute does not need rapid updates!
#endif
const uint32_t SETUP_TIMEOUT = 60000;     // in milliseconds - timeout for setup inactivity
const uint32_t SHOW_DELAY_TIME = 5000;    // in milliseconds - timeout for showing time mode
const uint32_t INIT_CHECK_TIME = 1000;    // in milliseconds - initilization check per display

// --------------------------------------
// PWM setting for each tick mark on each meter
// Defined in a table to allow for non-linear meter movements

const PROGMEM uint8_t PWM_M[MIN_PER_HR] = 
{ 
   0,   4,   9,  13,  17,  21,  26,  30,  34,  38,  43,  47,  //  0 - 11
  51,  55,  60,  64,  68,  72,  77,  81,  85,  89,  94,  98,  // 12 - 23
 102, 106, 111, 115, 119, 123, 128, 132, 136, 140, 145, 149,  // 24 - 35
 153, 157, 162, 166, 170, 174, 179, 183, 187, 191, 196, 200,  // 36 - 47
 204, 208, 213, 217, 221, 225, 230, 234, 238, 242, 247, 251   // 48 - 59
};
const PROGMEM uint8_t PWM_H[HR_PER_DAY] = 
{
 0, 21, 43, 64, 85, 106, 128, 149, 170, 191, 213, 234, 255,  //  0 - 12
 21, 43, 64, 85, 106, 128, 149, 170, 191, 213, 234           // 13 - 23
};
const PROGMEM uint8_t PWM_DW[DAY_PER_WK] =
{
 0, 42, 85, 128, 170, 213, 255   // Mo, Tu, ... Sa, Su
};
const PROGMEM uint8_t PWM_DD[31] = 
{
   0,   9,  17,  26,  34,  43,  51,  //  1 - 7
  60,  68,  77,  85,  94, 102, 111,  //  8 - 14
 119, 128, 136, 145, 153, 162, 170,  // 15 - 21
 179, 187, 196, 204, 213, 221, 230,  // 22 - 28
 238, 247, 255                       // 29 - 31
};
const PROGMEM uint8_t PWM_MM[MTH_PER_YEAR] =
{
 0, 23, 46, 70, 93, 116, 139, 162, 185, 208, 232, 255  // Jan - Dec
};

// --------------------------------------
//  END OF USER CONFIGURABLE INFORMATION
// --------------------------------------

// --------------------------------------
// Global variables
enum pinId { PIN_MM = 0, PIN_DD = 1, PIN_DW = 2, PIN_H = 3, PIN_M = 4, PIN_MAX = 5 };
uint8_t pins[PIN_MAX] = { PIN_TIME_MM, PIN_TIME_DD, PIN_TIME_DW, PIN_TIME_H, PIN_TIME_M };

MD_UISwitch_Digital  swMode(PIN_MODE);            // mode/setup switch handler
MD_UISwitch_Digital  swSet(PIN_SET);              // setting switch handler

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

#if  DEBUG
#define PRINTS(x)    do { Serial.print(F(x)); } while (false)
#define PRINT(s, x)  do { Serial.print(F(s)); Serial.print(x); } while (false)
#define PRINTX(s, x) do { Serial.print(F(s)); Serial.print(F("0x"); Serial.print(x, HEX); } while (false)
#else
#define PRINTS(x)
#define PRINT(s, x)
#define PRINTX(x)
#endif

// --------------------------------------
// Code
bool isSummerMode()
// Return true if summer mode is active
{
  return(EEPROM.read(EE_SUMMER_FLAG) != 0);
}

void flipSummerMode(void)
// Reverse the the summer flag mode in the EEPROM
{
  // handle EEPROM changes
  EEPROM.write(EE_SUMMER_FLAG, isSummerMode() ? 0 : 1);
  PRINT("\nNew Summer Mode ", isSummerMode());

  // now show the current offset on the display
  //mapOffset(map, (isSummerMode() ? 1 : 0));
}

uint8_t daysInMonth(uint8_t month)
{
  static const PROGMEM uint8_t DAY_PER_MTH[MTH_PER_YEAR] =  // max days per month
  { 
    31, 29, 31, 30, 31, 30,   // Jan - Jun
    31, 31, 30, 31, 30, 31    // Jul - Dec
  };
  uint8_t days = 0;

  if (month <= 12)
    days = pgm_read_byte(DAY_PER_MTH + month);

    return(days);
}

uint8_t currentHour(uint8_t h)
// Change the RTC hour to include any summer time offset
// Clock always holds the 'real' time.
{ 
  if (h > 12) h -= 12;
  h += (isSummerMode() ? 1 : 0);

  return(h);
}

void dumpTime()
// Show displayed time to the debug display
{
  uint8_t h = currentHour(RTC.h);

  PRINTS("\nT: ");
  if (h < 10) PRINT("0", h); else PRINT("", h);
  if (RTC.m < 10) PRINT(":0", RTC.m); else PRINT(":", RTC.m);
  if (RTC.s < 10) PRINT(":0", RTC.s); else PRINT(":", RTC.s);
  if (RTC.h < 12) PRINTS(" am "); else PRINTS(" pm ");
  switch (RTC.dow)
  {
    case 1: PRINTS("Mon"); break;
    case 2: PRINTS("Tue"); break;
    case 3: PRINTS("Wed"); break;
    case 4: PRINTS("Thu"); break;
    case 5: PRINTS("Fri"); break;
    case 6: PRINTS("Sat"); break;
    case 7: PRINTS("Sun"); break;
  }
  if (RTC.dd < 10) PRINT(" 0", RTC.dd); else PRINT(" ", RTC.dd);
  if (RTC.mm < 10) PRINT("/0", RTC.mm); else PRINT("/", RTC.mm);
}

void showTime(const uint8_t idx, const pinId pin, const uint8_t sizeData, const uint8_t* data)
// Show the time on the display meters
{
  uint8_t aValue = 255;

  //PRINT(" (V,P,A):", idx); PRINT(",", pins[pin]); PRINT(",", pgm_read_byte(data + idx));
  if (idx < sizeData)
    aValue = pgm_read_byte(data + idx);
  analogWrite(pins[pin], aValue);
}

void setDisplays(uint8_t pwmValue)
// set all displays to the same value
{
  for (uint8_t i = 0; i < PIN_MAX; i++)
    analogWrite(pins[i], pwmValue);
}

void updateClock(uint8_t h, uint8_t m, uint8_t dw, uint8_t dd, uint8_t mm)
{
  dumpTime();  // debug output only

  //PRINTS("\nH "); 
  showTime(currentHour(h), PIN_H, sizeof(PWM_H), PWM_H);
  //PRINTS("\nM "); 
  showTime(m, PIN_M, sizeof(PWM_M), PWM_M);
  //PRINTS("\nDW "); 
  showTime(dw - 1, PIN_DW, sizeof(PWM_DW), PWM_DW);
  //PRINTS("\nDD "); 
  showTime(dd - 1, PIN_DD, sizeof(PWM_DD), PWM_DD);
  //PRINTS("\nMM "); 
  showTime(mm - 1, PIN_MM, sizeof(PWM_MM), PWM_MM);
}

bool configTime(uint8_t& h, uint8_t& m, uint8_t& dw, uint8_t& dd, uint8_t& mm)
// Handle the user interface to set the current time.
// Remains in this function until completed (finished == true)
// Return true if the time was fully set, false otherwise (eg, timeout)
{
  bool result = false;
  bool finished = false;
  pinId thisElement = PIN_MM;
  uint32_t  timeLastActivity = millis();

  PRINTS("\nStarting setup");
  setDisplays(0);   // blank everything out

  do
  {
    yield();          // allow WDT and other things to run if implemented

    if (swMode.read() == MD_UISwitch::KEY_PRESS)
    {
      timeLastActivity = millis();
      thisElement = (pinId)(thisElement + 1);
      PRINT("\nSetup: ", thisElement);

      if (thisElement == PIN_MAX)
      {
        PRINTS("\nSetup completed");
        finished = result = true;
      }
      else
      {
        // otherwise reset all the meters to zero so the relevant
        // element will update with the current value
        PRINTS(" - Clearing display");
        setDisplays(0);
      }
    }

    // process the current element
    MD_UISwitch::keyResult_t key = swSet.read();

    if (key != MD_UISwitch::KEY_NULL)   // valid key was detected
      timeLastActivity = millis();

    switch (thisElement)
    {
    case PIN_MM:   // month
      showTime(mm - 1, PIN_MM, sizeof(PWM_MM), PWM_MM);
      if (key == MD_UISwitch::KEY_PRESS)
      {
        mm++;
        if (mm > MTH_PER_YEAR) mm = 1;
        PRINT("\nMM:", mm);
      }
      break;

    case PIN_DD:   // date
      showTime(dd - 1, PIN_DD, sizeof(PWM_DD), PWM_DD);
      if (key == MD_UISwitch::KEY_PRESS)
      {
        dd++;
        if (dd > daysInMonth(mm)) dd = 1;
        PRINT("\nDD:", dd);
      }
      break;

    case PIN_DW:   // day of week
      showTime(dw - 1, PIN_DW, sizeof(PWM_DW), PWM_DW);
      if (key == MD_UISwitch::KEY_PRESS)
      {
        dw++;
        if (dw > DAY_PER_WK) dw = 1;
        PRINT("\nDW:", dw);
      }
      break;

    case PIN_H:    // hours
      showTime(currentHour(h), PIN_H, sizeof(PWM_H), PWM_H);
      showTime(RTC.h >= 12 ? 59 : 0, PIN_M, sizeof(PWM_M), PWM_M);   // show AM or PM
      if (key == MD_UISwitch::KEY_PRESS)
      {
        h++;
        if (h > HR_PER_DAY) h = 1;
        PRINT("\nH:", h);
      }
      break;

    case PIN_M:    // minutes
      showTime(m, PIN_M, sizeof(PWM_M), PWM_M);
      if (key == MD_UISwitch::KEY_PRESS)
      {
        m++;
        if (m >= MIN_PER_HR) m = 1;
        PRINT("\nM:", m);
      }
      break;

    default:
      // should never get here, but if we do set to 
      // something sensible that will end the cycle.
      thisElement = PIN_MM;
      break;
    }

    // check if we timed out
    if (millis() - timeLastActivity >= SETUP_TIMEOUT)
    {
      finished = true;
      PRINTS("\nSetup timeout");
    }
  } while (!finished);

  return(result);
}

void setup(void)
{
#if  DEBUG
  Serial.begin(57600);
#endif
  PRINTS("\n[APM_Clock]");

  // set the output pin modes
  for (uint8_t i = 0; i < PIN_MAX; i++)
    pinMode(pins[i], OUTPUT);

  // set up switch management
  swMode.begin();
  swMode.enableRepeat(false);
  swSet.begin();

  swSet.begin();
  swSet.enableRepeat(false);
  swSet.enableLongPress(false);
  swSet.enableDoublePress(false);

  // set up RTC management
  RTC.control(DS3231_12H, DS3231_OFF);        // set 24 hr mode
  RTC.control(DS3231_CLOCK_HALT, DS3231_OFF); // start the clock

  // flash the displays
  setDisplays(255);
  delay(INIT_CHECK_TIME);
  setDisplays(0);

  PRINT("\nSummer Mode ", isSummerMode());
}

void loop(void) 
{
  static uint32_t timeLastUpdate = 0;
  static enum stateRun_t    // loop() FSM states
  { 
    SR_UPDATE,        // update the display
    SR_IDLE,          // waiting for time to update
    SR_SETUP,         // run the time setup
    SR_SUMMER_TIME,   // toggle summer time indicator
    SR_FULL_SCALE     // show full scale test mode
  } state = SR_UPDATE;
  
  switch (state)
  {
  case SR_UPDATE:   // update the display
    timeLastUpdate = millis();
    RTC.readTime();
    updateClock(RTC.h, RTC.m, RTC.dow, RTC.dd, RTC.mm);
    state = SR_IDLE;
    break;

  case SR_IDLE:   // wait for ...
    // ... time to update the display ...
    if (millis() - timeLastUpdate >= CLOCK_UPDATE_TIME)
      state = SR_UPDATE;

    // ... or user input from MODE switch ...
    switch (swMode.read())
    {
    case MD_UISwitch::KEY_PRESS:     state = SR_SETUP; break;
    case MD_UISwitch::KEY_LONGPRESS: state = SR_SUMMER_TIME; break;
    default: break;
    }

    // ... or user input from SET switch
    if (swSet.read() == MD_UISwitch::KEY_DOWN)
      state = SR_FULL_SCALE;
    break;

  case SR_SETUP:   // time setup
    if (configTime(RTC.h, RTC.m, RTC.dow, RTC.dd, RTC.mm))
    {
      // write new time to the RTC
      RTC.s = 0;
      RTC.writeTime();
    }
    state = SR_UPDATE;
    break;

  case SR_SUMMER_TIME:  // handle the summer time selection
    flipSummerMode();
    state = SR_UPDATE;
    break;

  case SR_FULL_SCALE:  // force the display to full scale
    setDisplays(255);

    if (swSet.read() == MD_UISwitch::KEY_UP)
      state = SR_UPDATE;
    break;

  default:
    state = SR_UPDATE;
  }
}
