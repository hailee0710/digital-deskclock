#include <SPI.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// ----------------------
// --- CONFIGURATION ---
// ----------------------

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// OpenWeatherMap API details
const char* openWeatherApiKey = "YOUR_OPENWEATHER_API_KEY";
const char* city = "YOUR_CITY"; // e.g., "London"
const char* countryCode = "YOUR_COUNTRY_CODE"; // e.g., "uk"
// You must get an API key from https://openweathermap.org/api
String openWeatherUrl = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "," + String(countryCode) + "&APPID=" + String(openWeatherApiKey) + "&units=metric";

// Time settings
const long utcOffsetInSeconds = 7 * 3600; // Offset for GMT+7 (e.g., Vietnam)
// Change this to your timezone's offset. e.g., for GMT+1, it's 3600.

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
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds, 60000); // 60s update interval

// ----------------------
// --- GLOBAL VARIABLES ---
// ----------------------

char timeString[16] = "";
char weatherTempString[8] = "";
char dhtTempHumidString[8] = "";

// Timers for non-blocking updates
unsigned long lastTimeUpdate = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastDhtUpdate = 0;
const long timeUpdateInterval = 10800000; // Update time every 3 hours
const long weatherUpdateInterval = 10800000; // Update weather every 3 hours
const long dhtUpdateInterval = 5000; // Update DHT every 5 seconds

// ----------------------
// --- FUNCTIONS ---
// ----------------------

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  // Initialize NTP client
  timeClient.begin();
  timeClient.update();

  // Initialize the MAX7219 display
  P.begin();
  P.setIntensity(10); // Adjust brightness (0-15)
  P.setPause(0); // No pause between scrolling text
  P.displayClear();
  
  // Set up display zones
  // Zone 0 for time (first 2 modules, columns 0-15)
  P.setZone(0, 0, 1);
  // Zone 1 for weather (third module, columns 16-23)
  P.setZone(1, 2, 2);
  // Zone 2 for DHT (fourth module, columns 24-31)
  P.setZone(2, 3, 3);
  
  P.setZoneTextAlignment(0, PA_CENTER);
  P.setZoneTextAlignment(1, PA_CENTER);
  P.setZoneTextAlignment(2, PA_CENTER);

  // Initialize the DHT sensor
  dht.begin();
}

void loop() {
  // Check if it's time to update the time
  if (millis() - lastTimeUpdate > timeUpdateInterval) {
    timeClient.update();
    Serial.println("Time updated.");
    lastTimeUpdate = millis();
  }

  // Check if it's time to update the weather
  if (millis() - lastWeatherUpdate > weatherUpdateInterval) {
    fetchOpenWeather();
    Serial.println("Weather updated.");
    lastWeatherUpdate = millis();
  }

  // Check if it's time to update the DHT readings
  if (millis() - lastDhtUpdate > dhtUpdateInterval) {
    readDHT();
    Serial.println("DHT updated.");
    lastDhtUpdate = millis();
  }

  // Animate the display with the current data
  displayData();
}

/**
 * @brief Displays all data on the LED matrix.
 *
 * This function handles displaying the time, weather, and DHT
 * readings on their respective zones of the 32-column display.
 */
void displayData() {
  // Get and format the time
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  sprintf(timeString, "%02d:%02d", hours, minutes);

  // Set the text for each display zone
  P.displayZoneText(0, timeString, PA_CENTER, 0, 100, PA_SCROLL_LEFT, PA_NO_EFFECT);
  P.displayZoneText(1, weatherTempString, PA_CENTER, 0, 100, PA_SCROLL_LEFT, PA_NO_EFFECT);
  P.displayZoneText(2, dhtTempHumidString, PA_CENTER, 0, 100, PA_SCROLL_LEFT, PA_NO_EFFECT);

  // Animate the text and update the display
  P.displayAnimate();
}

/**
 * @brief Fetches temperature data from OpenWeatherMap.
 *
 * This function connects to the OpenWeatherMap API, parses the JSON
 * response, and updates the weatherTempString global variable.
 */
void fetchOpenWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;

    http.begin(client, openWeatherUrl);
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      float temp = doc["main"]["temp"];
      sprintf(weatherTempString, "%.0fC", temp); // Format to 0 decimal places
    } else {
      Serial.printf("HTTP request failed, error: %s\n", http.errorToString(httpCode).c_str());
      strcpy(weatherTempString, "ERR");
    }
    http.end();
  }
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
