#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <sntp.h>
#include <time.h>
#include <DHT.h>

// Timezone lookup from your original project
#include "tz_lookup.h"  
#include "mfactoryfont.h"    

// --- HARDCODED SETTINGS ---
const char* ssid = "Hailee";          // <-- 👈 UPDATE THIS
const char* password = "07102010";  // <-- 👈 UPDATE THIS
const char* timeZone = "Asia/Ho_Chi_Minh";    // <-- 👈 UPDATE THIS (e.g., "America/New_York", "Europe/London")

// --- DISPLAY HARDWARE SETTINGS ---
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN     12 // D6
#define DATA_PIN    15 // D8
#define CS_PIN      13 // D7

// --- DHT SENSOR SETTINGS ---
#define DHTPIN 2      // D4 on Wemos D1 Mini
#define DHTTYPE DHT22 // Using a DHT22 sensor

// --- GLOBAL OBJECTS & VARIABLES ---
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
DHT dht(DHTPIN, DHTTYPE);

// Display timing (milliseconds)
const unsigned long CLOCK_DURATION = 25000; // Show clock for 10 seconds
const unsigned long SENSOR_DURATION = 5000; // Show sensor data for 5 seconds

// Display management
int displayMode = 0; // 0 for Clock, 1 for Sensor Data
unsigned long lastSwitchTime = 0;
String currentTemp = "--";
String currentHumidity = "--";

// --- TIME AND NTP SETUP ---
void setupTime() {
  sntp_stop();
  Serial.println(F("[TIME] Starting NTP sync..."));
  // Configure time with default NTP servers
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Set the timezone using the helper function from tz_lookup.h
  setenv("TZ", ianaToPosix(timeZone), 1);
  tzset();
}

// --- SENSOR DATA FETCH ---
void fetchSensorData() {
  float h = dht.readHumidity();
  // Read temperature as Celsius (isFahrenheit = false)
  float t = dht.readTemperature(false); 

  // Check if any reads failed and exit early to keep old values.
  if (isnan(h) || isnan(t)) {
    Serial.println(F("[SENSOR] Failed to read from DHT sensor!"));
    return;
  }

  // Update global variables with new readings
  currentTemp = String(t, 1);
  currentHumidity = String((int)round(h));
  
  Serial.printf("[SENSOR] Temp: %s°C, Humidity: %s%%\n", currentTemp.c_str(), currentHumidity.c_str());
}


// --- MAIN SETUP ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n[SETUP] Starting..."));

  // Initialize Display
  P.begin();
  P.setIntensity(0); // Set a moderate brightness (0-15)
  P.setCharSpacing(1);
  P.setFont(mFactory);
  P.displayClear();
  P.print("BOOT");
  Serial.println(F("[SETUP] Display initialized."));

  // Initialize DHT Sensor
  dht.begin();
  Serial.println(F("[SETUP] DHT22 sensor initialized."));

  // Connect to WiFi
  Serial.printf("[WIFI] Connecting to %s ", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  } // <-- THIS BRACE WAS MISSING
  
  Serial.println(F("\n[WIFI] Connected!"));
  Serial.print(F("[WIFI] IP Address: "));
  Serial.println(WiFi.localIP());

  P.displayClear();
  P.print("WIFI OK");
  delay(1500);

  // Setup Time
  setupTime();
  P.displayClear();
  P.print("NTP OK");
  delay(1500);

  // Initial sensor read
  fetchSensorData(); 

  lastSwitchTime = millis();
  Serial.println(F("[SETUP] Setup complete. Starting main loop."));
}

// --- MAIN LOOP ---
void loop() {
  static unsigned long lastSensorRead = 0;
  const unsigned long sensorReadInterval = 30000; // Read sensor every 30 seconds

  // --- 1. Periodically read from the sensor ---
  if (millis() - lastSensorRead >= sensorReadInterval) {
    lastSensorRead = millis();
    fetchSensorData();
  }

  // --- 2. Toggle between display modes based on duration ---
  unsigned long currentDuration = (displayMode == 0) ? CLOCK_DURATION : SENSOR_DURATION;
  if (millis() - lastSwitchTime >= currentDuration) {
    lastSwitchTime = millis();
    displayMode = (displayMode + 1) % 2; // Toggles between 0 and 1
    P.displayClear(); // Clear the display for the new mode
    Serial.printf("[DISPLAY] Switching to mode %d\n", displayMode);
  }

  // --- 3. Update the display based on the current mode ---
  // P.displayAnimate() returns true when the display is ready for new data.
  if (P.displayAnimate()) {
    if (displayMode == 0) { // Clock Mode
      time_t now = time(nullptr);
      struct tm* timeinfo = localtime(&now);

      if (timeinfo->tm_year > 100) { // Check if time is synced
        char timeStr[6];
        sprintf(timeStr, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
        P.setTextAlignment(PA_CENTER);
        P.print(timeStr);
      } else {
        P.setTextAlignment(PA_CENTER);
        P.print("SYNC..."); // Waiting for NTP sync
      }
    } 
    else if (displayMode == 1) { // Sensor Data Mode
      String sensorDisplay = currentTemp + "C " + currentHumidity + "%";
      P.setTextAlignment(PA_CENTER);
      P.print(sensorDisplay.c_str());
    }
  }
}