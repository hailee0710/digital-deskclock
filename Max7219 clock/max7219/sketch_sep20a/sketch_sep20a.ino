#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <sntp.h>
#include <time.h>
#include <Adafruit_AHTX0.h>

#include "mfactoryfont.h"   // Custom font
#include "tz_lookup.h"      // Timezone lookup
#include "days_lookup.h"    // Languages for the Days of the Week
#include "months_lookup.h"  // Languages for the Months of the Year

// --- HARDWARE & SENSOR SETTINGS ---
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN 12
#define DATA_PIN 15
#define CS_PIN 13

// --- GLOBAL OBJECTS ---
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
AsyncWebServer server(80);
Adafruit_AHTX0 aht;
const byte DNS_PORT = 53;
DNSServer dnsServer;

// --- CONFIGURATION GLOBALS ---
char ssid[32] = "";
char password[32] = "";
char timeZone[64] = "";
char language[8] = "en";
unsigned long clockDuration = 10000;
unsigned long sensorDuration = 5000;
int brightness = 0;
bool twelveHourToggle = false;
bool showDayOfWeek = true;
bool showDate = false;
bool showHumidity = false;
bool colonBlinkEnabled = true;
char ntpServer1[64] = "pool.ntp.org";
char ntpServer2[64] = "time.nist.gov";

// --- STATE MANAGEMENT ---
bool isAPMode = false;
unsigned long lastSwitch = 0;
int displayMode = 0;  // 0: Clock, 1: Sensor, 2: Date
bool ntpSyncSuccessful = false;
String currentTemp = "--";
int currentHumidity = -1;

// -----------------------------------------------------------------------------
// CONFIGURATION LOAD & SAVE
// -----------------------------------------------------------------------------
void loadConfig() {
  Serial.println(F("[CONFIG] Loading configuration..."));
  if (!LittleFS.exists("/config.json")) {
    Serial.println(F("[CONFIG] config.json not found, creating default."));
    // Create a default config file
  }

  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println(F("[ERROR] Failed to open config.json."));
    return;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.println(F("[ERROR] Failed to parse config.json."));
    return;
  }

  strlcpy(ssid, doc["ssid"] | "", sizeof(ssid));
  strlcpy(password, doc["password"] | "", sizeof(password));
  strlcpy(timeZone, doc["timeZone"] | "Etc/UTC", sizeof(timeZone));
  strlcpy(language, doc["language"] | "en", sizeof(language));
  clockDuration = doc["clockDuration"] | 10000;
  sensorDuration = doc["sensorDuration"] | 5000;
  brightness = doc["brightness"] | 7;
  twelveHourToggle = doc["twelveHourToggle"] | false;
  showDayOfWeek = doc["showDayOfWeek"] | true;
  showDate = doc["showDate"] | false;
  showHumidity = doc["showHumidity"] | false;
  colonBlinkEnabled = doc.containsKey("colonBlinkEnabled") ? doc["colonBlinkEnabled"].as<bool>() : true;
  strlcpy(ntpServer1, doc["ntpServer1"] | "pool.ntp.org", sizeof(ntpServer1));
  strlcpy(ntpServer2, doc["ntpServer2"] | "time.nist.gov", sizeof(ntpServer2));

  Serial.println(F("[CONFIG] Configuration loaded."));
}

// -----------------------------------------------------------------------------
// SENSOR & TIME FUNCTIONS
// -----------------------------------------------------------------------------
void fetchSensorData() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp); // populate temp and humidity objects

  if (isnan(temp.temperature) || isnan(humidity.relative_humidity)) {
    Serial.println(F("[SENSOR] Failed to read from AHT sensor!"));
    return;
  }

  currentTemp = String(temp.temperature, 1);
  currentHumidity = (int)round(humidity.relative_humidity);
  Serial.printf("[SENSOR] Temp: %s, Humidity: %d%%\n", currentTemp.c_str(), currentHumidity);
}

void setupTime() {
  sntp_stop();
  Serial.println(F("[TIME] Starting NTP sync..."));
  configTime(0, 0, ntpServer1, ntpServer2);
  setenv("TZ", ianaToPosix(timeZone), 1);
  tzset();
}

// -----------------------------------------------------------------------------
// WIFI & WEBSERVER
// -----------------------------------------------------------------------------
void connectWiFi() {
  if (strlen(ssid) == 0) {
    isAPMode = true;
    return; // No credentials, go straight to AP mode in setup
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print(F("[WIFI] Connecting to "));
  Serial.println(ssid);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startAttemptTime > 20000) { // 20 second timeout
      Serial.println(F("\n[WIFI] Connection failed."));
      isAPMode = true;
      return;
    }
  }

  Serial.println(F("\n[WIFI] Connected!"));
  Serial.print(F("[WIFI] IP Address: "));
  Serial.println(WiFi.localIP());
  isAPMode = false;
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/config.json", "application/json");
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    // Load existing config to update it
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      deserializeJson(doc, configFile);
      configFile.close();
    }

    // Update values from the form
    if (request->hasParam("ssid", true)) strlcpy(ssid, request->getParam("ssid", true)->value().c_str(), sizeof(ssid));
    if (request->hasParam("password", true)) strlcpy(password, request->getParam("password", true)->value().c_str(), sizeof(password));
    if (request->hasParam("timeZone", true)) strlcpy(timeZone, request->getParam("timeZone", true)->value().c_str(), sizeof(timeZone));
    if (request->hasParam("language", true)) strlcpy(language, request->getParam("language", true)->value().c_str(), sizeof(language));
    if (request->hasParam("clockDuration", true)) {
      int val = request->getParam("clockDuration", true)->value().toInt();
      if (val > 0) clockDuration = val;
    }
    if (request->hasParam("sensorDuration", true)) {
      int val = request->getParam("sensorDuration", true)->value().toInt();
      if (val > 0) sensorDuration = val;
    }
    if (request->hasParam("brightness", true)) brightness = request->getParam("brightness", true)->value().toInt();
    if (request->hasParam("twelveHourToggle", true)) twelveHourToggle = request->getParam("twelveHourToggle", true)->value() == "true";
    if (request->hasParam("showDayOfWeek", true)) showDayOfWeek = request->getParam("showDayOfWeek", true)->value() == "true";
    if (request->hasParam("showDate", true)) showDate = request->getParam("showDate", true)->value() == "true";
    if (request->hasParam("showHumidity", true)) showHumidity = request->getParam("showHumidity", true)->value() == "true";
    if (request->hasParam("colonBlinkEnabled", true)) colonBlinkEnabled = request->getParam("colonBlinkEnabled", true)->value() == "true";

    // Save back to JSON
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["timeZone"] = timeZone;
    doc["language"] = language;
    doc["clockDuration"] = clockDuration;
    doc["sensorDuration"] = sensorDuration;
    doc["brightness"] = brightness;
    doc["twelveHourToggle"] = twelveHourToggle;
    doc["showDayOfWeek"] = showDayOfWeek;
    doc["showDate"] = showDate;
    doc["showHumidity"] = showHumidity;
    doc["colonBlinkEnabled"] = colonBlinkEnabled;

    File newConfigFile = LittleFS.open("/config.json", "w");
    serializeJson(doc, newConfigFile);
    newConfigFile.close();

    request->send(200, "text/plain", "Settings saved. Rebooting...");
    delay(1000);
    ESP.restart();
  });

  server.begin();
  Serial.println(F("[WEBSERVER] Web server started."));
}

void startAPMode() {
  const char *AP_SSID = "DeskClockSetup";
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(DNS_PORT, "*", apIP);

  Serial.println(F("\n[WIFI] AP Mode started."));
  Serial.print(F("[WIFI] SSID: "));
  Serial.println(AP_SSID);
  Serial.print(F("[WIFI] IP Address: "));
  Serial.println(WiFi.softAPIP());

  P.displayClear();
  P.print("SETUP");

  setupWebServer();
}

// -----------------------------------------------------------------------------
// SETUP & LOOP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n[SETUP] Starting..."));

  P.begin();
  P.setIntensity(brightness);
  P.setCharSpacing(1);
  P.setFont(mFactory);
  P.displayClear();
  P.print("BOOT");

    if (!aht.begin()) {
    Serial.println(F("[SENSOR] Could not find AHT sensor!"));
  } else {
    Serial.println(F("[SENSOR] AHT sensor found."));
  }

  if (!LittleFS.begin()) {
    Serial.println(F("[ERROR] LittleFS mount failed!"));
  } else {
    loadConfig();
  }

  connectWiFi();

  if (isAPMode) {
    startAPMode();
  } else {
    P.print("WIFI OK");
    delay(1000);
    setupTime();
    fetchSensorData();
    setupWebServer();
  }

  lastSwitch = millis();
  Serial.println(F("[SETUP] Setup complete."));
}

void advanceDisplayMode() {
  int oldMode = displayMode;
  P.displayClear();

  if (displayMode == 0) { // From Clock
    displayMode = showDate ? 2 : 1; // To Date or Sensor
  } else if (displayMode == 2) { // From Date
    displayMode = 1; // To Sensor
  } else if (displayMode == 1) { // From Sensor
    displayMode = 0; // To Clock
  }
  
  Serial.printf("[DISPLAY] Switching from mode %d to %d\n", oldMode, displayMode);
  lastSwitch = millis();
}

void loop() {
  if (isAPMode) {
    dnsServer.processNextRequest();
    return;
  }

  // --- NTP Sync Check ---
  if (!ntpSyncSuccessful) {
    time_t now = time(nullptr);
    if (now > 1000) {
      ntpSyncSuccessful = true;
      Serial.println(F("[TIME] NTP sync successful."));
    }
  }

  // --- Periodic Sensor Read ---
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 30000) { // Read every 30 seconds
    fetchSensorData();
    lastSensorRead = millis();
  }

  // --- Display Mode Switching ---
  unsigned long currentDuration = (displayMode == 0) ? clockDuration : sensorDuration;
  if (millis() - lastSwitch > (currentDuration * 1000)) {
    advanceDisplayMode();
  }

  // --- Display Rendering ---
  if (P.displayAnimate()) {
    P.setTextAlignment(PA_CENTER);

    if (displayMode == 0) { // Clock Mode
      if (!ntpSyncSuccessful) {
        P.print("SYNC...");
      } else {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        char timeStr[6];
        if (twelveHourToggle) {
          int hour12 = timeinfo->tm_hour % 12;
          if (hour12 == 0) hour12 = 12;
          sprintf(timeStr, "%d:%02d", hour12, timeinfo->tm_min);
        } else {
          sprintf(timeStr, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
        }
        P.print(timeStr);
      }
    } 
    else if (displayMode == 1) { // Sensor Mode
      String sensorDisplay = currentTemp;
      if (showHumidity) {
        sensorDisplay += " " + String(currentHumidity) + "%";
      }
      P.print(sensorDisplay.c_str());
    }
    else if (displayMode == 2) { // Date Mode
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        const char* const* months = getMonthsOfYear(language);
        const char* const* days = getDaysOfWeek(language);
        char dateStr[20];
        sprintf(dateStr, "%s %d", days[timeinfo->tm_wday], timeinfo->tm_mday);
        P.print(dateStr);
    }
  }
}
