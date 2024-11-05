#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "bitmaps.h"

//ESP82266 Board Manager - https://arduino.esp8266.com/stable/package_esp8266com_index.json

// WIFI INFORMATION
#define WIFI_SSID "Hailee"
#define WIFI_PASSWORD "07102010"
#define JSON_MEMORY_BUFFER 1024 * 2

// DISPLAY PINS
#define TFT_CS 15
#define TFT_DC 4
#define TFT_RST 2
// TEMP PIN
#define ONE_WIRE_BUS 2
//Button PIN
#define BUTTON_PIN D3
#define BL_PIN D0

#define ST77XX_LIME 0x07FF
#define ST77XX_GRAY 0x8410

//Setup indoor temp
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Display and WiFiUdp
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
WiFiUDP ntpUDP;

// NTP pool link:-
NTPClient timeClient(ntpUDP, "0.vn.pool.ntp.org");

// Latitude and Longitude of you location.
float lat = 20.98;
float lon = 105.78;
// You can get API KEY and HOST KEY from RapidAPI, Search weatherapi.com and subscribe.
String API_KEY = "f586af33deb007176bc728331310a694";
// API endpoint.
String weather_url = "https://api.openweathermap.org/data/2.5/weather?lat=" + String(lat) + "&lon=" + String(lon) + "&units=metric&appid=" + API_KEY;

// Global variables
String current_time;
String hour;
String minute;
String alternative;
String weekDay;
String month;
String day;
int year;
String temp;
String weather;
float indoorTemp;


int buttonState = 0;
bool displayOn = true;
int brightness = 255;  // Full brightness (255 = 100% duty cycle)
bool dimmed = false;

// Array for days and months
String weekDays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
String months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// For delay in fetching weather data.
unsigned long lastTime = 0;
unsigned long fetch_delay = 1800000;

void setup(void) {
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Configure button pin as input with internal pull-up
  // Initialization
  Serial.begin(9600);
  tft.init(240, 240);
  analogWrite(BL_PIN, brightness);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  timeClient.begin();
  sensors.begin();

  // Set this to you timezone in seconds i.e 5:30 = 19800 seconds;
  timeClient.setTimeOffset(25200);

  // Set display rotation
  tft.setRotation(3);

  // Clear display
  tft.fillScreen(0);

  // Set text color
  tft.setTextColor(ST77XX_WHITE);

  // Set font size
  tft.setTextSize(2);

  String loading = ".";

  // While connecting to wifi
  while (WiFi.status() != WL_CONNECTED) {
    tft.drawBitmap(110, 35, wifi, 30, 30, ST77XX_WHITE);
    tft.setCursor(40, 90);
    tft.println("Connecting to ");
    tft.setCursor(40, 125);
    tft.print(WIFI_SSID);
    tft.println(loading);
    if (loading.length() > 6) {
      loading = "";
    }
    loading += ".";
    delay(500);
  }
  // Clear display
  tft.fillScreen(0);

  // Show connected
  tft.setCursor(60, 110);
  tft.println("Connected!");
  delay(3000);

  // Clear display and fetch tempurature
  tft.fillRect(60, 110, 130, 50, ST77XX_BLACK);
  fetchTemp();
}


void loop() {
  // Update time.
  timeClient.update();
  currentTime();
  // Fetching weather after delay
  if ((millis() - lastTime) > fetch_delay) {
    fetchTemp();
    getIndoorTemp();
    lastTime = millis();
  }
  display();

  buttonState = digitalRead(BUTTON_PIN);  // Read the state of the button

  if (buttonState == LOW) {  // Button is pressed (LOW because of pull-up)
    // Displaying items.
    delay(200);
    dimmed = !dimmed;

    // Set brightness based on the dimmed state
    if (dimmed) {
      brightness = 1;  // Dimmed brightness (adjust this value)
    } else {
      brightness = 255;  // Full brightness
    }

    // Apply brightness to backlight
    analogWrite(BL_PIN, brightness);
    delay(200);
  }
}

void display() {

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  //Time
  tft.setTextSize(13);  // ie. 6x8 * 5 = 30x40
  tft.setCursor(0, 5);
  tft.println(hour);
  tft.setCursor(0, 120);
  tft.println(minute);

  //Wifi
  // int wifiSize = 30;
  // if (WiFi.status() != WL_CONNECTED) {
  //   tft.drawBitmap(185, 8, wifidis, wifiSize, wifiSize, ST77XX_WHITE);
  // } else {
  //   tft.drawBitmap(185, 8, wifi, wifiSize, wifiSize, ST77XX_WHITE);
  // }

  //Date
  tft.setTextSize(4);
  tft.setCursor(160, 45);
  tft.println(weekDay);
  tft.setTextSize(2);
  tft.setCursor(168, 88);
  tft.print(day);
  tft.print("/");
  tft.println(month);

  //Weather
  tft.setTextSize(3);
  tft.drawLine(157, 120, 240, 120, ST77XX_GREEN);
  tft.setCursor(160, 135);
  if (temp != "") {
    // if (temp.toInt() > 30) {
    //   tft.setTextColor(ST77XX_ORANGE);
    // } else if (temp.toInt() < 30 & temp.toInt() > 24) {
    //   tft.setTextColor(ST77XX_GREEN);
    // } else {
    //   tft.setTextColor(ST77XX_CYAN);
    // }
    tft.print(temp);
    tft.print(char(247));
    tft.print("C");
  } else {
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.print("No Data");
  }

  int w_x = 167;
  int w_y = 167;
  //Weather icon
  if (weather.equalsIgnoreCase("clear")){
    if (hour.toInt() > 19 | hour.toInt() < 6) {
      tft.drawBitmap(w_x, w_y, sun, 48, 48, ST77XX_YELLOW);
    } else {
      tft.drawBitmap(w_x, w_y, moon, 48, 48, ST77XX_WHITE);
    }
  } else if (weather.equalsIgnoreCase("clouds")){
    tft.drawBitmap(w_x, w_y, cloud, 48, 48, ST77XX_WHITE);
  } else if (weather.equalsIgnoreCase("rain")){
    tft.drawBitmap(w_x, w_y, rain, 48, 48, ST77XX_WHITE);
  } else if (weather.equalsIgnoreCase("drizzle")){
    tft.drawBitmap(w_x, w_y, drizzle, 48, 48, ST77XX_WHITE);
  } else if (weather.equalsIgnoreCase("thunderstorm")){
    tft.drawBitmap(w_x, w_y, storm, 48, 48, ST77XX_LIME);
  } else if (weather.equalsIgnoreCase("atmosphere")){
    tft.drawBitmap(w_x, w_y, atmos, 48, 48, ST77XX_GRAY);
  } else if (weather.equalsIgnoreCase("snow")){
    tft.drawBitmap(w_x, w_y, snow, 48, 48, ST77XX_WHITE);
  } else {
    tft.drawBitmap(w_x, w_y, clouderror, 48, 48, ST77XX_RED);
  } 

  //Indoor temp
  
  
}

// Formatting and setting time
void currentTime() {
  hour = String(timeClient.getHours());
  minute = String(timeClient.getMinutes());
  weekDay = weekDays[timeClient.getDay()];
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  int d = ptm->tm_mday;
  int current_month = ptm->tm_mon + 1;
  month = String(current_month);
  year = ptm->tm_year + 1900;
  if (d < 10) {
    day = "0" + String(d);
  } else {
    day = String(d);
  }
  if (hour.toInt() < 10) {
    hour = "0" + hour;
  }
  if (minute.toInt() < 10) {
    minute = "0" + minute;
  }
  current_time = String(hour) + ":" + minute;
}

// Getting tempurature from API using Https request
void fetchTemp() {
  WiFiClientSecure client;
  HTTPClient https;
  client.setInsecure();
  https.useHTTP10(true);
  if (https.begin(client, weather_url.c_str())) {
    int httpCode = https.GET();
    if (httpCode > 0) {
      if (httpCode == 200) {
        DynamicJsonDocument doc(JSON_MEMORY_BUFFER);
        DeserializationError error = deserializeJson(doc, https.getStream());
        Serial.println(https.getStream());
        if (error) {
          Serial.println("deserialization error");
          Serial.println(error.f_str());
          temp = "";
          weather = "";
        } else {
          temp = String(doc["main"]["temp"].as<int>());
          weather = String(doc["weather"][0]["main"]);
        }
      }
    }
  }
  https.end();
}

void getIndoorTemp() {
  sensors.requestTemperatures(); 
  indoorTemp = sensors.getTempCByIndex(0);
}
