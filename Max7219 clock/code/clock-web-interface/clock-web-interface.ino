#include <SPI.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <DHT.h>
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
};
Config config;

// ----------------------
// --- PIN DEFINITIONS ---
// ----------------------

// MAX7219 pin definitions
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4 // 8x32 matrix is 4 cascaded 8x8 modules
#define CS_PIN D6     // ESP8266 D1 Mini pin D6
#define CLK_PIN D5    // ESP8266 D1 Mini pin D5
#define DATA_PIN D7   // ESP8266 D1 Mini pin D7

// DHT22 sensor pin
#define DHT_PIN D4   // ESP8266 D1 Mini pin D4
#define DHT_TYPE DHT22

// ----------------------
// --- OBJECTS ---
// ----------------------

// Parola and MAX72xx objects
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// DHT sensor object
DHT dht(DHT_PIN, DHT_TYPE);

// Time client object
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", config.utcOffsetInSeconds);

// ----------------------
// --- GLOBAL VARIABLES ---
// ----------------------

char timeString[16] = "";
char dhtTempHumidString[16] = "";

// Timers for non-blocking updates
unsigned long lastTimeUpdate = 0;
unsigned long lastDhtUpdate = 0;
const long timeUpdateInterval = 10800000; // Update time every 3 hours
const long dhtUpdateInterval = 5000; // Update DHT every 5 seconds

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

  Serial.println("[CONFIG] Configuration loaded successfully.");
  Serial.printf("  SSID: %s\n", config.ssid);
  Serial.printf("  UTC offset: %ld\n", config.utcOffsetInSeconds);
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
  P.setZone(0, 0, 1);
  P.setZone(1, 2, 3);
  P.setTextAlignment(0, PA_CENTER);
  P.setTextAlignment(1, PA_CENTER);
  P.displayZoneText(0, "STARTING", PA_CENTER, 0, 0, PA_SCROLL_LEFT, PA_NO_EFFECT);
  P.displayAnimate();

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
  }
  
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
    P.displayZoneText(0, "CONNECTED", PA_CENTER, 0, 0, PA_SCROLL_LEFT, PA_NO_EFFECT);
    P.displayAnimate();
    
    // Set time offset from loaded config
    timeClient.setUpdateInterval(60000); // 1 minute update interval for NTP client
    timeClient.setTimeOffset(config.utcOffsetInSeconds);
    timeClient.begin();
    timeClient.update();

    dht.begin();
    
  } else {
    // Configuration mode
    Serial.println("Failed to connect to WiFi. Starting configuration mode.");
    
    P.displayZoneText(0, "SETUP", PA_CENTER, 0, 0, PA_SCROLL_LEFT, PA_NO_EFFECT);
    P.displayAnimate();
    
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
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // Normal operation
    if (millis() - lastTimeUpdate > timeUpdateInterval) {
      timeClient.update();
      lastTimeUpdate = millis();
    }
    if (millis() - lastDhtUpdate > dhtUpdateInterval) {
      readDHT();
      lastDhtUpdate = millis();
    }
    displayData();
  } else {
    // Configuration mode
    dns.processNextRequest();
  }
}

/**
 * @brief Displays time and DHT sensor data on the LED matrix.
 *
 * Zone 0 (modules 0-1): current time
 * Zone 1 (modules 2-3): DHT temperature and humidity
 */
void displayData() {
  // Get and format the time
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  sprintf(timeString, "%02d:%02d", hours, minutes);

  // Set the text for each display zone
  P.displayZoneText(0, timeString, PA_CENTER, 0, 100, PA_SCROLL_LEFT, PA_NO_EFFECT);
  P.displayZoneText(1, dhtTempHumidString, PA_CENTER, 0, 100, PA_SCROLL_LEFT, PA_NO_EFFECT);

  // Animate the text and update the display
  P.displayAnimate();
}

/**
 * @brief Reads temperature and humidity from the DHT22 sensor.
 *
 * This function checks for valid readings from the sensor and updates
 * the dhtTempHumidString global variable with the formatted data.
 */
void readDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    strcpy(dhtTempHumidString, "ERR");
    return;
  }

  // Format the string as "25C 50%"
  sprintf(dhtTempHumidString, "%.0fC %.0f%%", t, h);
}
