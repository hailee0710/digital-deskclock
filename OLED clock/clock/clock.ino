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

const char *ssid = "Hailee";        //SET UP YOUR Wi-Fi NAME
const char *password = "07102010";  //SET UP YOUR Wi-Fi PASSWORD

float lat = 20.98;
float lon = 105.78;
String API_KEY = "f586af33deb007176bc728331310a694";
// API endpoint.
String weather_url = "https://api.openweathermap.org/data/2.5/weather?lat=" + String(lat) + "&lon=" + String(lon) + "&units=metric&appid=" + API_KEY;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "0.vn.pool.ntp.org");

String weekDays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
String months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// Define the desired LED activation and deactivation times
const int activateHour = 10;      // Set your activation hour (in 24 Hour Format)
const int activateMinute = 38;    // Set your activation minute
const int deactivateHour = 10;    // Set your deactivation hour (in 24 Hour Format)
const int deactivateMinute = 39;  // Set your deactivation minute

char intro[] = "GIVE THIS VIDEO A LIKE,IF YOU ENJOYED | ROBU.IN";
int x, minX;

String temp;
String weather;

bool deviceActive = false;

#define BUTTON_PIN D3  // Button connected to D3 (GPIO0)
int buttonState = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Configure button pin as input with internal pull-up

  // Initialize the OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.display();  // Initialize and clear the display

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
  buttonState = digitalRead(BUTTON_PIN); // Read the state of the button

  if (buttonState == LOW) { // Button is pressed (LOW because of pull-up)
    clockDisplay();
    delay(3000);
  }
}

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
  int currentYear = ptm->tm_year + 1900;

  display.clearDisplay();
  display.setTextSize(4);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 16);
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
  display.setCursor(45, 0);
  display.print(String(monthDay) + "/" + String(currentMonth));  //+"-"+String(currentYear)
  display.setTextSize(2);
  display.setCursor(0, 50);
  display.print(weather + "|");
  if (temp != "") {
    display.print(temp);
    display.print(char(247));
  } else {
    display.print("---");
  }
  display.clearDisplay(); // Clear the display after 3 seconds
  display.display(); // Turn off OLED
}

// void textScroll() {
//   display.setTextSize(1);
//   display.setCursor(x,55);
//   display.print(intro);
//   x=x-1;
//   if(x < minX) x = display.width();
// }

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
