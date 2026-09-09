#include <ESP8266WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <AceSorting.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>

#define D6 12
#define D7 13

using ace_sorting::shellSortKnuth;

// Initialize LCD and RTC
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Adjust I2C address to match yours
JsonDocument doc;

const char* const months[12] PROGMEM = { "Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sept", "Oct", "Nov", "Dec" };
const char* const weekdayNames[7] PROGMEM = { "Sun", "Mon", "Tues", "Wed", "Thurs", "Fri", "Sat" };

enum Weekday : int { SUNDAY = 0,
                     MONDAY,
                     TUESDAY,
                     WEDNESDAY,
                     THURSDAY,
                     FRIDAY,
                     SATURDAY };

struct DoseTime {
  int weekday;
  int hour;
  int minute;
};

// -----------------------------------------------------------------------------------
// CHANGE THESE TO CUSTOMIZE SETTINGS

// Wi-Fi Settings
const char* ssid = "<WI-FI NETWORK NAME HERE>";
const char* password = "<PASSWORD HERE>";

// Timezones can be found here:
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char* timezone = "<+06>+6";           // GMT +6
const char* ntpServer = "pool.ntp.org";     // Server to sync time with
constexpr int timeFormat = 0;               // 0 for 12-hour; 1 for 24-hour
constexpr float doseAmount = 1.0;           // Duration of the dose in Milliliters (ml).
constexpr unsigned long DOSE_WINDOW = 300;  // How late a dose can fire. (in Seconds) Ex. 300 = 5 min
constexpr float PUMP_CALIBRATION = 1.04;    // Pump calibration factor (for 1 mL/sec)

DoseTime doseSchedule[] = {
  // Format: {Day, Hour (24hr Format), Minute}
  // Ex. {SATURDAY, 13, 0} = Saturday @ 1:00 pm or 13:00.
  // Min Hour = 0; Max Hour = 23 (Hour 24 == 0)
  // Min Minute = 0; Max Minute = 59 (Minute 60 == 0)
  // Out of bounds numbers will be automatically assigned to 0.
  { SUNDAY, 9, 0 },
  { MONDAY, 9, 0 },
  { TUESDAY, 9, 0 },
  { WEDNESDAY, 9, 0 },
  { THURSDAY, 9, 0 },
  { FRIDAY, 9, 0 },
  { SATURDAY, 12, 0 },
};
// -----------------------------------------------------------------------------------

// Constants
constexpr int PUMP_PIN = D6;    // Pin connected to the pump (WeMos D1 mini D6)
constexpr int BUTTON_PIN = D7;  // Pin connected to the manual dose button (WeMos D1 mini D7)

constexpr unsigned long DEBOUNCE_DELAY = 50;             // Delay for button debounce (in milliseconds)
constexpr unsigned long DISPLAY_UPDATE_INTERVAL = 1000;  // Interval for updating the display (in milliseconds)
constexpr int PUMP_DURATION = (((PUMP_CALIBRATION - 1.0) * -1.0) + 0.9) * (doseAmount * 1000);  // The time (in milliseconds) for the pump to run.
constexpr int MAX_PUMP_DURATION = PUMP_DURATION + 1;                                            // Maximum pump runtime failsafe to prevent pump from injecting too much.

constexpr unsigned long SECONDS_PER_MINUTE = 60;
constexpr unsigned long SECONDS_PER_HOUR = 3600;
constexpr unsigned long SECONDS_PER_DAY = 86400;
constexpr unsigned long SECONDS_PER_WEEK = 604800;
constexpr time_t MIN_VALID_EPOCH = 946684800;  // Jan 1, 2000 UTC => for NTP syncs in setup

// Global variables
unsigned long lastDebounceTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastNTPUpdate = 0;
int lastButtonState = HIGH;
int buttonState = HIGH;
bool pumpActivated = false;
unsigned long pumpStartTime = 0;
unsigned long pumpStopTime = 0;
int numDoses = sizeof(doseSchedule) / sizeof(doseSchedule[0]);  // The number of doses based on the schedule.
int currentDoseIndex = 0;
DoseTime nextDose;

// -----------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);  // For Serial Monitor

  initLCD();
  initFS();
  initWiFi();
  initPumpPins();
  initTime();
  initDoseSchedule();
}

void initLCD() {
  lcd.init();
  lcd.backlight();

  Serial.println(F("Starting setup..."));
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting");
  lcd.setCursor(0, 1);
  lcd.print("Setup...");
}

void initFS() {
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }
}

void initWiFi() {
  Serial.println("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
  }

  Serial.print("\n");
  Serial.println("IP Address:");
  Serial.println(WiFi.localIP());
  WiFi.setOutputPower(15);  // using 15 dBm for lower power consumption.

  long rssi = WiFi.RSSI();
  Serial.print("Signal Strength (RSSI): ");
  Serial.println(rssi);  // Higher negative values means weaker signal (Ex. -30 dBm is strong, -80 dBm is weak)
}

void initPumpPins() {
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void initTime() {
  configTzTime(timezone, ntpServer);

  // Wait until time is retrieved for the first time.
  time_t now = time(nullptr);
  while (now < MIN_VALID_EPOCH) {
    delay(100);
    now = time(nullptr);
  }
}

void initDoseSchedule() {
  sortSchedule(doseSchedule, numDoses);
  printSchedule();
  initNextDose(doseSchedule, numDoses);

  Serial.println("Next Dose:");
  printDose(nextDose);
  storeNextDoseToFile(nextDose);
  storeCurrentDoseIndex(currentDoseIndex);

  loadNextDoseFromFile();
}

//----------------------------------------------------------------------------------------------
// Dose/Schedule Operations

bool isDateValid(DoseTime* doseTime) {
  // Prevent any out of bounds weekDays
  if ((doseTime->weekday < SUNDAY) || (doseTime->weekday > SATURDAY)) {
    return false;
  }

  // Prevent any out of bounds hours
  if ((doseTime->hour < 0) || (doseTime->hour > 23)) {
    return false;
  }

  // Prevent any out of bounds minutes
  if ((doseTime->minute < 0) || (doseTime->minute > 59)) {
    return false;
  }

  return true;
}

tm* getLocalTime() {
  time_t now = time(nullptr);
  return localtime(&now);
}

long timeToSeconds(tm* timeinfo) {
  return timeinfo->tm_wday * SECONDS_PER_DAY
         + timeinfo->tm_hour * SECONDS_PER_HOUR
         + timeinfo->tm_min * SECONDS_PER_MINUTE;
}

long doseTimeToSeconds(DoseTime doseTime) {
  return doseTime.weekday * SECONDS_PER_DAY
         + doseTime.hour * SECONDS_PER_HOUR
         + doseTime.minute * SECONDS_PER_MINUTE;
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
    // Move unique dose to the front of the schedule.
    if (compareDoseTime(schedule[uniqueDose], schedule[i]) || compareDoseTime(schedule[i], schedule[uniqueDose])) {
      uniqueDose++;
      schedule[uniqueDose] = schedule[i];
    }
  }
  // Modify the total number of doses to exclude duplicates.
  // + 1 as the first entry is always unique.
  numDoses = uniqueDose + 1;
}

bool compareDoseTime(const DoseTime& date1, const DoseTime& date2) {
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

// Obtain initial dose with respect to the current day. This is only ran once during setup.
DoseTime initNextDose(DoseTime* schedule, int size) {
  DoseTime dose;

  struct tm* timeinfo = getLocalTime();

  dose.weekday = timeinfo->tm_wday;
  dose.hour = timeinfo->tm_hour;
  dose.minute = timeinfo->tm_min;

  // Find the first dose in the future.
  for (int i = 0; i < size; i++) {
    if (compareDoseTime(dose, schedule[i])) {
      nextDose = schedule[i];
      return nextDose;
    }
    currentDoseIndex++;
  }
  // Wrap around to the start of the week if no dose if found.
  nextDose = schedule[0];
  return nextDose;
}

// Obtain and update next dose
void updateNextDose() {
  DoseTime dose;

  // Wrap around to the first scheduled dose if at the end of schedule list.
  if (currentDoseIndex + 1 >= numDoses) {
    currentDoseIndex = 0;
    nextDose = doseSchedule[currentDoseIndex];
  } else {
    nextDose = doseSchedule[currentDoseIndex + 1];
    currentDoseIndex++;
  }

  // Print the next dose details
  Serial.print(F("Next Dose: "));
  printDose(nextDose);
}

void handleScheduledDose() {
  if (pumpActivated) {
    return;
  }

  long timeDiff = ((timeToSeconds(getLocalTime()) - doseTimeToSeconds(nextDose)) + SECONDS_PER_WEEK) % SECONDS_PER_WEEK;

  if (timeDiff <= DOSE_WINDOW) {
    runPump("Scheduled Dose!");
    updateNextDose();
  }
}

void printDose(DoseTime dose) {
  Serial.println(F("-------------------------------------------------"));
  Serial.print(F("Day: "));
  Serial.println((const char*)pgm_read_ptr(&(weekdayNames[dose.weekday])));
  Serial.print(F("Hour: "));
  Serial.println(dose.hour);
  Serial.print(F("Minute: "));
  Serial.println(dose.minute);
  Serial.print(F("-------------------------------------------------\n"));
}

//----------------------------------------------------------------------------------------------
// LittleFS/Storage Operations

void storeNextDoseToFile(DoseTime dose) {
  File file = LittleFS.open("/nextDose.json", "w");

  // Set values in JSON Doc with the next dose.
  doc["weekday"] = dose.weekday;
  doc["hour"] = dose.hour;
  doc["minute"] = dose.minute;

  serializeJson(doc, file);
  file.close();
}

void loadNextDoseFromFile() {
  File file = LittleFS.open("/nextDose.json", "r");
  deserializeJson(doc, file);

  nextDose.weekday = doc["weekday"];
  nextDose.hour = doc["hour"];
  nextDose.minute = doc["minute"];


  Serial.println(F("Loaded Next Dose:"));
  printDose(nextDose);
  file.close();
}

void storeCurrentDoseIndex(int index) {
  File file = LittleFS.open("/currentDoseIndex.json", "w");

  // Set values in JSON Doc with the next dose.
  doc["currentDoseIndex"] = index;

  serializeJson(doc, file);
  file.close();
}

void loadCurrentDoseIndex() {
  // Avoid empty schedules
  if (numDoses == 0) {
    return;
  }

  File file = LittleFS.open("/currentDoseIndex.json", "r");

  // Default to the very first dose as a safe catch
  if (!file) {
    currentDoseIndex = 0;
    return;
  }

  deserializeJson(doc, file);

  currentDoseIndex = doc["currentDoseIndex"];

  Serial.println(F("Loaded Current Dose Index:"));
  Serial.println(currentDoseIndex);
  Serial.println(F("=================================================\n"));
  file.close();
}


//----------------------------------------------------------------------------------------------
// Time Operations

// Get the current time in 24-hour format
void get24Time(char* timeStr) {
  struct tm* timeinfo = getLocalTime();

  int currentHour = timeinfo->tm_hour;
  int currentMin = timeinfo->tm_min;
  int currentSec = timeinfo->tm_sec;

  snprintf(timeStr, 20, PSTR("%02d:%02d:%02d"), currentHour, currentMin, currentSec);
}

// Gets the current time in 12-hour format
void get12Time(char* timeStr) {
  struct tm* timeinfo = getLocalTime();

  int currentHour = timeinfo->tm_hour;
  int currentMin = timeinfo->tm_min;
  int currentSec = timeinfo->tm_sec;
  int displayHour = (currentHour % 12) != 0 ? (currentHour % 12) : 12;

  const char* ampm = currentHour < 12 ? "AM" : "PM";
  snprintf(timeStr, 20, PSTR("%02d:%02d:%02d %s"), displayHour, currentMin, currentSec, ampm);
}

// Gets the current date (month and date) and returns it as a string.
// This is only used for displaying the current date on the LCD and nothing more.
String getDate() {
  struct tm* timeinfo = getLocalTime();

  const char* dayName = (const char*)pgm_read_ptr(&(weekdayNames[timeinfo->tm_wday]));
  int currentDay = timeinfo->tm_mday;  // tm_mday for getting current day in the month, not in the week.
  const char* monthName;
  int currentMonth = timeinfo->tm_mon + 1;  // +1 is for indexing; Preventing off by one or out of bounds errors

  if (currentMonth >= 1 && currentMonth <= 12) {
    monthName = (const char*)pgm_read_ptr(&(months[currentMonth - 1]));
  } else {
    monthName = "Invalid month name";
  }

  char dateString[30];
  snprintf(dateString, sizeof(dateString), PSTR("%s, %s. %02d"),
           dayName, monthName, currentDay);
  return String(dateString);
}

//----------------------------------------------------------------------------------------------
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
  Serial.println(F("Pump running!"));
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

  unsigned long startDelayTime = millis();
  delay(2000);  // Display the completion message for 2 seconds
  updateDisplay();
}

//----------------------------------------------------------------------------------------------
// Button Operations

void handleButtonPress() {
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = HIGH;
  int reading = digitalRead(BUTTON_PIN);

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

//----------------------------------------------------------------------------------------------
// Display Operations

void updateDisplay() {
  lcd.clear();
  String date = getDate();
  char timeStr[50];

  if (timeFormat == 0) {
    get12Time(timeStr);
  } else if (timeFormat == 1) {
    get24Time(timeStr);
  }

  Serial.println(date);
  Serial.println(timeStr);
  Serial.println("");

  lcd.setCursor(0, 0);
  lcd.print(date);
  lcd.setCursor(0, 1);
  lcd.print(timeStr);
}

//----------------------------------------------------------------------------------------------

void loop() {
  handleButtonPress();
  handleScheduledDose();
  handlePumpOperation();

  // Display update
  if ((millis() - lastDisplayUpdate) >= DISPLAY_UPDATE_INTERVAL && !pumpActivated) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
}
