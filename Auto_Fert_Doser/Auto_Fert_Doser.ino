#include <RTClib.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <AceSorting.h>

RTC_DS1307 rtc;

using ace_sorting::shellSortKnuth;

// EEPROM Initialization Flag
#define E_INIT 0
enum DayOfWeek : uint8_t {SUNDAY = 0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY};

const char* const months[12] PROGMEM = {"Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sept", "Oct", "Nov", "Dec"};
const char* const daysOfWeek[] PROGMEM = {"Sun", "Mon", "Tues", "Wed", "Thurs", "Fri", "Sat"};

// Struct for dose schedule
struct DoseTime {
  DayOfWeek dayOfWeek;
  uint8_t hour;
  uint8_t minute;
};


// -----------------------------------------------------------------------------------
// CHANGE THESE TO CUSTOMIZE PUMP SETTINGS
LiquidCrystal_I2C lcd(39, 16, 2);  // Adjust I2C address to match your LCD screen, if necessary.

constexpr float doseAmount = 1.0; // Duration of the dose in Milliliters (ml).
constexpr uint8_t timeFormat = 0; // 0 for 12-hour; 1 for 24-hour

DoseTime doseSchedule[] = {
  // Format: {Day, Hour (24hr Format), Minute}
  // Ex. {SATURDAY, 13, 0}, = Saturday @ 1:00 pm or 13:00.
  // Hour Max = 23, Minute Max = 59.
  {SUNDAY, 9, 0},
  {MONDAY, 9, 0},
  {TUESDAY, 9, 0},
  {WEDNESDAY, 9, 0},
  {THURSDAY, 9, 0},
  {FRIDAY, 9, 0},
  {SATURDAY, 12, 0},
};

// -----------------------------------------------------------------------------------

// Constants
constexpr uint8_t PUMP_PIN = 9;  // Pin connected to the pump
constexpr uint8_t BUTTON_PIN = 2;  // Pin connected to the manual dose button
constexpr unsigned long DEBOUNCE_DELAY = 50;  // Delay for button debounce (in milliseconds)
constexpr unsigned long DISPLAY_UPDATE_INTERVAL = 1000;  // Interval for updating the display (in milliseconds)
constexpr float PUMP_CALIBRATION = 1.04;  // Pump calibration factor (for 1 mL/sec)
constexpr int PUMP_DURATION = (((PUMP_CALIBRATION - 1.0) * -1.0) + 0.9) * (doseAmount * 1000); // The time (in milliseconds) for the pump to run.
constexpr int MAX_PUMP_DURATION = PUMP_DURATION + 1; // Maximum pump runtime to prevent any hangups
const uint8_t numDoses = sizeof(doseSchedule) / sizeof(doseSchedule[0]);  // Number of doses in the schedule

// Global variables
unsigned long lastDebounceTime = 0;
unsigned long lastDisplayUpdate = 0;
int buttonState = HIGH;
bool pumpActivated = false;
unsigned long pumpStartTime = 0;
unsigned long pumpStopTime = 0;
uint8_t currentDoses = 0;
DoseTime nextDose;
// -----------------------------------------------------------------------------------


void setup() {
  Serial.begin(9600);

  // -----------------------------------------------------------------------------------
  //    Uncomment this function is you want a factory reset.
  //    Comment out this function, make changes to schedule, and reupload.
  //    resetSchedule();
  // -----------------------------------------------------------------------------------

  lcd.init();
  lcd.backlight();

  if (!rtc.begin()) {
    Serial.println(F("RTC not found!"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC not found!");
    while (1);
  }

  if (!rtc.isrunning()) {
    Serial.println(F("RTC not running!"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC not running!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  char initFlag = EEPROM.read(E_INIT);
  if (!checkInitializeEEPROM(initFlag)) {
    sortSchedule(doseSchedule, numDoses);
    Serial.println(F("Dose Schedule Sorted"));
    saveScheduleToEEPROM(doseSchedule);
    Serial.println(F("Dose Schedule Saved"));
    printSchedule();
    EEPROM.write(E_INIT, 'T');  // Mark EEPROM as initialized
  }

  initNextDose();
  updateDisplay();
}

// -----------------------------------------------------------------------------------
// Schedule Operations

void resetSchedule() {
  // Use this if you want to do a factory reset.
  EEPROM.write(E_INIT, 'A');
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Please Reupload Firmware");
  lcd.setCursor(0, 1);
  lcd.print("Remove Reset Funct");

  Serial.println(F("Please Reupload Firmware"));
  Serial.println(F("Please Comment Out Reset Function"));

  // Infinite Loop to get user to hard reset and reupload code.
  while (true) {
  }
}

bool checkInitializeEEPROM(char initFlag) {
  return (initFlag == 'T');
}

bool isDateValid(DoseTime* doseTime){
  // Prevent any out of bounds hours
  if ((doseTime -> hour < 0) || (doseTime -> hour > 23)) {
    return false;
  }

  // Prevent any out of bounds minutes
  if ((doseTime -> minute < 0) || (doseTime -> minute > 59)) {
    return false;
  }

  return true;
}

void sortSchedule(DoseTime* schedule, int size) {

  // Remove invalid entries first
  int validCount = 0;
  for (int i = 0; i < size; i++) {
    DoseTime* doseDate = &schedule[i];

    if (isDateValid(doseDate)) {
      schedule[validCount] = schedule[i];
      validCount++;
    } else {
      Serial.print(F("Invalid dose entry dropped at index "));
      Serial.println(i);
      Serial.println(F("Entry: "));
      printDose(*doseDate);
    }
  }
  numDoses = validCount;

  // This function also removes duplicates.
  // The main method is moving unique entries to the front of the array
  // Any duplicate entries are at the back and can easily be indexed out.
  shellSortKnuth(schedule, size, compareDoseTime);

  // This section also removes any duplicates found.
  // Track the number of unique doses.
  // Starts at 1 as the first entry will always be unique.
  int uniqueDose = 0;

  for (int i = 1; i < numDoses; i++) {
    // If the current element is not a duplicate of the previous element
    if (compareDoseTime(schedule[uniqueDose], schedule[i]) || compareDoseTime(schedule[i], schedule[uniqueDose])) {
      uniqueDose++;
      schedule[uniqueDose] = schedule[i]; // Move unique dose to the front of the schedule.
    }
  }
  // Modify the total number of doses to exclude duplicates.
  // + 1 as the first entry is always unique.
  numDoses = uniqueDose + 1;
}

bool compareDoseTime(const DoseTime &date1, const DoseTime &date2) {
  // Compare the day of the week first
  if (date1.weekday != date2.weekday) {
    return date1.weekday < date2.weekday;
  }

  // If weekday is the same, compare the hour
  if (date1.hour != date2.hour) {
    return date1.hour < date2.hour;
  }

  // If both weekday and hour are the same, compare the minute
  return date1.minute < date2.minute;
}

void saveScheduleToEEPROM(DoseTime* schedule) {
  uint8_t address = 1;  // Use 1 to account for initialization flag
  EEPROM.put(address, numDoses);  // Store the number of doses
  address += sizeof(numDoses);

  for (uint8_t i = 0; i < numDoses; i++) {
    // Correct hour if needed
    if (schedule[i].hour == 24) {
      schedule[i].hour = 0;
    }

    // Correct minute if needed
    if (schedule[i].minute == 60) {
      schedule[i].minute = 0;
    }

    // Prevent any invalid entries from being saved (no need to check for 24/60 anymore)
    if (schedule[i].hour < 0 || schedule[i].hour > 23) {
      Serial.print(F("Invalid hour for index: "));
      Serial.print(i);
      Serial.println(F(" - Skipping schedule entry..."));
    } else if (schedule[i].minute < 0 || schedule[i].minute > 59) {
      Serial.print(F("Invalid minute for index: "));
      Serial.print(i);
      Serial.println(F(" - Skipping schedule entry..."));
    } else {
      // Save valid schedule entry to EEPROM
      EEPROM.put(address, schedule[i]);
      address += sizeof(DoseTime);
    }
  }
}

void printSchedule() {
  Serial.print("\n");
  Serial.println(F("================================================="));
  Serial.println(F("                    SCHEDULE                     "));
  Serial.println(F("================================================="));
  Serial.print(F("Total Doses: "));
  Serial.println(numDoses);
  Serial.println(F("-------------------------------------------------"));

  for (uint8_t i = 0; i < numDoses; i++) {
    int hour = doseSchedule[i].hour;
    int minute = doseSchedule[i].minute;

    Serial.print(F("Dose "));
    Serial.print(i + 1);
    Serial.print(F(" of "));
    Serial.print(numDoses);
    Serial.print(F(" | "));
    Serial.print((const char*)pgm_read_ptr(&(weekdayNames[doseSchedule[i].weekday])));
    Serial.print(F(" @ "));

    if (timeFormat == 0) {
      int displayHour = (hour % 12) != 0 ? (hour % 12) : 12;
      const char* ampm = hour < 12 ? "AM" : "PM";
      Serial.print(displayHour);
      Serial.print(F(":"));
      if (minute < 10) Serial.print(F("0"));
      Serial.print(minute);
      Serial.print(F(" "));
      Serial.println(ampm);
    } else {
      Serial.print(hour);
      Serial.print(F(":"));
      if (minute < 10) Serial.print(F("0"));
      Serial.println(minute);
    }
  }

  Serial.println(F("=================================================\n"));
}

// -----------------------------------------------------------------------------------
// Dose Operations

DoseTime readDoseFromEEPROM(uint8_t index) {
  uint8_t address = 1;  // Start reading from address 1
  EEPROM.get(address, numDoses);  // Read the number of doses
  address += sizeof(numDoses);

  if (index >= numDoses) {
    // Handle invalid index; wrap around or return a default value
    index = 0;
  }

  address += index * sizeof(DoseTime);
  DoseTime dose;
  EEPROM.get(address, dose);  // Read the specific DoseTime item
  return dose;
}

// Only used during initialization phase
// Goes through the list and updates the next dose with respect to current IRL date.
// This way, we don't
void initNextDose() {
  DateTime now = rtc.now();
  uint8_t day = currentDoses;
  bool doseFound = false;
  DoseTime dose;

  while (!doseFound) {
    if (day >= numDoses) {
      day = 0; // Wrap around if we reach the end of the schedule.
    }

    dose = readDoseFromEEPROM(day);

    // Check if the day matches
    if (dose.dayOfWeek == now.dayOfTheWeek()) {
      // Check if the time is in the future
      if (dose.hour > now.hour() || (dose.hour == now.hour() && dose.minute >= now.minute())) {
        currentDoses = day;
        doseFound = true;
        break;
      }
    } else if (dose.dayOfWeek > now.dayOfTheWeek()) {
      currentDoses = day;
      doseFound = true;
      break;
    }
    day += 1;
    if (day == currentDoses) { // Break if no matches are found after a full loop.
      Serial.println(F("No valid dose found."));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("No valid dose found.");
      break;
    }
  }

  // After finding the correct dose, assign it to nextDose
  if (doseFound) {
    nextDose = dose;
    // Print the next dose details
    Serial.print(F("Next Dose: "));
    Serial.print((const char*)pgm_read_ptr(&(daysOfWeek[nextDose.dayOfWeek])));
    Serial.print(F(" at "));

    char timeStr[20];
    if (timeFormat == 0) { // 12-Hour Format
      const char* ampm = dose.hour < 12 ? "AM" : "PM";
      snprintf_P(timeStr, 20, PSTR("%02d:%02d %s\n"), dose.hour, dose.minute, ampm);
    } else if (timeFormat == 1) {  // 24-Hour Format
      snprintf_P(timeStr, 20, PSTR("%02d:%02d\n"), dose.hour, dose.minute);
    }
    Serial.print(timeStr);
  } else {
    // Fallback in case no future dose is found.
    // Possibly set nextDose to the first in the schedule.
    currentDoses = 0;
    nextDose = readDoseFromEEPROM(0);
  }
}

// Obtain and update next dose
void updateNextDose() {
  uint8_t day;
  DoseTime dose;

  if (currentDoses == numDoses) {
    currentDoses = 0;
    day = currentDoses;
  } else {
    day = currentDoses + 1;
  }

  if (checkInitializeEEPROM(EEPROM.read(E_INIT))) {
    dose = readDoseFromEEPROM(day);
    nextDose = dose;

    // Print the next dose details
    Serial.print(F("Next Dose: "));
    Serial.print((const char*)pgm_read_ptr(&(daysOfWeek[nextDose.dayOfWeek])));
    Serial.print(F(" at "));

    char timeStr[20];
    if (timeFormat == 0) { // 12-Hour Format
      const char* ampm = dose.hour < 12 ? "AM" : "PM";
      snprintf_P(timeStr, 20, PSTR("%02d:%02d %s\n"), dose.hour, dose.minute, ampm);
    } else if (timeFormat == 1) {  // 24-Hour Format
      snprintf_P(timeStr, 20, PSTR("%02d:%02d\n"), dose.hour, dose.minute);
    }
    Serial.print(timeStr);
    Serial.print("\n");
  } else {
    Serial.println(F("No valid dose found."));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("No valid dose found.");
  }
}

void handleScheduledDose() {
  DateTime now = rtc.now();
  if (!pumpActivated && now.dayOfTheWeek() == nextDose.dayOfWeek && now.hour() == nextDose.hour && now.minute() == nextDose.minute && now.second() == 0) {
    runPump("Scheduled Dose!");

    if (currentDoses == numDoses) {
      currentDoses = 0;
    } else {
      currentDoses += 1;
    }
    updateNextDose();  // Update the next scheduled dose after running the current one
  }
}

// -----------------------------------------------------------------------------------
// Pump Operations

void handlePumpOperation() {
  if (pumpActivated) {
    unsigned long currentMillis = millis();
    if (currentMillis >= pumpStopTime) {
      Serial.println(F("Pump duration reached, stopping pump"));
      stopPump("Dose complete");
    } else {
      // Update LCD while pump is running
      lcd.setCursor(0, 1);
      lcd.print("Pump is running.");
    }
  }
}


void runPump(const char* reason) {
  if (pumpActivated) {
    Serial.println(F("Pump already active, ignoring activation request"));
    return;
  }

  pumpActivated = true;
  pumpStartTime = millis();
  pumpStopTime = pumpStartTime + PUMP_DURATION;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(reason);

  digitalWrite(PUMP_PIN, HIGH);
}

void stopPump(const char* reason) {
  if (!pumpActivated) {
    Serial.println(F("Pump not active, ignoring stop request"));
    return;
  }

  digitalWrite(PUMP_PIN, LOW);

  unsigned long pumpRuntime = millis() - pumpStartTime;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pump stopped");
  lcd.setCursor(0, 1);
  lcd.print("Duration: ");
  float runtimeDuration = pumpRuntime / 1000.0;
  lcd.print(runtimeDuration);
  lcd.print("sec");

  pumpActivated = false;
  delay(2000);  // Display the completion message for 2 seconds
  updateDisplay();
}

// -----------------------------------------------------------------------------------
// DateTime + Display Operations

void updateDisplay() {
  lcd.clear();
  String date = getDate();
  char timeStr[20];

  if (timeFormat == 0) {
    get12Time(timeStr);
  } else if (timeFormat == 1) {
    get24Time(timeStr);
  }

  lcd.setCursor(0, 0);
  lcd.print(date);
  lcd.setCursor(0, 1);
  lcd.print(timeStr);
}

String getDate() {
  DateTime now = rtc.now();
  const char* dayName = (const char*)pgm_read_ptr(&(daysOfWeek[now.dayOfTheWeek()]));
  const char* monthName;

  if (now.month() >= 1 && now.month() <= 12) {
    monthName = (const char*)pgm_read_ptr(&(months[now.month() - 1]));
  } else {
    monthName = "Invalid month name";
  }

  char dateString[30];
  snprintf_P(dateString, sizeof(dateString), PSTR("%s, %s %02d"),
             dayName, monthName, now.day());
  return String(dateString);
}

// Get the current time in 24-hour format
void get24Time(char* timeStr) {
  DateTime now = rtc.now();
  snprintf_P(timeStr, 20, PSTR("%02d:%02d:%02d"), now.hour(), now.minute(), now.second());
}

// Get the current time in 12-hour format
void get12Time(char* timeStr) {
  DateTime now = rtc.now();
  uint8_t displayHour = (now.hour() % 12) != 0 ? (now.hour() % 12) : 12;
  const char* ampm = now.hour() < 12 ? "AM" : "PM";
  snprintf_P(timeStr, 20, PSTR("%02d:%02d:%02d %s"), displayHour, now.minute(), now.second(), ampm);
}

// -----------------------------------------------------------------------------------
// Button Operations

void handleButtonPress() {
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = HIGH;
  int reading = digitalRead(BUTTON_PIN);

  // Button debound logic
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW && !pumpActivated) {
        runPump("Manual Release");
      }
    }
  }

  lastButtonState = reading;
}

void loop() {
  handleButtonPress();
  handleScheduledDose();
  handlePumpOperation();

  // Update display at set intervals when pump is not active
  if ((millis() - lastDisplayUpdate) >= DISPLAY_UPDATE_INTERVAL && !pumpActivated) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
}
