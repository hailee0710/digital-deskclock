#include <SPI.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include "mfactoryfont.h"

// ----------------------
// --- CONFIGURATION ---
// ----------------------

// Web Server
const char* host = "esp-display";
AsyncWebServer server(80);
DNSServer dns;

// LittleFS config file
const char* configFilePath = "/config.json";

// Configuration structure
struct Config {
  char ssid[33];      // 32 max + null terminator
  char password[65];  // 64 max + null terminator
  float latitude;
  float longitude;
  long utcOffsetInSeconds;
  unsigned long clockDuration;   // seconds to show clock
  unsigned long sensorDuration;  // seconds to show sensor
  int brightness;                // MAX7219 intensity (0-15)
  bool twelveHourToggle;         // true = 12h, false = 24h
  bool showHumidity;             // show humidity in sensor mode
  char ntpServer[65];            // NTP server hostname
};
Config config;

// ----------------------
// --- PIN DEFINITIONS ---
// ----------------------

// MAX7219 pin definitions
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4 // 8x32 matrix is 4 cascaded 8x8 modules
#define CLK_PIN 12   // GPIO12 = D6
#define DATA_PIN 15  // GPIO15 = D8
#define CS_PIN 13    // GPIO13 = D7

// ----------------------
// --- OBJECTS ---
// ----------------------

// Parola and MAX72xx objects
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// AHT sensor object
Adafruit_AHTX0 aht;

// Time client object
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", config.utcOffsetInSeconds);

// ----------------------
// --- GLOBAL VARIABLES ---
// ----------------------

char timeString[16] = "";
char sensorString[16] = "";
String currentTemp = "--";
int currentHumidity = -1;

// Timers for non-blocking updates
unsigned long lastTimeUpdate = 0;
unsigned long lastSensorRead = 0;
const long timeUpdateInterval = 10800000; // Update time every 3 hours
const long sensorUpdateInterval = 5000; // Update sensor every 5 seconds

// Display mode switching (full-display, alternating)
int displayMode = 0;              // 0 = Clock, 1 = Sensor
unsigned long lastSwitch = 0;

// ----------------------
// --- FUNCTIONS ---
// ----------------------

// Loads configuration from LittleFS
bool loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("[CONFIG] LittleFS mount failed!");
    return false;
  }

  if (!LittleFS.exists(configFilePath)) {
    Serial.println("[CONFIG] Config file not found, using defaults.");
    return false;
  }

  File configFile = LittleFS.open(configFilePath, "r");
  if (!configFile) {
    Serial.println("[CONFIG] Failed to open config file for reading.");
    return false;
  }

  size_t size = configFile.size();
  if (size == 0) {
    Serial.println("[CONFIG] Config file is empty.");
    configFile.close();
    return false;
  }

  // Allocate +1 for null terminator - readBytes does NOT null-terminate
  std::unique_ptr<char[]> buf(new char[size + 1]);
  configFile.readBytes(buf.get(), size);
  buf[size] = '\0';
  configFile.close();

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, buf.get());
  if (err) {
    Serial.printf("[CONFIG] JSON parse error: %s\n", err.c_str());
    return false;
  }

  const char* s = doc["ssid"] | "";
  strncpy(config.ssid, s, sizeof(config.ssid) - 1);
  config.ssid[sizeof(config.ssid) - 1] = '\0';

  const char* p = doc["password"] | "";
  strncpy(config.password, p, sizeof(config.password) - 1);
  config.password[sizeof(config.password) - 1] = '\0';

  config.latitude = doc["latitude"] | 0.0;
  config.longitude = doc["longitude"] | 0.0;
  config.utcOffsetInSeconds = doc["utcOffsetInSeconds"] | 0;
  config.clockDuration = doc["clockDuration"] | 10;
  config.sensorDuration = doc["sensorDuration"] | 5;
  config.brightness = doc["brightness"] | 7;
  config.twelveHourToggle = doc["twelveHourToggle"] | false;
  config.showHumidity = doc["showHumidity"] | false;
  const char* ntp = doc["ntpServer"] | "pool.ntp.org";
  strncpy(config.ntpServer, ntp, sizeof(config.ntpServer) - 1);
  config.ntpServer[sizeof(config.ntpServer) - 1] = '\0';

  Serial.println("[CONFIG] Configuration loaded successfully.");
  Serial.printf("  SSID: %s\n", config.ssid);
  Serial.printf("  UTC offset: %ld\n", config.utcOffsetInSeconds);
  Serial.printf("  Clock duration: %lu s\n", config.clockDuration);
  Serial.printf("  Sensor duration: %lu s\n", config.sensorDuration);
  Serial.printf("  Brightness: %d\n", config.brightness);
  Serial.printf("  12h toggle: %s\n", config.twelveHourToggle ? "on" : "off");
  return true;
}

// Saves configuration to LittleFS
bool saveConfig() {
  if (!LittleFS.begin()) {
    Serial.println("[CONFIG] LittleFS mount failed during save!");
    return false;
  }

  File configFile = LittleFS.open(configFilePath, "w");
  if (!configFile) {
    Serial.println("[CONFIG] Failed to open config file for writing.");
    return false;
  }

  DynamicJsonDocument doc(1024);
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  doc["latitude"] = config.latitude;
  doc["longitude"] = config.longitude;
  doc["utcOffsetInSeconds"] = config.utcOffsetInSeconds;
  doc["clockDuration"] = config.clockDuration;
  doc["sensorDuration"] = config.sensorDuration;
  doc["brightness"] = config.brightness;
  doc["twelveHourToggle"] = config.twelveHourToggle;
  doc["showHumidity"] = config.showHumidity;
  doc["ntpServer"] = config.ntpServer;

  size_t written = serializeJson(doc, configFile);
  configFile.close();

  if (written == 0) {
    Serial.println("[CONFIG] Failed to write config (0 bytes).");
    return false;
  }

  Serial.printf("[CONFIG] Configuration saved (%u bytes).\n", written);
  return true;
}

// Handles the root page of the web server
void handleRoot(AsyncWebServerRequest *request) {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
    <title>ESP Display Config</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial, sans-serif; background-color: #f0f0f0; padding: 20px; }
      .container { max-width: 500px; margin: auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
      h1 { text-align: center; color: #333; }
      .form-group { margin-bottom: 15px; }
      label { display: block; margin-bottom: 5px; color: #555; }
      input[type="text"], input[type="password"], input[type="number"] { width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
      button { width: 100%; padding: 10px; background-color: #007BFF; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
      button:hover { background-color: #0056b3; }
      .message { text-align: center; margin-top: 10px; color: green; }
    </style>
    </head>
    <body>
    <div class="container">
      <h1>Display Configuration</h1>
      <form action="/save" method="post">
        <div class="form-group">
          <label for="ssid">WiFi SSID</label>
          <input type="text" id="ssid" name="ssid" value="%SSID%" required>
        </div>
        <div class="form-group">
          <label for="password">WiFi Password</label>
          <input type="password" id="password" name="password" value="%PASSWORD%">
        </div>
        <div class="form-group">
          <label for="latitude">Latitude</label>
          <input type="number" step="any" id="latitude" name="latitude" value="%LATITUDE%" required>
        </div>
        <div class="form-group">
          <label for="longitude">Longitude</label>
          <input type="number" step="any" id="longitude" name="longitude" value="%LONGITUDE%" required>
        </div>
        <div class="form-group">
          <label for="utcOffset">Timezone Offset (seconds from GMT)</label>
          <input type="number" id="utcOffset" name="utcOffset" value="%UTCOFFSET%" required>
        </div>
        <div class="form-group">
          <label for="clockDuration">Clock Display Duration (seconds)</label>
          <input type="number" id="clockDuration" name="clockDuration" value="%CLOCKDURATION%" required>
        </div>
        <div class="form-group">
          <label for="sensorDuration">Sensor Display Duration (seconds)</label>
          <input type="number" id="sensorDuration" name="sensorDuration" value="%SENSORDURATION%" required>
        </div>
        <div class="form-group">
          <label for="brightness">Brightness (0-15)</label>
          <input type="number" id="brightness" name="brightness" min="0" max="15" value="%BRIGHTNESS%" required>
        </div>
        <div class="form-group">
          <label for="twelveHourToggle">12-Hour Clock</label>
          <input type="checkbox" id="twelveHourToggle" name="twelveHourToggle" value="true" %12H_CHECKED%>
        </div>
        <div class="form-group">
          <label for="showHumidity">Show Humidity</label>
          <input type="checkbox" id="showHumidity" name="showHumidity" value="true" %HUMID_CHECKED%>
        </div>
        <div class="form-group">
          <label for="ntpServer">NTP Server</label>
          <input type="text" id="ntpServer" name="ntpServer" value="%NTPSERVER%" required>
        </div>
        <button type="submit">Save & Restart</button>
      </form>
    </div>
    </body>
    </html>
  )rawliteral";
  
  html.replace("%SSID%", config.ssid);
  html.replace("%PASSWORD%", config.password);
  html.replace("%LATITUDE%", String(config.latitude, 6));
  html.replace("%LONGITUDE%", String(config.longitude, 6));
  html.replace("%UTCOFFSET%", String(config.utcOffsetInSeconds));
  html.replace("%CLOCKDURATION%", String(config.clockDuration));
  html.replace("%SENSORDURATION%", String(config.sensorDuration));
  html.replace("%BRIGHTNESS%", String(config.brightness));
  html.replace("%12H_CHECKED%", config.twelveHourToggle ? "checked" : "");
  html.replace("%HUMID_CHECKED%", config.showHumidity ? "checked" : "");
  html.replace("%NTPSERVER%", config.ntpServer);

  request->send(200, "text/html", html);
}

void handleSave(AsyncWebServerRequest *request) {
  // Save new settings
  if (request->hasArg("ssid")) {
    strncpy(config.ssid, request->arg("ssid").c_str(), sizeof(config.ssid) - 1);
    config.ssid[sizeof(config.ssid) - 1] = '\0';
  }
  if (request->hasArg("password")) {
    strncpy(config.password, request->arg("password").c_str(), sizeof(config.password) - 1);
    config.password[sizeof(config.password) - 1] = '\0';
  }
  if (request->hasArg("latitude")) {
    config.latitude = request->arg("latitude").toFloat();
  }
  if (request->hasArg("longitude")) {
    config.longitude = request->arg("longitude").toFloat();
  }
  if (request->hasArg("utcOffset")) {
    config.utcOffsetInSeconds = request->arg("utcOffset").toInt();
  }
  if (request->hasArg("clockDuration")) {
    long val = request->arg("clockDuration").toInt();
    if (val > 0) config.clockDuration = val;
  }
  if (request->hasArg("sensorDuration")) {
    long val = request->arg("sensorDuration").toInt();
    if (val > 0) config.sensorDuration = val;
  }
  if (request->hasArg("brightness")) {
    int val = request->arg("brightness").toInt();
    if (val < 0) val = 0;
    if (val > 15) val = 15;
    config.brightness = val;
  }
  // Checkboxes: only submitted when checked; absent = false
  config.twelveHourToggle = (request->hasArg("twelveHourToggle") && request->arg("twelveHourToggle") == "true");
  config.showHumidity = (request->hasArg("showHumidity") && request->arg("showHumidity") == "true");
  if (request->hasArg("ntpServer")) {
    strncpy(config.ntpServer, request->arg("ntpServer").c_str(), sizeof(config.ntpServer) - 1);
    config.ntpServer[sizeof(config.ntpServer) - 1] = '\0';
  }
  
  if (!saveConfig()) {
    request->send(500, "text/plain", "Error saving configuration.");
    return;
  }
  
  request->send(200, "text/plain", "Configuration saved. Restarting...");
  
  // Restart the device
  delay(1000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);

  // Initialize display
  P.begin();
  P.setIntensity(10);
  P.setPause(0);
  P.setCharSpacing(1);
  P.setFont(mFactory);
  P.displayClear();
  P.print("STARTING");

  // Load configuration
  if (!loadConfig()) {
    // Default values if config file not found
    strncpy(config.ssid, "YOUR_WIFI_SSID", sizeof(config.ssid) - 1);
    config.ssid[sizeof(config.ssid) - 1] = '\0';
    strncpy(config.password, "", sizeof(config.password) - 1);
    config.password[sizeof(config.password) - 1] = '\0';
    config.latitude = 20.9714;
    config.longitude = 105.7788;
    config.utcOffsetInSeconds = 7 * 3600;
    config.clockDuration = 10;
    config.sensorDuration = 5;
    config.brightness = 7;
    config.twelveHourToggle = false;
    config.showHumidity = false;
    strncpy(config.ntpServer, "pool.ntp.org", sizeof(config.ntpServer) - 1);
    config.ntpServer[sizeof(config.ntpServer) - 1] = '\0';
  }

  P.setIntensity(config.brightness);
  
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    // Normal mode
    Serial.println("WiFi connected.");
    P.print("CONNECTED");
    
    // Set time offset from loaded config
    timeClient.setUpdateInterval(60000); // 1 minute update interval for NTP client
    timeClient.setTimeOffset(config.utcOffsetInSeconds);
    timeClient.setPoolServerName(config.ntpServer);
    timeClient.begin();
    timeClient.update();

    if (!aht.begin()) {
      Serial.println("[SENSOR] Could not find AHT sensor!");
    } else {
      Serial.println("[SENSOR] AHT sensor found.");
    }

    // Setup web server for normal mode too
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound([](AsyncWebServerRequest *request){
      request->redirect("/");
    });
    server.begin();
    Serial.println("Web server started.");
    
  } else {
    // Configuration mode
    Serial.println("Failed to connect to WiFi. Starting configuration mode.");
    
    P.print("SETUP");
    
    // Start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(host, "password"); // Default password for setup
    dns.start(53, "*", WiFi.softAPIP());

    // Setup web server
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound([](AsyncWebServerRequest *request){
      request->redirect("/");
    });
    server.begin();
    Serial.println("Web server started. Connect to 'esp-display' WiFi.");
  }

  lastSwitch = millis();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // --- NTP Update ---
    if (millis() - lastTimeUpdate > timeUpdateInterval) {
      timeClient.update();
      lastTimeUpdate = millis();
    }

    // --- Sensor Update ---
    if (millis() - lastSensorRead > sensorUpdateInterval) {
      fetchSensorData();
      lastSensorRead = millis();
    }

    // --- Display Mode Switching ---
    unsigned long currentDuration = (displayMode == 0) ? config.clockDuration : config.sensorDuration;
    if (millis() - lastSwitch > (currentDuration * 1000)) {
      advanceDisplayMode();
    }

    // --- Display Rendering ---
    if (P.displayAnimate()) {
      P.setTextAlignment(PA_CENTER);

      if (displayMode == 0) {  // Clock
        int hours = timeClient.getHours();
        int minutes = timeClient.getMinutes();
        if (config.twelveHourToggle) {
          int hour12 = hours % 12;
          if (hour12 == 0) hour12 = 12;
          sprintf(timeString, "%d:%02d", hour12, minutes);
        } else {
          sprintf(timeString, "%02d:%02d", hours, minutes);
        }
        P.print(timeString);
      } else {  // Sensor
        if (config.showHumidity && currentHumidity >= 0) {
          sprintf(sensorString, "%sC %d%%", currentTemp.c_str(), currentHumidity);
        } else {
          strcpy(sensorString, currentTemp.c_str());
        }
        P.print(sensorString);
      }
    }
  } else {
    // Configuration mode
    dns.processNextRequest();
  }
}

/**
 * @brief Advances to the next display mode with a clean transition.
 *
 * Modes: 0 = Clock, 1 = Sensor. Clears the display on each switch
 * so the new text appears cleanly on the full 32-column matrix.
 */
void advanceDisplayMode() {
  P.displayClear();
  displayMode = (displayMode == 0) ? 1 : 0;
  lastSwitch = millis();
  Serial.printf("[DISPLAY] Switched to mode %d\n", displayMode);
}

/**
 * @brief Reads temperature and humidity from the AHT sensor.
 *
 * Uses the Adafruit AHTX0 library to read environmental data.
 * Updates currentTemp and currentHumidity globals on success.
 */
void fetchSensorData() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  if (isnan(temp.temperature) || isnan(humidity.relative_humidity)) {
    Serial.println("[SENSOR] Failed to read from AHT sensor!");
    return;
  }

  currentTemp = String(temp.temperature, 1);
  currentHumidity = (int)round(humidity.relative_humidity);
  Serial.printf("[SENSOR] Temp: %s, Humidity: %d%%\n", currentTemp.c_str(), currentHumidity);
}
