// Custom Icons
byte thingSpeakSymbol[8] = {B00100, B01110, B11111, B00100, B00100, B00100, B00100, B11111};
byte batterySymbol[8] = {B01110, B11011, B10001, B10001, B10001, B10011, B10111, B11111};
byte wifiIcon[8] = {B00000, B01110, B10001, B00100, B01010, B00000, B00100, B00000};
byte thermocoupleIcon[8] = {B00100, B00100, B00100, B00100, B00100, B01110, B01110, B00100};
byte tempIcon[8] = {B00100, B01010, B01010, B01110, B01110, B11111, B11111, B01110};
byte humidityIcon[8] = {B00100, B00100, B01010, B01010, B10001, B10011, B10111, B01110};
byte heatIcon[8] = {
    B00100,
    B01010,
    B01010,
    B01110,
    B01110,
    B11111,
    B01110,
    B00100};

#include <Adafruit_SHT4x.h>
#include <WiFi.h>
#include "ThingSpeak.h"
#include <ESP32Ping.h>
#include <CircularBuffer.hpp>
#include <LCD_I2C.h>
#include <OneButton.h>
#include <Adafruit_ADS7830.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>

// WiFi credentials
const char *ssid = "butternutsquash";
const char *password = "5129659724";
// WiFi credentials backup
const char *ssid2 = "832";
const char *password2 = "83221266";

// ThingSpeak channel details
unsigned long myChannelNumber = 2804994;
const char *myWriteAPIKey = "AMHQECWI699QY540";
unsigned long myChannelNumber2 = 2805533; // mats
const char *myWriteAPIKey2 = "L9MT2L33AUR5FYH6";
const char *myReadAPIKey2 = "FGSY5318I773XD5K";

WiFiClient client;

// Pin definitions for MAX31855
#define THERMO_SCK 21 // SPI Clock pin
#define THERMO_CS 19  // Chip Select pin
#define THERMO_SO 18  // Serial Out (MISO)

// Pin definitions for I2C LCD
#define I2C_SDA 23 // I2C Data pin
#define I2C_SCL 22 // I2C Clock pin

// Initialize MAX31855 object
Adafruit_MAX31855 thermocouple(THERMO_SCK, THERMO_CS, THERMO_SO);

// Initialize the LCD (address 0x3F, 16 columns, 2 rows)
LCD_I2C lcd(0x3F, 16, 2); // Change address to 0x27 if your LCD uses that address

// Global variables
Adafruit_SHT4x sht4 = Adafruit_SHT4x();
Adafruit_ADS7830 ads7830;
// OneButton button(5, true); // Button on GPIO0 (active LOW)
OneButton button(0, true); // Button on GPIO0 (active LOW)

float shtTemp, shtHumidity;
int batteryPercentage;
double avgThermocoupleTemp = 0;
float voltages[8];
int pingResult = 0;

CircularBuffer<double, 10> thermocoupleBuffer;

unsigned long lastSampleTime = 0;
unsigned long lastDisplayTime = 0;
unsigned long previousThingSpeakTime = 0;
unsigned long lastPingUpdate = 0;
unsigned long scrollStartTime = 0; // Track when scrolling started

// Scrolling text variables
unsigned long lastScrollTime = 0;
int scrollPosition = 0;
const char *currentScrollText = nullptr;
int currentScrollRow = 0;

bool displayFahrenheit = false;
bool inMenuMode = false; // Flag to indicate if menu mode is active
bool backlightOn = true;

int currentMood = 1;              // Tracks the current mood (1: Regular, 2: Slow, 3: Slow + Backlight Off)
unsigned long moodMultiplier = 1; // Multiplier for event intervals (1x for Mood 1, 3x for Mood 2/3)

// Function prototypes
void setupWiFi();
void setupSHT4x();
void setupADS7830();
void readSHT4x(float &temperature, float &humidity);
void readAllChannels(float *voltages);
void updatePing();
double calculateAverageTemperature();
void updateLCD(double thermocoupleTemp, float shtTemp, float shtHumidity, int batteryPercentage);
void sendToThingSpeak(double thermoTemp, float shtTemp, float shtHumidity, float vbat, float vusb, float v3_3, float v5);
void ScrollText(const char *text, int row, int scroll_speed_ms);
void updateScrollText(int scroll_speed_ms);
void displayHelp();
void displayWiFiInfo();
void displayADCValues();
void displayChannelInfo();
void handleShortPress();
void handleLongPressStart();
void handleLongPressStop();

void setup()
{
  Serial.begin(115200);

  // Initialize LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.begin();
  lcd.backlight();

  // Create custom icons
  lcd.createChar(0, thingSpeakSymbol);
  lcd.createChar(1, batterySymbol);
  lcd.createChar(2, wifiIcon);
  lcd.createChar(3, thermocoupleIcon);
  lcd.createChar(4, tempIcon);
  lcd.createChar(5, humidityIcon);
  lcd.createChar(6, heatIcon);

  // Initialize sensors and WiFi
  setupWiFi();
  setupSHT4x();
  setupADS7830();
  setupThermocouple();

  // Initialize ThingSpeak
  ThingSpeak.begin(client);

  // Button setup
  button.attachClick(handleShortPress);
  button.attachDoubleClick(handleDoubleClick);
  button.attachLongPressStart(handleLongPressStart);
  button.attachLongPressStop(handleLongPressStop);

  lcd.clear(); // needed for main management

  Serial.println("Setup complete!");

  // fancyCharactersShow();
}

void loop()
{
  unsigned long currentTime = millis();

  // Button handling
  button.tick();

  // Update scrolling text (non-blocking)
  updateScrollText(300);

  if (inMenuMode)
  {
    // If in menu mode, do not update main screen
    return;
  }

  // Main screen updates
  if (currentTime - lastPingUpdate >= 5000)
  { // Update every 5 seconds
    updatePing();
    lastPingUpdate = currentTime;
  }

  if (currentTime - lastSampleTime >= 180)
  { // Sample thermocouple temperature periodically
    avgThermocoupleTemp = readAverageThermocoupleTemp();
    lastSampleTime = currentTime;
  }

  if (currentTime - lastDisplayTime >= 1000)
  { // Read sensors and update LCD periodically
    readSHT4x(shtTemp, shtHumidity);
    readAllChannels(voltages); // Read ADC channels

    int heatLevel = manageHeater(shtHumidity, shtTemp);
    updateLCD(avgThermocoupleTemp, shtTemp, shtHumidity, batteryPercentage, heatLevel);

    lastDisplayTime = currentTime;
  }

  if (currentTime - previousThingSpeakTime >= 15000)
  { // Send data to ThingSpeak periodically
    sendToThingSpeak(avgThermocoupleTemp, shtTemp, shtHumidity,
                     voltages[0], voltages[1], voltages[2], voltages[3]);
    previousThingSpeakTime = currentTime;
  }
}

// WiFi setup function
void setupWiFi()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting to:");
  lcd.setCursor(0, 1);
  lcd.print(ssid);

  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();

  // Try connecting to the primary WiFi for 10 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000)
  {
    delay(500);
    lcd.setCursor(0, 1);
    // lcd.print("Trying primary...");
    Serial.println("Trying primary WiFi...");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connected to:");
    lcd.setCursor(0, 1);
    lcd.print(ssid);
    Serial.println("Connected to primary WiFi.");
    return;
  }

  // If primary fails, try backup WiFi
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting to:");
  lcd.setCursor(0, 1);
  lcd.print(ssid2);

  WiFi.begin(ssid2, password2);
  startAttemptTime = millis();

  // Try connecting to the backup WiFi for 10 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000)
  {
    delay(500);
    lcd.setCursor(0, 1);
    // lcd.print("Trying backup...");
    Serial.println("Trying backup WiFi...");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connected to:");
    lcd.setCursor(0, 1);
    lcd.print(ssid2);
    Serial.println("Connected to backup WiFi.");
  }
  else
  {
    // If both fail
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    Serial.println("Failed to connect to any WiFi.");
  }
  delay(80); // show lcd briefly
}

void updatePing()
{
  IPAddress targetIP(8, 8, 8, 8); // Google's public DNS server

  if (Ping.ping(targetIP, 1))
  {                                  // Send 1 ping requests
    pingResult = Ping.averageTime(); // Get average response time in ms
    Serial.print("Ping successful! Average time: ");
    Serial.println(pingResult);
  }
  else
  {
    pingResult = -1; // Indicate ping failure
    Serial.println("Ping failed!");
  }
}

void sendToThingSpeak(double thermoTemp, float shtTemp, float shtHumidity, float vbat, float vusb, float v3_3, float v5)
{
  Serial.println();
  Serial.println("-Entering sendToThingSpeak-");

  static bool sendToChannel1 = true;

  // NO MORE Display ThingSpeak symbol over wifi symbol untill overwritten
  lcd.setCursor(15, 1);
  lcd.write((byte)0);

  // Set fields for ThingSpeak
  ThingSpeak.setField(1, static_cast<float>(thermoTemp));
  ThingSpeak.setField(2, shtTemp);
  ThingSpeak.setField(3, shtHumidity);
  ThingSpeak.setField(4, vbat);
  ThingSpeak.setField(5, vusb);
  ThingSpeak.setField(6, v3_3);
  ThingSpeak.setField(7, v5);
  ThingSpeak.setField(8, pingResult);

  int x;
  if (sendToChannel1)
  {
    // Write data to ThingSpeak channel 1
    x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    Serial.print("Channel 1 update ");
  }
  else
  {
    // Write data to ThingSpeak channel 2
    x = ThingSpeak.writeFields(myChannelNumber2, myWriteAPIKey2);
    Serial.print("Channel 2 update ");
  }

  if (x == 200)
  {
    Serial.println("successful.");
  }
  else
  {
    Serial.println("failed. HTTP error code " + String(x));
    lcd.setCursor(10, 1);
    lcd.print("!E");
    lcd.print(String(x));
    delay(300);
    lcd.setCursor(14, 1);
    lcd.print("  ");
  }

  // Toggle the channel for next time
  sendToChannel1 = !sendToChannel1;
}

// SHT4x sensor setup function
void setupSHT4x()
{
  if (!sht4.begin())
  {
    Serial.println("Couldn't find SHT4x");
    while (1)
      delay(1);
  }
  Serial.println("SHT4x sensor found");
}

// ADS7830 ADC setup function
void setupADS7830()
{
  if (!ads7830.begin(0x4B))
  {
    Serial.println("Failed to initialize ADS7830!");
    while (1)
      ;
  }
}

double calculateAverageTemperature()
{
  double sum = 0;
  for (int i = 0; i < thermocoupleBuffer.size(); i++)
  {
    sum += thermocoupleBuffer[i];
  }
  return (thermocoupleBuffer.size() > 0) ? (sum / thermocoupleBuffer.size()) : 0.0;
}

void readSHT4x(float &temperature, float &humidity)
{
  sensors_event_t humidity_event, temp_event;
  sht4.getEvent(&humidity_event, &temp_event);
  temperature = temp_event.temperature;
  humidity = humidity_event.relative_humidity;
}

void readAllChannels(float *voltages)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    float sum = 0;
    for (int j = 0; j < 3; j++)
    {
      uint8_t rawValue = ads7830.readADCsingle(i);
      sum += rawValue * (2.5 / 255.0);
      delay(1);
    }
    voltages[i] = sum / 3.0;

    Serial.print("Channel ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(voltages[i], 3);
    Serial.println(" V");
  }

  // Set vbat to pin A3's reading
  voltages[3] = voltages[3];
  Serial.print("vbat ADC's(A3): ");
  Serial.print(voltages[3], 3);
  Serial.println(" V");
}

void handleShortPress()
{
  Serial.println("Short press detected.");
  inMenuMode = true;  // Enter menu mode
  cycleDisplayInfo(); // Display menu items
}
void cycleDisplayInfo()
{
  static int infoState = 0; // Tracks which info screen to display
  lcd.clear();

  switch (infoState)
  {
  case 0:
    displayWiFiInfo();
    break;
  case 1:
    displayADCValues();
    break;
  case 2:
    displayChannelInfo();
    break;
  case 3:
    displayAPIKey();
    break;
  case 4:
    displayAPIKey2();
    break;
  case 5:
    displayHelp();
    break;
  case 6:
    displayURL();
    break;
  case 7:
    displayLoginUsr();
    break;
  case 8:
    displayLoginPwd();
    break;
  case 9:
    infoState = -1; // Reset to -1 so it becomes 0 after increment
    exitMenuMode(); // Exit menu mode after the last item
    return;
  default:
    infoState = -1; // Reset to -1 so it becomes 0 after increment
    exitMenuMode(); // Exit menu mode if an unexpected state is reached
    return;
  }

  infoState = (infoState + 1) % 15; // DONT Cycle through screens
}

void handleLongPressStart()
{
  Serial.println("Long press start detected.");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Unit: ");
  lcd.print(displayFahrenheit ? "F -> C" : "C -> F"); // Show the current toggle direction
  delay(1000);                                        // Display the toggle message for 1 second

  // Toggle the temperature unit
  displayFahrenheit = !displayFahrenheit;

  // Refresh the main screen with the new unit
  updateLCD(avgThermocoupleTemp, shtTemp, shtHumidity, batteryPercentage, manageHeater(shtHumidity, shtTemp));
}

void handleLongPressStop()
{
  Serial.println("Long press stop detected.");
  lcd.clear();
}

void displayThermocoupleTemp(double temperature)
{
  lcd.setCursor(0, 0);
  lcd.print("Thermo Temp: ");
  lcd.print(temperature, 1); // Display temperature with 1 decimal place
  lcd.print("c");
}

void displaySHT4xData(float temperature, float humidity)
{
  lcd.setCursor(0, 1);
  lcd.print("SHT: ");
  lcd.print(temperature, 1); // Display temperature with 1 decimal place
  lcd.print("c ");
  lcd.print(humidity, 1); // Display humidity with 1 decimal place
  lcd.print("%");
}

void displayTemperature(float temperature)
{
  float displayTemp = displayFahrenheit ? (temperature * 9.0 / 5.0 + 32.0) : temperature;
  lcd.print(displayTemp, 2);
  lcd.print(displayFahrenheit ? "F" : "C");
}

void updateLCD(double thermocoupleTemp, float shtTemp, float shtHumidity, int batteryPercentage, int heatLevel)
{
  // lcd.clear();

  // Line 1: Thermocouple Temp and WiFi Icon/Ping Result
  lcd.setCursor(0, 0);
  lcd.write((byte)3); // Thermocouple icon
  displayTemperature(thermocoupleTemp);
  lcd.print(" ");

  lcd.setCursor(13, 0);
  if (pingResult >= 0 && pingResult < 760)
  {
    lcd.print(pingResult); // Display ping result in ms
    lcd.write((byte)2);    // WiFi icon
  }
  else
  {
    lcd.print(">!");    // Display warning if ping fails
    lcd.write((byte)2); // WiFi icon
  }
//next calibrate for 3.3v alwasys
  // Battery icon and percentage
  int batteryPercent = calculateBatteryPercentage(voltages[3]);
  lcd.setCursor(8, 0);
    lcd.write((byte)1); // Battery icon
  if (batteryPercent < 0 || batteryPercent > 100)
  {
    lcd.setCursor(8, 0);
    lcd.print(2* voltages[3], 2);
  }
  else
  {
    lcd.print(batteryPercent, 0);
    lcd.print("%");
    //lcd.print(" ");
    
  }

  // Line 2: SHT4x Temp/Humidity and Battery Percentage/Heat Level/ThingSpeak Icon
  lcd.setCursor(0, 1);
  lcd.write((byte)4); // Temperature icon for SHT4x
  displayTemperature(shtTemp);

  lcd.print(" ");

  lcd.write((byte)5); // Humidity icon for SHT4x
  lcd.print(shtHumidity, 2);

  // Display heat level in the bottom-right corner (column 15)
  lcd.setCursor(15, 1);
  switch (heatLevel)
  {
  case 0:
    lcd.print(" "); // No heat (built-in slash symbol)
    break;
  case 1:
    lcd.write((byte)6); // Low heat (custom icon)
    break;
  case 2:
    lcd.print("M"); // Medium heat (b)
    break;
  case 3:
    lcd.print("H"); // High heat (c)
    break;
  }
}

int calculateBatteryPercentage(float vbat)
{
  float percentage = (2 * vbat - 3.0) / (4.22 - 3.0) * 100;
  return constrain(percentage, -1, 101);
}

void displayWiFiIcon()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    // WiFi is connected; show ping result or WiFi icon
    lcd.setCursor(13, 0);
    if (pingResult >= 0 && pingResult <= 699)
    {
      lcd.print(pingResult); // Show ping result if valid
      lcd.write((byte)2);    // WiFi icon
    }
    else
    {
      lcd.print(">!");
      lcd.write((byte)2); // Warning symbol with WiFi icon
    }
  }
  else
  {
    // WiFi is not connected; show disconnected symbol
    lcd.setCursor(13, 0);
    lcd.print("!");
    lcd.write(0xF8); // Disconnected WiFi icon
  }
}

void displayWiFiInfo()
{
  lcd.setCursor(0, 0);
  lcd.write((byte)2);
  lcd.print("SSID: ");
  lcd.print(WiFi.SSID());
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
}

void displayADCValues()
{
  lcd.setCursor(0, 0);
  lcd.print("ADC: ");
  lcd.print(voltages[0], 2);
  lcd.print("V ");
  lcd.print(voltages[1], 2);
  lcd.print("V");
  lcd.setCursor(0, 1);
  lcd.print("RSSI: ");
  lcd.print(WiFi.RSSI());
  lcd.print("dBm");
}

void displayChannelInfo()
{
  lcd.setCursor(0, 0);
  lcd.print("Wifi-Ch: ");
  lcd.print(WiFi.channel());
  lcd.setCursor(0, 1);
  lcd.print("Thgspk: ");
  lcd.print(myChannelNumber2);
  // Add more channel-related info if needed
}

void displayAPIKey()
{
  lcd.setCursor(0, 0);
  lcd.print("Write API Key:");
  lcd.setCursor(0, 1);
  lcd.print(myWriteAPIKey2); // Display full API key
}

void displayAPIKey2()
{
  lcd.setCursor(0, 0);
  lcd.print("Read API Key:");
  lcd.setCursor(0, 1);
  lcd.print(myReadAPIKey2); // Display full API key
}

void displayHelp2()
{
  lcd.setCursor(0, 0);
  lcd.print("Hold: Unit F<->C");
  lcd.setCursor(0, 1);
  lcd.print("DoubleTap:Slower");
}

void displayHelp()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("For more info:");

  // Example URL or message to scroll
  const char *url2 = "Visit github.com/Fireflaker for Matilda documentation.";

  scrollText(url2, 1, 300); // Scroll on the second row with a delay of 300ms
}

#define STRINGIFY(x) #x // for compile-time const char
#define TOSTRING(x) STRINGIFY(x)
void displayURL()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ThingSpeak URL:");
  const char *url = "thingspeak.com/channels/" STRINGIFY(myChannelNumber);
  scrollText(url, 1, 300); // Scroll on the second row with a delay of 300ms
}

void displayLoginUsr()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Login Email:");

  const char *LoginUsr = "jelac67984@kvegg.com";

  scrollText(LoginUsr, 1, 300); // Scroll on the second row with a delay of 300ms
}

void displayLoginPwd()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Login Password:");

  const char *LoginPwd = "Jelac67984";
  lcd.print(LoginPwd);
}

void exitMenuMode()
{
  inMenuMode = false;          // Exit menu mode
  currentScrollText = nullptr; // Reset scrolling text
  scrollStartTime = 0;         // Reset scroll start time
  lcd.clear();
}

void scrollText(const char *message, int row, int speed)
{
  currentScrollText = message;
  currentScrollRow = row;
  scrollPosition = 0;
  lastScrollTime = millis();
  scrollStartTime = millis();

  lcd.setCursor(0, row);
  lcd.print(message);

  if (strlen(message) > 16)
  {
    updateScrollText(speed);
  }
}

void updateScrollText(int scroll_speed_ms)
{
  if (currentScrollText == nullptr || millis() - scrollStartTime > 30000) // 30 seconds timeout
  {
    currentScrollText = nullptr;
    scrollStartTime = 0;
    return;
  }

  unsigned long currentTime = millis();
  if (currentTime - lastScrollTime >= scroll_speed_ms)
  {
    int textLength = strlen(currentScrollText);
    int screenWidth = 16;
    int totalWidth = textLength + screenWidth;

    lcd.setCursor(0, currentScrollRow);

    for (int j = 0; j < screenWidth; j++)
    {
      int charIndex = (scrollPosition + j) % totalWidth;
      if (charIndex < textLength)
      {
        lcd.write(currentScrollText[charIndex]);
      }
      else
      {
        lcd.write(' ');
      }
    }

    scrollPosition = (scrollPosition + 1) % totalWidth;
    lastScrollTime = currentTime;
  }
}

int8_t manageHeater(float humidity, float temperature)
{
  static unsigned long lastHeaterActivation = 0;
  static unsigned long lastMiniHeat = 0;
  const unsigned long heaterInterval = 60000;    // 1 minute interval
  const unsigned long miniHeatInterval = 900000; // 15 minutes
  static int8_t heatLevel = 0;                   // Heat levels: 0 (off), 1 (low), 2 (medium), 3 (high)

  unsigned long currentTime = millis();

  // Mini heat every 15 minutes
  if (currentTime - lastMiniHeat >= miniHeatInterval)
  {
    sht4.setHeater(SHT4X_LOW_HEATER_100MS);
    lastMiniHeat = currentTime;
    heatLevel = 1;
    return heatLevel;
  }

  // High heat conditions: Humidity >90% and Temperature <65°F
  if (humidity > 90.0 && temperature < 65.0 && currentTime - lastHeaterActivation >= heaterInterval)
  {
    sht4.setHeater(SHT4X_HIGH_HEATER_1S);
    lastHeaterActivation = currentTime;
    heatLevel = 3;
    return heatLevel;
  }

  // Low heat conditions: Humidity >50% and Temperature <60°F
  if (humidity > 50.0 && temperature < 60.0 && currentTime - lastHeaterActivation >= heaterInterval)
  {
    sht4.setHeater(SHT4X_LOW_HEATER_100MS);
    lastHeaterActivation = currentTime;
    heatLevel = 1;
    return heatLevel;
  }

  return heatLevel; // Return current heat level for display purposes
}

void setupThermocouple()
{
  if (!thermocouple.begin())
  {
    Serial.println("ERROR: Thermocouple initialization failed!");
    // while (1)
    //   delay(10); // Halt execution if initialization fails
    lcd.setCursor(0, 0);
    lcd.print(" Thermocouple ");
    lcd.setCursor(0, 1);
    lcd.print(" Init Failed! ");
    delay(1000);
  }
  Serial.println("Thermocouple initialized successfully!");
}

double readAverageThermocoupleTemp()
{
  static double tempSum = 0;
  static int tempCount = 0;

  double tempC = thermocouple.readCelsius();

  if (thermocouple.readError())
  {
    uint8_t errorCode = thermocouple.readError();
    if (errorCode & MAX31855_FAULT_OPEN)
    {
      Serial.println("ERROR: Thermocouple open circuit!");
      lcd.setCursor(1, 0);
      lcd.print("OPEN!");
    }
    else if (errorCode & MAX31855_FAULT_SHORT_VCC)
    {
      Serial.println("ERROR: Thermocouple shorted to VCC!");
      lcd.setCursor(1, 0);
      lcd.print("S-Vc!");
    }
    else if (errorCode & MAX31855_FAULT_SHORT_GND)
    {
      Serial.println("ERROR: Thermocouple shorted to GND!");
      lcd.setCursor(1, 0);
      lcd.print("S-GD!");
    }
    return 0.0; // Return 0 for invalid readings
  }

  tempSum += tempC;
  tempCount++;

  // Calculate average every 10 readings
  if (tempCount >= 10)
  {
    double avgTemp = tempSum / tempCount;
    tempSum = 0;
    tempCount = 0;
    return avgTemp;
  }

  return tempC; // Return current reading until average is calculated
}

void handleDoubleClick()
{
  currentMood++; // Increment mood
  if (currentMood > 3)
  {
    currentMood = 1; // Reset to Mood 1 after Mood 3
  }

  // Update behavior based on the new mood
  switch (currentMood)
  {
  case 1: // Regular mode
    lcd.setCursor(0, 0);
    lcd.print("    - FAST -     ");
    moodMultiplier = 1;
    lcd.backlight(); // Turn on backlight
    Serial.println("Switched to Mood 1: Regular mode");
    inMenuMode = false;
    break;

  case 2: // Slow mode with backlight on
    lcd.setCursor(0, 0);
    lcd.print("    - SLOW -     ");
    moodMultiplier = 3;
    lcd.backlight(); // Ensure backlight is on
    Serial.println("Switched to Mood 2: Slow mode with backlight on");
    inMenuMode = false;
    break;

  case 3: // Slow mode with backlight off
    lcd.setCursor(0, 0);
    lcd.print(" - DARK & SLOW -  ");
    moodMultiplier = 4;
    lcd.noBacklight(); // Turn off backlight
    Serial.println("Switched to Mood 3: Slow mode with backlight off");
    inMenuMode = false;
    break;
  }
  lcd.clear();
}

void fancyCharactersShow()
{

  for (int i = 0; i <= 0xFF; i += 16)
  {
    lcd.clear();

    // Display characters
    for (int j = 0; j < 16; j++)
    {
      lcd.setCursor(j, 0);
      lcd.write(i + j);
    }

    // Bottom row: display incrementing code + fixed digits
    lcd.setCursor(0, 1);
    lcd.print(String(i, HEX));
    lcd.print("23456789012345");

    delay(3000); // Wait 2 seconds before showing the next set
  }
}