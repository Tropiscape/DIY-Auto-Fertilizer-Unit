#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <AceSorting.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>

// Initialize LCD and RTC
LiquidCrystal_I2C lcd(39, 16, 2);  // Adjust I2C address to match yours

using ace_sorting::shellSortKnuth;
JsonDocument doc;

const char* const months[12] PROGMEM = {"Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sept", "Oct", "Nov", "Dec"};
const char* const weekdayNames[7] PROGMEM = {"Sun", "Mon", "Tues", "Wed", "Thurs", "Fri", "Sat"};

enum Weekday : int {SUNDAY = 0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY};

// Struct for dose schedule
struct DoseTime {
  int weekday;
  int hour;
  int minute;
};

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// -----------------------------------------------------------------------------------
// CHANGE THESE TO CUSTOMIZE SETTINGS

// Wi-Fi Settings
const char* ssid     = "<WI-FI NETWORK NAME HERE>";
const char* password = "<PASSWORD HERE>";

constexpr float doseAmount = 1.0; // Duration of the dose in seconds.
constexpr int timeFormat = 0; // 0 for 12-hour; 1 for 24-hour

DoseTime doseSchedule[] = {
  // Format: {Day, Hour (24hr Format), Minute}
  // Ex. {SATURDAY, 13, 0} = Saturday @ 1:00 pm or 13:00.
  // Min Hour = 0; Max Hour = 23 (Hour 24 == 0)
  // Min Minute = 0; Max Minute = 59 (Minute 60 == 0)
  // Out of bounds numbers will be automatically assigned to 0.
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
constexpr int PUMP_PIN = D6;        // Pin connected to the pump
constexpr int BUTTON_PIN = D7;      // Pin connected to the manual dose button
constexpr float PUMP_CALIBRATION = 1.04;  // Pump calibration factor (for 1 mL/sec)
constexpr unsigned long DEBOUNCE_DELAY = 50;  // Delay for button debounce (in milliseconds)
constexpr unsigned long DISPLAY_UPDATE_INTERVAL = 1000;  // Interval for updating the display (in milliseconds)
constexpr int PUMP_DURATION = (1.0 / PUMP_CALIBRATION) * (doseAmount * 1000);
constexpr int MAX_PUMP_DURATION = PUMP_DURATION + 1; // Maximum pump runtime

// Global variables
unsigned long lastDebounceTime = 0;
unsigned long lastDisplayUpdate = 0;
int lastButtonState = HIGH;
int buttonState = HIGH;
bool pumpActivated = false;
unsigned long pumpStartTime = 0;
unsigned long pumpStopTime = 0;
int numDoses = sizeof(doseSchedule) / sizeof(doseSchedule[0]);  // The number of doses based on the schedule.
int currentDoseIndex = 0;
DoseTime nextDose;

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting");
  lcd.setCursor(0, 1);
  lcd.print("Setup...");

  Serial.println(F("Starting setup..."));
  delay(1000);

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  // Connect to Wi-Fi
  Serial.println("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\n");
  Serial.println("IP Address:");
  Serial.println(WiFi.localIP());
  WiFi.setOutputPower(15);

  long rssi = WiFi.RSSI();
  Serial.print("Signal Strength (RSSI): ");
  Serial.println(rssi); // Higher negative values means weaker signal (Ex. -30 dBm is strong, -80 dBm is weak)

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  timeClient.begin(); // Initialize a NTPClient to get time

  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(-21600);

  sortSchedule(doseSchedule, numDoses);
  printSchedule();
  initNextDose(doseSchedule, numDoses);

  Serial.println("Next Dose:");
  printDose(nextDose);
  storeNextDoseToFile(nextDose);
  storeCurrentDoseIndex(currentDoseIndex);

  loadNextDoseFromFile();
  loadCurrentDoseIndex();

}



//----------------------------------------------------------------------------------------------
// Dose/Schedule Operations

void sortSchedule(DoseTime* schedule, int size) {
  // This function also removes duplicates.
  // The main method is moving unique entries to the front of the array
  // Any duplicate entries are at the back and can easily be indexed out.
  shellSortKnuth(schedule, size, compareDoseTime);

  // This section also removes any duplicates found.
  // Track the number of unique doses.
  // Starts at 1 as the first entry will always be unique.
  int uniqueDose = 0;

  for (int i = 1; i < size; i++) {
    // Prevent any out of bounds hours
    if ((schedule[i].hour < 0) || (schedule[i].hour > 23)) {
      schedule[i].hour = 0;
    }

    // Prevent any out of bounds minutes
    if ((schedule[i].minute < 0) || (schedule[i].minute > 59)) {
      schedule[i].minute = 0;
    }

    // If the current element is not a duplicate of the previous element
    if (compareDoseTime(schedule[uniqueDose], schedule[i]) || compareDoseTime(schedule[i], schedule[uniqueDose])) {
      uniqueDose++;
      schedule[uniqueDose] = schedule[i]; // Move unique dose to the front of the schedule.
    }
  }
  // Modify the total number of doses to exclude duplicates.
  numDoses = numDoses - (numDoses - uniqueDose);
}

bool compareDoseTime(const DoseTime &a, const DoseTime &b) {
  // Compare the day of the week first
  if (a.weekday != b.weekday) {
    return a.weekday < b.weekday;
  }

  // If weekday is the same, compare the hour
  if (a.hour != b.hour) {
    return a.hour < b.hour;
  }

  // If both weekday and hour are the same, compare the minute
  return a.minute < b.minute;
}

void printSchedule() {
  Serial.println(F("Schedule:"));
  for (uint8_t i = 0; i < numDoses; i++) {
    Serial.print(F("Dose "));
    Serial.print(i);
    Serial.print(F(": Day "));
    Serial.print((const char*)pgm_read_ptr(&(weekdayNames[doseSchedule[i].weekday])));
    Serial.print(F(", Hour "));
    Serial.print(doseSchedule[i].hour);
    Serial.print(F(", Minute "));
    Serial.println(doseSchedule[i].minute);
    Serial.print("\n");
  }
}

DoseTime initNextDose(DoseTime* schedule, int size) {
  DoseTime dose;

  timeClient.update();

  dose.weekday = timeClient.getDay();
  dose.hour = timeClient.getHours();
  dose.minute = timeClient.getMinutes();

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
  if (currentDoseIndex + 1 > numDoses) {
    currentDoseIndex = 0;
    nextDose = doseSchedule[currentDoseIndex];
    currentDoseIndex++;
  } else {
    nextDose = doseSchedule[currentDoseIndex + 1];
    currentDoseIndex++;
  }

  // Print the next dose details
  Serial.print(F("Next Dose: "));
  printDose(nextDose);
}

void handleScheduledDose() {
  if (!pumpActivated && timeClient.getDay() == nextDose.weekday && timeClient.getHours() == nextDose.hour && timeClient.getMinutes() == nextDose.minute && timeClient.getSeconds() == 0) {
    runPump("Scheduled Dose!");
    updateNextDose();  // Update the next scheduled dose after running the current one
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
  File file = LittleFS.open("/currentDoseIndex.json", "r");

  deserializeJson(doc, file);

  currentDoseIndex = doc["currentDoseIndex"];

  Serial.println(F("Loaded Current Dose Index:"));
  Serial.println(currentDoseIndex);
  Serial.print(F("-------------------------------------------------\n"));
  file.close();
}


//----------------------------------------------------------------------------------------------
// Time Operations

// Get the current time in 24-hour format
void get24Time(char* timeStr) {
  timeClient.update();
  snprintf_P(timeStr, 20, PSTR("%02d:%02d:%02d"), timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());
}

// Get the current time in 12-hour format
void get12Time(char* timeStr) {
  timeClient.update();
  int currentHour = timeClient.getHours();
  int displayHour = (currentHour % 12) != 0 ? (currentHour % 12) : 12;
  const char* ampm = currentHour < 12 ? "AM" : "PM";
  snprintf_P(timeStr, 20, PSTR("%02d:%02d:%02d %s"), displayHour, timeClient.getMinutes(), timeClient.getSeconds(), ampm);
}

String getDate() {
  timeClient.update();

  // Time structure
  time_t epochTime = timeClient.getEpochTime();
  struct tm* ptm = gmtime ((time_t *)&epochTime);
  const char* dayName = (const char*)pgm_read_ptr(&(weekdayNames[timeClient.getDay()]));
  int currentDay = ptm->tm_mon + 1;
  const char* monthName;
  int currentMonth = ptm->tm_mon + 1;

  if (currentMonth >= 1 && currentMonth <= 12) {
    monthName = (const char*)pgm_read_ptr(&(months[currentMonth - 1]));
  } else {
    monthName = "Invalid month name";
  }

  char dateString[30];
  snprintf_P(dateString, sizeof(dateString), PSTR("%s, %s. %02d"),
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
  timeClient.update();
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

  // Update display at set intervals when pump is not active
  if ((millis() - lastDisplayUpdate) >= DISPLAY_UPDATE_INTERVAL && !pumpActivated) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
}
