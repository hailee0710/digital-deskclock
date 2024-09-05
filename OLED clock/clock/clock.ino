#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define JSON_MEMORY_BUFFER 1024 * 2

//link: https://robu.in/tim-e-a-digital-clock-using-seeed-studio-xiao-esp32s3/
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *ssid = "HL";              //SET UP YOUR Wi-Fi NAME
const char *password = "1234567890";  //SET UP YOUR Wi-Fi PASSWORD

float lat = 20.98;
float lon = 105.78;
String API_KEY = "f586af33deb007176bc728331310a694";
// API endpoint.
String weather_url = "https://api.openweathermap.org/data/2.5/weather?lat=" + String(lat) + "&lon=" + String(lon) + "&units=metric&appid=" + API_KEY;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "0.vn.pool.ntp.org");

String weekDays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
String months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const int ledPin = 13;     // Change this to your actual LED pin number on the Xiao ESP32 S3
const int buzzerPin = 12;  // Change this to your actual buzzer pin number on the Xiao ESP32 S3

// Define the desired LED activation and deactivation times
const int activateHour = 10;      // Set your activation hour (in 24 Hour Format)
const int activateMinute = 38;    // Set your activation minute
const int deactivateHour = 10;    // Set your deactivation hour (in 24 Hour Format)
const int deactivateMinute = 39;  // Set your deactivation minute

char intro[] = "GIVE THIS VIDEO A LIKE,IF YOU ENJOYED | ROBU.IN";
int x, minX;

String temp;

bool deviceActive = false;

void wifiConnect() {
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("WiFi");
  display.setCursor(0, 30);
  display.println("Connecting");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("WiFi");
  display.setCursor(0, 30);
  display.println("Connected");
  display.display();
  delay(2000);
}

void clockDisplay() {
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int currentSecond = timeClient.getSeconds();
  // String am_pm = (currentHour < 12) ? "AM" : "PM";
  // if (currentHour == 0) {
  //   currentHour = 12; // 12 AM
  // } else if (currentHour > 12) {
  //   currentHour -= 12;
  // }

  String weekDay = weekDays[timeClient.getDay()];
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime(&epochTime);
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon + 1;
  String currentMonthName = months[currentMonth - 1];
  int currentYear = ptm->tm_year + 1900;

  display.clearDisplay();
  display.setTextSize(4);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(1, 3);
  String displayHour = String(currentHour);
  String displayMinute = String(currentMinute);

  if (currentHour < 10) {
    displayHour = '0' + displayHour;
  }
  if (currentMinute < 10) {
    displayMinute = '0' + displayMinute;
  }
  if (currentSecond % 2 == 0) {
    display.print(displayHour + " " + displayMinute);
  } else {
    display.print(displayHour + ":" + displayMinute);
  }
  display.setTextSize(2);
  display.setCursor(10, 50);
  display.println(String(monthDay) + "-" + String(currentMonthName));  //+"-"+String(currentYear)
  display.setTextSize(2);
  display.setCursor(80, 50);
  if (temp != "") {
    display.print(temp);
    display.print(char(247));
    display.print("C");
  } else {
    display.print("---");
  }
}

// void textScroll() {
//   display.setTextSize(1);
//   display.setCursor(x,55);
//   display.print(intro);
//   x=x-1;
//   if(x < minX) x = display.width();
// }

void deviceActivation() {
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  if ((currentHour > activateHour || (currentHour == activateHour && currentMinute >= activateMinute)) && (currentHour < deactivateHour || (currentHour == deactivateHour && currentMinute < deactivateMinute))) {
    deviceActive = true;
    digitalWrite(ledPin, HIGH);  // Activate LED
  } else {
    deviceActive = false;
    digitalWrite(ledPin, LOW);  // Deactivate LED
  }
}

void setup() {

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.clearDisplay();
  display.display();  // Initialize and clear the display

  pinMode(ledPin, OUTPUT);     // Initialize the LED pin
  pinMode(buzzerPin, OUTPUT);  // Initialize the buzzer pin

  wifiConnect();

  timeClient.begin();
  timeClient.setTimeOffset(25200);
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  if ((currentHour > activateHour || (currentHour == activateHour && currentMinute >= activateMinute)) && (currentHour < deactivateHour || (currentHour == deactivateHour && currentMinute < deactivateMinute))) {
    deviceActive = true;
    digitalWrite(ledPin, HIGH);  // Activate LED
  }
  fetchTemp();
}

void loop() {
  clockDisplay();

  // textScroll();
  display.display();
  deviceActivation();

  static bool previousDeviceActive = false;
  if (deviceActive != previousDeviceActive) {
    previousDeviceActive = deviceActive;
    digitalWrite(buzzerPin, HIGH);  // Activate Buzzer
    delay(1000);                    // Keep the buzzer on for 1 second
    digitalWrite(buzzerPin, LOW);   // Deactivate Buzzer
  }
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
        display.print(https.getStream());
        if (error) {
          display.println("deserialization error");
          display.println(error.f_str());
          temp = "";
        } else {
          temp = String(doc["main"]["temp"].as<int>());
        }
      }
    }
  }
  https.end();
}
