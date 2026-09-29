#include <Arduino.h>
#include <BleKeyboard.h>
#include <Wire.h>
#include <esp_log.h>
#include <Adafruit_GFX.h>
#include <Bounce2.h>
#include <Preferences.h>
#include "RoboEyes.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_sleep.h>

// === Display driver abstraction (compile-time selection) ===
// One of: SSD1306 (default), SH1106G (USE_SH1106), SH1107 (USE_SH1107).
#ifdef USE_SH1107
  // SH1107 panels are commonly 64x128 (portrait). The UI is drawn for a 128x64
  // logical landscape, so build the driver for the portrait panel and rotate
  // 90 deg clockwise (Adafruit rotation 1) to map the landscape UI onto it —
  // the same convention the Adafruit SH110X library uses for its 64x128
  // FeatherWing. If your panel is 128x64 landscape instead, drop the rotation
  // and the (64,128) size below.
  #include <Adafruit_SH110X.h>
  #define DISPLAY_WHITE     SH110X_WHITE
  #define DISPLAY_BLACK     SH110X_BLACK
  #define DISPLAY_INVERSE   SH110X_INVERSE
  #define SET_BRIGHTNESS(v) display.setContrast(v)
  #define DISPLAY_BEGIN()   display.begin(SCREEN_ADDRESS)
  #define DISPLAY_ROTATION  1
  #define ALLOC_FAILED_MSG  "OLED allocation failed"
#elif defined(USE_SH1106)
  #include <Adafruit_SH110X.h>
  #define DISPLAY_WHITE     SH110X_WHITE
  #define DISPLAY_BLACK     SH110X_BLACK
  #define DISPLAY_INVERSE   SH110X_INVERSE
  #define SET_BRIGHTNESS(v) display.setContrast(v)
  #define DISPLAY_BEGIN()   display.begin(SCREEN_ADDRESS)
  #define DISPLAY_ROTATION  0
  #define ALLOC_FAILED_MSG  "OLED allocation failed"
#else
  #include <Adafruit_SSD1306.h>
  #define DISPLAY_WHITE     SSD1306_WHITE
  #define DISPLAY_BLACK     SSD1306_BLACK
  #define DISPLAY_INVERSE   SSD1306_INVERSE
  #define SET_BRIGHTNESS(v) do { display.ssd1306_command(SSD1306_SETCONTRAST); \
                                       display.ssd1306_command(v); } while(0)
  #define DISPLAY_BEGIN()   display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)
  #define DISPLAY_ROTATION  0
  #define ALLOC_FAILED_MSG  "SSD1306 allocation failed"
#endif

const char* WIFI_SSID = "AIRCONECT_FIBRA-5865_5G";
const char* WIFI_PASSWORD = "ZnP8A6F53bMV[{I,";
WiFiUDP udp;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

#ifdef USE_SH1107
  Adafruit_SH1107 display(64, 128, &Wire, OLED_RESET); // 64x128 portrait panel
  RoboEyes<Adafruit_SH1107> eyes(display);
#elif defined(USE_SH1106)
  Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  RoboEyes<Adafruit_SH1106G> eyes(display);
#else
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  RoboEyes<Adafruit_SSD1306> eyes(display);
#endif
BleKeyboard bleKeyboard("BindDeck", "Custom", 100);
Preferences preferences;

// Encoder
#ifndef TARGET_ESP32C3
#define ENCODER_CLK 18
#define ENCODER_DT 19
#define ENCODER_SW 5
#define OLED_SDA 21
#define OLED_SCL 22
#define MENU_BTN 4
const int DEFAULT_SWITCH_PINS[8] = {13, 12, 14, 27, 32, 33, 25, 26};
#else
// ESP32-C3 DevKitC-02: GPIO18/19 are UART0 (USB serial), reserved.
// GPIO6-11 are SPI flash, not usable. GPIO0/2/3 are strapping.
#define ENCODER_CLK 20
#define ENCODER_DT 21
#define ENCODER_SW 1
#define OLED_SDA 4
#define OLED_SCL 5
#define MENU_BTN 10
const int DEFAULT_SWITCH_PINS[8] = {12, 13, 14, 15, 16, 17, 2, 3};
#endif

const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
volatile int encoderSteps = 0;
volatile uint8_t old_AB = 0;

void IRAM_ATTR readEncoder() {
  old_AB <<= 2;
  uint8_t current = 0;
  if (digitalRead(ENCODER_CLK)) current |= 0x02;
  if (digitalRead(ENCODER_DT)) current |= 0x01;
  old_AB |= (current & 0x03);
  encoderSteps += enc_states[(old_AB & 0x0f)];
}

// Pins
// Default button GPIOs; overridable at runtime via CFG:PINS: from the PC app.
// (Defined above inside the TARGET_ESP32C3 conditional block.)
int switchPins[8];  // current pin for each button, populated from Preferences
const uint8_t MACRO_KEYS[8] = {KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18, KEY_F19, KEY_F20};

// GPIOs already in use by other peripherals — not available for buttons.
#ifdef TARGET_ESP32C3
const int OCCUPIED_PINS[] = {ENCODER_CLK, ENCODER_DT, ENCODER_SW, MENU_BTN /*menuBtn*/, OLED_SDA /*OLED SDA*/, OLED_SCL /*OLED SCL*/, 18 /*UART0 TXD*/, 19 /*UART0 RXD*/};
#else
const int OCCUPIED_PINS[] = {ENCODER_CLK, ENCODER_DT, ENCODER_SW, MENU_BTN /*menuBtn*/, OLED_SDA /*OLED SDA*/, OLED_SCL /*OLED SCL*/};
#endif
const int NUM_OCCUPIED = sizeof(OCCUPIED_PINS) / sizeof(OCCUPIED_PINS[0]);

bool isValidButtonPin(int pin) {
  if (pin < 0 || pin > 39) return false;            // ESP32 has GPIO 0-39
  for (int i = 0; i < NUM_OCCUPIED; i++) {
    if (pin == OCCUPIED_PINS[i]) return false;
  }
  return true;
}

bool isValidPinSet(const int pins[8]) {
  for (int i = 0; i < 8; i++) {
    if (!isValidButtonPin(pins[i])) return false;
    for (int j = i + 1; j < 8; j++) {
      if (pins[i] == pins[j]) return false;  // no duplicates
    }
  }
  return true;
}

Bounce2::Button switches[8];

// Re-attach every button to its (possibly new) GPIO. Bounce2::attach()
// detaches the old pin and binds the new one, so no restart is needed.
void applyButtonPins() {
  Serial.print("[DBG applyButtonPins] attaching to {");
  for (int i = 0; i < 8; i++) { Serial.print(switchPins[i]); if (i < 7) Serial.print(","); }
  Serial.println("}");
  for (int i = 0; i < 8; i++) {
    switches[i].attach(switchPins[i], INPUT_PULLUP);
    switches[i].interval(25);
    switches[i].setPressedState(LOW);
  }
}
Bounce2::Button menuBtn;
int currentIdleScreen = 0; // 0=Stats, 1=Time, 2=Eyes


enum State {
  STATE_IDLE,
  STATE_ACTION,
  STATE_MENU,
  STATE_EYES
};
State currentState = STATE_IDLE;

// Deep sleep: when neither Bluetooth nor USB is active for the configured
// timeout, put the ESP32 into deep sleep (cuts ~all power). Wakes on the
// menu button (GPIO4) via ext0. Disabled entirely when sleepEnabled is false.
bool sleepEnabled = true;
unsigned long sleepTimeoutMs = 300000; // default 5 min
unsigned long lastConnectionTime = 0; // updated whenever BT or USB is active
bool sleepPending = false; // set true once, so we don't re-arm every loop()

// Telemetry Data
int cpu_temp = 0, cpu_usage = 0, gpu_temp = 0, gpu_usage = 0;

// Action Data
unsigned long actionStartTime = 0;
unsigned long lastEyeTime = 0;
unsigned long eyeStateStartTime = 0;
const unsigned long ACTION_DURATION = 800;
int lastActionKeyIndex = -1; // -1: mute, -2: preview, -3: volume
int visualVolume = 50;

// Config Data
int brightness = 255;
int animMode = 0; // 0: Circles, 1: Flash, 2: Minimal
int encMode = 0;  // 0: Volume, 1: Vertical Arrows, 2: Horizontal Arrows
int keyAnims[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // -1 means use global animMode
String keyTexts[8] = {"", "", "", "", "", "", "", ""};
int previewAnimOverride = -1;
String customMsg = "";

void saveConfig() {
  preferences.begin("binddeck", false);
  preferences.putInt("animMode", animMode);
  preferences.putInt("encMode", encMode);
  preferences.putInt("brightness", brightness);
  for(int i=0; i<8; i++) {
    char key[10];
    sprintf(key, "kbanim%d", i);
    preferences.putInt(key, keyAnims[i]);
  }
  for(int i=0; i<8; i++) {
    char key[10];
    sprintf(key, "pin%d", i);
    preferences.putInt(key, switchPins[i]);
  }
  preferences.putBool("sleepEnabled", sleepEnabled);
  preferences.putULong("sleepTimeoutMs", sleepTimeoutMs);
  preferences.end();
}


#include <time.h>

void drawTimeScreen() {
  display.clearDisplay();
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50)) {
    display.setCursor(20, 25);
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    display.print("Waiting for Time...");
    display.display();
    return;
  }
  
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  
  display.setTextSize(3);
  display.setTextColor(DISPLAY_WHITE);
  
  // Center time
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeStringBuff, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, (64 - h) / 2);
  display.print(timeStringBuff);
  
  display.display();
}

// Forward declarations
void drawIdle();
void drawAction();
void drawMenu();
void drawUpdateScreen();
void drawMicIcon(int x, int y, uint16_t color, uint16_t bg);
void handleEncoderAction(bool forward);

void loadConfig() {
  // On first boot (or after erase_flash) the NVS namespace doesn't exist
  // yet. The Arduino Preferences library doesn't auto-create it, so
  // nvs_open returns NOT_FOUND and the chip watchdogs. Open in read-write
  // mode so the library creates the namespace on first use.
  if (!preferences.begin("binddeck", false)) {
    preferences.end();
    log_w("[DBG load_config] NVS begin failed, using defaults");
    animMode = 0; encMode = 0; brightness = 255;
    for (int i = 0; i < 8; i++) { keyAnims[i] = -1; keyTexts[i] = ""; }
    for (int i = 0; i < 8; i++) switchPins[i] = DEFAULT_SWITCH_PINS[i];
    return;
  }
  animMode = preferences.getInt("animMode", 0);
  encMode = preferences.getInt("encMode", 0);
  brightness = preferences.getInt("brightness", 255);
  for(int i=0; i<8; i++) {
    char key[10];
    sprintf(key, "kbanim%d", i);
    keyAnims[i] = preferences.getInt(key, -1);

    sprintf(key, "kbtxt%d", i);
    keyTexts[i] = preferences.getString(key, "");
  }
  if (encMode < 0 || encMode > 5) encMode = 0;

  // Button GPIOs: read persisted values, fall back to defaults if invalid
  // (e.g. after a bad CFG:PINS: or a Preferences wipe). Keep the NVS handle
  // open — calling preferences.end() here and then keep reading on the closed
  // handle is a use-after-close (the reads silently return defaults, and the
  // double end() is just wrong). Close ONCE at the very end.
  int loaded[8];
  bool anyInvalid = false;
  for(int i=0; i<8; i++) {
    char key[10];
    sprintf(key, "pin%d", i);
    loaded[i] = preferences.getInt(key, DEFAULT_SWITCH_PINS[i]);
    if (!isValidButtonPin(loaded[i])) anyInvalid = true;
  }
  for (int i = 0; i < 8; i++) {
    for (int j = i + 1; j < 8; j++) {
      if (loaded[i] == loaded[j]) { anyInvalid = true; break; }
    }
  }
  if (anyInvalid) {
    for (int i = 0; i < 8; i++) switchPins[i] = DEFAULT_SWITCH_PINS[i];
  } else {
    for (int i = 0; i < 8; i++) switchPins[i] = loaded[i];
  }
  sleepEnabled = preferences.getBool("sleepEnabled", true);
  sleepTimeoutMs = preferences.getULong("sleepTimeoutMs", 300000);
  if (sleepTimeoutMs < 60000) sleepTimeoutMs = 300000;
  preferences.end();
  Serial.print("[DBG load_config] switchPins={");
  for (int i = 0; i < 8; i++) { Serial.print(switchPins[i]); if (i < 7) Serial.print(","); }
  Serial.println("}");
}

// Track last time we received data (Serial or WiFi)
unsigned long lastDataTime = 0;
IPAddress pcIP;
bool pcIpSet = false;

void sendDataToPC(String data) {
  Serial.println(data);
  if (WiFi.status() == WL_CONNECTED) {
     if (pcIpSet) {
       udp.beginPacket(pcIP, 4211);
     } else {
       udp.beginPacket(IPAddress(255, 255, 255, 255), 4211);
     }
     udp.print(data);
     udp.endPacket();
  }
}

void processCommand(String data) {
    lastDataTime = millis();
    // Config commands from PC App (e.g. CFG:ANIM:1)
    if (data.startsWith("CFG:ANIM:")) {
      animMode = data.substring(9).toInt();
      saveConfig();
    } else if (data.startsWith("CFG:ENC:")) {
      encMode = data.substring(8).toInt();
      saveConfig();
    } else if (data.startsWith("CFG:BRIGHT:")) {
      brightness = data.substring(11).toInt();
      SET_BRIGHTNESS(brightness);
      saveConfig();
    } else if (data.startsWith("CFG:SLEEP:")) {
      // CFG:SLEEP:<enabled>,<timeout_minutes>
      String payload = data.substring(10);
      int comma = payload.indexOf(',');
      sleepEnabled = (comma != -1) ? payload.substring(0, comma).toInt() : 1;
      sleepTimeoutMs = (comma != -1) ? payload.substring(comma + 1).toInt() * 60000UL : 300000UL;
      if (sleepTimeoutMs < 60000) sleepTimeoutMs = 60000; // min 1 min
      saveConfig();
      Serial.print("[DBG CFG:SLEEP] enabled="); Serial.print(sleepEnabled);
      Serial.print(" timeout_ms="); Serial.println(sleepTimeoutMs);
    } else if (data.startsWith("CFG:KB_ANIM:")) {
      String payload = data.substring(12);
      for(int i=0; i<8; i++) {
        int comma = payload.indexOf(',');
        if (comma != -1) {
          keyAnims[i] = payload.substring(0, comma).toInt();
          payload = payload.substring(comma+1);
        } else {
          keyAnims[i] = payload.toInt();
        }
      }
      saveConfig();
    } else if (data.startsWith("CFG:TXT:")) {
      int idx = data.substring(8, 9).toInt();
      String txt = data.substring(10);
      if (idx >= 0 && idx < 8) {
        keyTexts[idx] = txt;
        char pk[10];
        sprintf(pk, "kbtxt%d", idx);
        preferences.begin("binddeck", false);
        preferences.putString(pk, txt);
        preferences.end();
      }
    } else if (data.startsWith("CFG:PINS:")) {
    // CFG:PINS:13,12,14,27,32,33,25,26  — one GPIO per button, comma separated
    String payload = data.substring(9);
    int newPins[8];
    bool ok = true;
    for (int i = 0; i < 8; i++) {
      int comma = payload.indexOf(',');
      String token = (comma == -1) ? payload : payload.substring(0, comma);
      token.trim();
      newPins[i] = token.toInt();
      if (newPins[i] <= 0 && token.length() > 0 && token[0] != '0') ok = false;
      if (comma == -1) break;
      payload = payload.substring(comma + 1);
    }
    Serial.print("[DBG CFG:PINS] recv payload='");
    Serial.print(data.substring(9));
    Serial.print("' parsed={");
    for (int i = 0; i < 8; i++) { Serial.print(newPins[i]); if (i < 7) Serial.print(","); }
    Serial.print("} ok="); Serial.print(ok);
    Serial.print(" valid="); Serial.print(isValidPinSet(newPins));
    Serial.println();
    if (ok && isValidPinSet(newPins)) {
      for (int i = 0; i < 8; i++) switchPins[i] = newPins[i];
      applyButtonPins();
      saveConfig();
      Serial.print("[DBG CFG:PINS] APPLIED switchPins={");
      for (int i = 0; i < 8; i++) { Serial.print(switchPins[i]); if (i < 7) Serial.print(","); }
      Serial.println("}");
    } else {
      log_w("[DBG CFG:PINS] REJECTED — keeping previous switchPins");
    }
    // Invalid payloads are silently ignored — device keeps its last valid config.
} else if (data.startsWith("CFG:WIFI:")) {
      String payload = data.substring(9);
      int pipeIdx = payload.indexOf('|');
      if (pipeIdx != -1) {
        String ssid = payload.substring(0, pipeIdx);
        String pwd = payload.substring(pipeIdx + 1);
        ssid.trim();
        pwd.trim();
        preferences.begin("binddeck", false);
        preferences.putString("wifiSSID", ssid);
        preferences.putString("wifiPwd", pwd);
        preferences.end();
        WiFi.disconnect(true, true);
        delay(100);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pwd.c_str());
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
      }
    } else if (data.startsWith("CMD:GET_WIFI")) {
      if (WiFi.status() == WL_CONNECTED) {
        preferences.begin("binddeck", true);
        String ssid = preferences.getString("wifiSSID", WIFI_SSID);
        preferences.end();
        Serial.print("WIFI_INFO:");
        Serial.print(ssid);
        Serial.print(",");
        Serial.println(WiFi.localIP().toString());
      } else {
        Serial.println("WIFI_INFO:DISCONNECTED,0.0.0.0");
      }
    } else if (data.startsWith("CMD:PREVIEW:")) {
      previewAnimOverride = data.substring(12).toInt();
      lastActionKeyIndex = -2;
      currentState = STATE_ACTION;
      actionStartTime = millis();
    } else if (data.startsWith("CMD:UPDATE")) {
      drawUpdateScreen();
    } else if (data.startsWith("CMD:SIMULATE:")) {
      lastActionKeyIndex = data.substring(13).toInt();
      currentState = STATE_ACTION;
      actionStartTime = millis();
    } else if (data.startsWith("CMD:MSG:")) {
      customMsg = data.substring(8);
      lastActionKeyIndex = -4;
      currentState = STATE_ACTION;
      actionStartTime = millis();
    } else if (data.indexOf("C:") != -1 && data.indexOf("G:") != -1) {
      sscanf(data.c_str(), "C:%d,U:%d,G:%d,V:%d", &cpu_temp, &cpu_usage, &gpu_temp, &gpu_usage);
    }
}

unsigned long lastSerialTime = 0;

void parseSerialData() {
  if (Serial.available()) {
    lastSerialTime = millis();
    String data = Serial.readStringUntil('\n');
    data.trim();
    if (data.length() > 0) {
      processCommand(data);
    }
  }
}

// Batery config
#ifndef TARGET_ESP32C3
const int BATTERY_PIN = 35; // Analog pin to measure voltage (ESP32 classic)
#else
const int BATTERY_PIN = -1; // ESP32-C3 has no GPIO35; battery sense not wired
#endif

bool isBatteryCharging = false;

int getBatteryPercentage() {
  if (BATTERY_PIN < 0) { isBatteryCharging = false; return -1; }
  static float filteredRaw = -1;
  static unsigned long lastBatteryUpdate = 0;
  
  static float smoothForCharging = -1;
  static float lastChargingCheckVal = 0;
  static unsigned long lastChargingCheckTime = 0;
  
  // With a voltage divider (100k + 100k), the pin voltage is half of the battery.
  // Battery max = 4.2V -> Pin = 2.1V.
  // On the ESP32, 2.1V is approx 2600-2800 in the 12-bit ADC (0-4095).
  // These values must be adjusted based on the real divider you use.
  int raw = analogRead(BATTERY_PIN);
  
  // If the pin is not connected to the divider, it will read a very low value (noise or 0)
  // A depleted battery (3.0V) would still give > 1800. So if it is less than 1000, 
  // we know for sure that there is no measurement hardware connected.
  if (raw < 1000) {
    filteredRaw = -1;
    isBatteryCharging = false;
    return -1; // -1 means "Battery not detected"
  }
  
  if (filteredRaw == -1) {
    filteredRaw = raw; // Initialize on first valid reading
    smoothForCharging = raw;
    lastChargingCheckVal = raw;
  }
  
  // Fast EMA to detect sudden voltage rises (charger)
  smoothForCharging = (0.2 * raw) + (0.8 * smoothForCharging);
  
  // Check for jumps every 2 seconds
  if (millis() - lastChargingCheckTime > 2000) {
    if (smoothForCharging - lastChargingCheckVal > 60) {
      isBatteryCharging = true; // Sudden voltage rise -> Charging
    } else if (lastChargingCheckVal - smoothForCharging > 60) {
      isBatteryCharging = false; // Sudden drop -> Disconnected
    }
    lastChargingCheckVal = smoothForCharging;
    lastChargingCheckTime = millis();
  }
  
  // Exponential moving average (EMA) filter to smooth the reading
  if (millis() - lastBatteryUpdate > 50) {
    filteredRaw = (0.05 * raw) + (0.95 * filteredRaw);
    lastBatteryUpdate = millis();
  }
  
  // Assuming reading from 0 to 4095. For 4.2V (100%), we read approx 2600. For 3.3V (0%), we read approx 2050.
  // IMPORTANT: Adjust these values experimentally with a multimeter.
  int minRaw = 2050; // 3.3V
  int maxRaw = 2600; // 4.2V
  
  int pct = map((int)filteredRaw, minRaw, maxRaw, 0, 100);
  return constrain(pct, 0, 100);
}

void drawBatteryIcon(int x, int y, int percentage, bool isCharging) {
  // Draw percentage text to the left of the icon
  display.setCursor(x, y + 1);
  display.print(percentage);
  display.print("%");
  
  // Icon offset
  int iconX = x + 25; // 4 chars * 6px = 24px + 1px gap
  
  // Draw battery outline
  display.drawRect(iconX, y, 20, 10, DISPLAY_WHITE);
  display.fillRect(iconX + 20, y + 2, 2, 6, DISPLAY_WHITE); // Battery tip
  
  // Draw fill
  int fillWidth = map(percentage, 0, 100, 0, 16);
  if (fillWidth > 0) {
    display.fillRect(iconX + 2, y + 2, fillWidth, 6, DISPLAY_WHITE);
  }
  
  if (isCharging) {
    // Draw a lightning bolt in the center. We use INVERSE so it looks white on black background, or black on the fill bar.
    int lx = iconX + 8;
    int ly = y + 1;
    display.drawLine(lx + 3, ly + 1, lx + 1, ly + 4, DISPLAY_INVERSE);
    display.drawLine(lx + 1, ly + 4, lx + 4, ly + 4, DISPLAY_INVERSE);
    display.drawLine(lx + 4, ly + 4, lx + 2, ly + 7, DISPLAY_INVERSE);
  }
}

void drawWiFiIcon(int x, int y) {
  // Simple Wi-Fi waves
  display.drawPixel(x+4, y+6, DISPLAY_WHITE);
  display.drawLine(x+2, y+4, x+6, y+4, DISPLAY_WHITE);
  display.drawLine(x, y+2, x+8, y+2, DISPLAY_WHITE);
}

void drawBTIcon(int x, int y) {
  display.drawLine(x+3, y, x+3, y+8, DISPLAY_WHITE);
  display.drawLine(x+3, y, x+5, y+2, DISPLAY_WHITE);
  display.drawLine(x+5, y+2, x+1, y+6, DISPLAY_WHITE);
  display.drawLine(x+3, y+8, x+5, y+6, DISPLAY_WHITE);
  display.drawLine(x+5, y+6, x+1, y+2, DISPLAY_WHITE);
}

void drawIdle() {
  display.clearDisplay();

  // If we haven't received data (neither USB nor WiFi) in 3 seconds,
  // we assume there is no PC connected or there are connection problems
  bool isDisconnected = (millis() - lastDataTime > 3000);

  if (cpu_temp > 85 || gpu_temp > 85) {
    if ((millis() / 500) % 2 == 0) { // Blink every 500ms
      display.fillRect(0, 0, 128, 64, DISPLAY_WHITE);
      display.setTextColor(DISPLAY_BLACK);
      display.setTextSize(2);
      display.setCursor(20, 15);
      display.println("ALERT!");
      display.setTextSize(1);
      display.setCursor(15, 40);
      if (cpu_temp > 85) display.print("HIGH CPU TEMP: "); else display.print("HIGH GPU TEMP: ");
      display.println(cpu_temp > 85 ? cpu_temp : gpu_temp);
      display.display();
      return;
    }
  }
  
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  
  // Header
  display.setCursor(28, 0);
  if (isDisconnected) {
      display.setCursor(20, 0);
      display.print("NO SIGNAL");
  } else {
      display.print("PC STATS");
  }
  
  // Connection Icons (Bottom Right)
  if (WiFi.status() == WL_CONNECTED) {
    drawWiFiIcon(116, 54);
  } else {
    drawBTIcon(120, 54);
  }
  
  int batPct = getBatteryPercentage();
  bool isConnectedByCable = (millis() - lastSerialTime < 3000);
  
  if (batPct != -1) {
      // Show the battery ALWAYS when we are wireless (!isConnectedByCable)
      // Or ALWAYS when it is charging (isBatteryCharging)
      if (!isConnectedByCable || isBatteryCharging) {
          drawBatteryIcon(80, 0, batPct, isBatteryCharging);
      }
      
      if (bleKeyboard.isConnected()) {
          bleKeyboard.setBatteryLevel(batPct);
      }
  }
  
  // Data
  display.setCursor(0, 15);
  display.print("CPU Temp: "); display.print(cpu_temp); display.println(" C");
  display.setCursor(0, 25);
  display.print("CPU Load: "); display.print(cpu_usage); display.println(" %");

  display.setCursor(0, 40);
  display.print("GPU Temp: "); display.print(gpu_temp); display.println(" C");
  display.setCursor(0, 50);
  display.print("GPU Load: "); display.print(gpu_usage); display.println(" %");

  display.display();
}

void drawUpdateScreen() {
  int frame = 0;
  while(true) {
    display.clearDisplay();
    
    // Texts
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    
    display.setCursor(20, 35);
    display.println("Updating...");
    
    display.setCursor(16, 45);
    display.println("Do not disconnect");
    
    display.setCursor(31, 55);
    display.println("the device");
    
    // Spinner
    int cx = 64;
    int cy = 16;
    int r = 10;
    
    for (int i = 0; i < 8; i++) {
      float angle = (i * 45) * 3.14159 / 180.0;
      int x = cx + cos(angle) * r;
      int y = cy + sin(angle) * r;
      
      // Calculate dot size based on frame to create rotation effect
      int dotDistance = (i - (frame % 8) + 8) % 8;
      
      if (dotDistance < 2) {
        display.fillCircle(x, y, 2, DISPLAY_WHITE); // Big dot
      } else if (dotDistance < 4) {
        display.drawCircle(x, y, 1, DISPLAY_WHITE); // Medium dot
      } else {
        display.drawPixel(x, y, DISPLAY_WHITE); // Small dot
      }
    }
    
    display.display();
    frame++;
    delay(100);
  }
}

void drawMicIcon(int x, int y, uint16_t color, uint16_t bg) {
  int bw = 8;
  int bh = 14;
  
  // Clear a box behind the mic
  display.fillRoundRect(x - bw/2 - 6, y - bh/2 - 4, bw + 12, bh + 14, 2, bg);

  // Cup (drawn first)
  display.drawRoundRect(x - bw/2 - 4, y - bh/2 + 2, bw + 8, bh, 4, color);
  // Erase top half of cup
  display.fillRect(x - bw/2 - 5, y - bh/2 - 2, bw + 10, bh/2 + 4, bg); 

  // Mic body (drawn over the cup erase area)
  display.fillRoundRect(x - bw/2, y - bh/2, bw, bh, 3, color);
  
  // Stand
  display.drawLine(x, y + bh/2 + 2, x, y + bh/2 + 6, color);
  display.drawLine(x - 5, y + bh/2 + 6, x + 5, y + bh/2 + 6, color);
  
  // Diagonal slash
  display.drawLine(x - 12, y - 10, x + 12, y + 14, color);
  display.drawLine(x - 12, y - 9, x + 11, y + 14, color);
}

void drawAction() {
  display.clearDisplay();
  unsigned long elapsed = millis() - actionStartTime;
  
  if (lastActionKeyIndex == -3) {
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    if (encMode == 0 || encMode == 5) {
      display.setCursor(encMode == 0 ? 46 : 40, 15);
      display.print(encMode == 0 ? "VOLUME" : "APP VOL");
      display.drawRect(14, 35, 100, 10, DISPLAY_WHITE);
      display.fillRect(14, 35, visualVolume, 10, DISPLAY_WHITE);
    } else if (encMode == 1) {
      display.setCursor(52, 28);
      display.print("ZOOM");
    } else if (encMode == 2) {
      display.setCursor(52, 28);
      display.print("TABS");
    } else if (encMode == 3) {
      display.setCursor(38, 28);
      display.print("UNDO/REDO");
    }

    display.display();
    if (elapsed > 1000) {
      currentState = STATE_IDLE;
      lastEyeTime = millis();
    }
    return;
  }
  
  int currentAnim = animMode;
  if (lastActionKeyIndex == -2) {
    currentAnim = previewAnimOverride;
  } else if (lastActionKeyIndex >= 0 && lastActionKeyIndex < 8) {
    if (keyAnims[lastActionKeyIndex] != -1) {
      currentAnim = keyAnims[lastActionKeyIndex];
    }
  }
  
  String dispText = "";
  if (lastActionKeyIndex == -2) dispText = "PREVIEW";
  else if (lastActionKeyIndex >= 0 && lastActionKeyIndex < 8) {
    if (keyTexts[lastActionKeyIndex].length() > 0) dispText = keyTexts[lastActionKeyIndex];
    else {
      dispText = "F";
      dispText += (13 + lastActionKeyIndex);
      dispText += " HIT";
    }
  }

  auto printCentered = [](String text, int y, uint16_t c) {
    int sz = (text.length() > 10) ? 1 : 2;
    display.setTextSize(sz);
    display.setTextColor(c);
    int cw = sz * 6;
    int tw = text.length() * cw;
    int tx = (SCREEN_WIDTH - tw) / 2;
    if (tx < 0) tx = 0;
    int ty = y + (16 - (sz * 8)) / 2;
    display.setCursor(tx, ty);
    display.print(text);
  };

  if (lastActionKeyIndex == -4) {
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    printCentered("AUDIO", 15, DISPLAY_WHITE);
    printCentered(customMsg, 35, DISPLAY_WHITE);
    display.display();
    if (elapsed > 1500) {
      currentState = STATE_IDLE;
      lastEyeTime = millis();
    }
    return;
  }
  
  if (currentAnim == 0) {
    int maxRadius = 40;
    int radius = (elapsed * maxRadius) / ACTION_DURATION;
    display.drawCircle(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, radius, DISPLAY_WHITE);
    if (radius > 5) display.drawCircle(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, radius - 5, DISPLAY_WHITE);
    
    if (lastActionKeyIndex == -1) {
      drawMicIcon(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, DISPLAY_WHITE, DISPLAY_BLACK);
    } else {
      printCentered(dispText, 25, DISPLAY_WHITE);
    }
    
  } else if (currentAnim == 1) {
    uint16_t color = ((elapsed / 100) % 2 == 0) ? DISPLAY_BLACK : DISPLAY_WHITE;
    uint16_t bg = ((elapsed / 100) % 2 == 0) ? DISPLAY_WHITE : DISPLAY_BLACK;
    
    if (bg == DISPLAY_WHITE) {
      display.fillRect(10, 15, 108, 34, DISPLAY_WHITE);
    } else {
      display.drawRect(10, 15, 108, 34, DISPLAY_WHITE);
    }
    
    if (lastActionKeyIndex == -1) {
      drawMicIcon(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, color, bg);
    } else {
      printCentered(dispText, 25, color);
    }
    
  } else if (currentAnim == 2) {
    if (lastActionKeyIndex == -1) {
      drawMicIcon(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, DISPLAY_WHITE, DISPLAY_BLACK);
    } else {
      printCentered(dispText, 25, DISPLAY_WHITE);
    }
  } else if (currentAnim == 3) {
    int maxRadius = 40;
    int radius = (elapsed * maxRadius) / ACTION_DURATION;
    display.drawCircle(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, radius, DISPLAY_WHITE);
    if (radius > 5) display.drawCircle(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, radius - 5, DISPLAY_WHITE);
    drawMicIcon(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, DISPLAY_WHITE, DISPLAY_BLACK);
  }

  display.display();
  
  unsigned long duration = (currentAnim == 2) ? 800 : ACTION_DURATION;
  if (elapsed > duration) {
    currentState = STATE_IDLE;
    lastEyeTime = millis();
  }
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(0, 0);
  display.println("-- SETTINGS MENU --");
  display.setCursor(0, 20);
  display.println("Set Brightness:");
  
  display.drawRect(10, 40, 100, 10, DISPLAY_WHITE);
  int w = map(brightness, 0, 255, 0, 100);
  display.fillRect(10, 40, w, 10, DISPLAY_WHITE);
  display.display();
}

void handleEncoderAction(bool forward) {
  if (encMode == 0) { // Volume
    if (bleKeyboard.isConnected()) bleKeyboard.write(forward ? KEY_MEDIA_VOLUME_UP : KEY_MEDIA_VOLUME_DOWN);
    sendDataToPC(forward ? "ENC:VUP" : "ENC:VDN");
  } else if (encMode == 1) { // Zoom (Ctrl + / Ctrl -)
    if (bleKeyboard.isConnected()) {
      bleKeyboard.press(KEY_LEFT_CTRL);
      bleKeyboard.write(forward ? '+' : '-');
      bleKeyboard.releaseAll();
    }
    sendDataToPC(forward ? "ENC:ZIN" : "ENC:ZOUT");
  } else if (encMode == 2) { // Browser Tabs
    if (bleKeyboard.isConnected()) {
      bleKeyboard.press(KEY_LEFT_CTRL);
      if (!forward) bleKeyboard.press(KEY_LEFT_SHIFT);
      bleKeyboard.write(KEY_TAB);
      bleKeyboard.releaseAll();
    }
    sendDataToPC(forward ? "ENC:TFWD" : "ENC:TBCK");
  } else if (encMode == 3) { // Undo / Redo
    if (bleKeyboard.isConnected()) {
      bleKeyboard.press(KEY_LEFT_CTRL);
      bleKeyboard.write(forward ? 'y' : 'z');
      bleKeyboard.releaseAll();
    }
    sendDataToPC(forward ? "ENC:REDO" : "ENC:UNDO");
  } else if (encMode == 5) { // App Volume
    if (bleKeyboard.isConnected()) bleKeyboard.write(forward ? KEY_F24 : KEY_F23);
    sendDataToPC(forward ? "ENC:APPVUP" : "ENC:APPVDN");
  }
}

void setupWiFi() {
  preferences.begin("binddeck", true);
  String ssid = preferences.getString("wifiSSID", WIFI_SSID);
  String pwd = preferences.getString("wifiPwd", WIFI_PASSWORD);
  preferences.end();

  // On first boot the NVS keys don't exist yet; getString returns the
  // default, but if it ever comes back empty fall back to the hardcoded
  // credentials so WiFi.begin() doesn't get a NULL/empty SSID.
  if (ssid.length() == 0) ssid = WIFI_SSID;
  if (pwd.length() == 0) pwd = WIFI_PASSWORD;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pwd.c_str());
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
}

void loopWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    static bool udpStarted = false;
    if (!udpStarted) {
      udp.begin(4210);
      udpStarted = true;
    }
    
    int packetSize = udp.parsePacket();
    if (packetSize) {
      pcIP = udp.remoteIP();
      pcIpSet = true;
      char packetBuffer[255];
      int len = udp.read(packetBuffer, 255);
      if (len > 0) {
        packetBuffer[len] = 0;
        String data = String(packetBuffer);
        data.trim();
        if (data.length() > 0) {
           processCommand(data);
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);
  log_w("[DBG setup] serial ok"); delay(30);
  loadConfig();  // populates switchPins[] from Preferences (or defaults)
  log_w("[DBG setup] loadConfig ok"); delay(30);

  setupWiFi();
  log_w("[DBG setup] wifi ok"); delay(30);

  // CRITICAL for ESP32-C3: the devkit's default I2C pins are GPIO8/9, but this
  // board wires the OLED to OLED_SDA/OLED_SCL (4/5 on C3). Passing them
  // explicitly is required — otherwise Wire talks to the wrong bus, the display
  // never ACKs, and every I2C write blocks until the peripheral timeout, which
  // starves the task watchdog (rst:0x8 TG1WDT_SYS_RST). Bound the timeout so a
  // missing display can never hang the firmware.
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setTimeout(50);
  if(!DISPLAY_BEGIN()) {
    Serial.println(F(ALLOC_FAILED_MSG));
  }
  log_w("[DBG setup] display ok"); delay(30);
  display.setRotation(DISPLAY_ROTATION);

  SET_BRIGHTNESS(brightness);
  display.clearDisplay();
  display.display();
  
  eyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 30);
  eyes.setAutoblinker(true, 2, 2);
  eyes.setIdleMode(true, 1, 2);
  log_w("[DBG setup] eyes ok"); delay(30);
  
  log_w("[DBG setup] ble begin..."); delay(30);
  bleKeyboard.begin();
  log_w("[DBG setup] ble ok"); delay(30);
  
  // Encoder setup
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), readEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT), readEncoder, CHANGE);
  
applyButtonPins();
  log_w("[DBG setup] buttons ok"); delay(30);

  menuBtn.attach(MENU_BTN, INPUT_PULLUP);
  menuBtn.interval(25);
  menuBtn.setPressedState(LOW);

  log_w("[DBG setup] DONE - entering loop"); delay(30);

#ifdef TARGET_ESP32C3
  // Deep sleep resets the chip, so setup() runs on every wake. Re-arm the
  // "go back to sleep if nothing happened" check so the C3 polls buttons in
  // short windows instead of staying awake for the full sleep timeout.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    sleepPending = true;
  }
#endif
}

void loop() {
  static bool _loopStarted = false;
  if (!_loopStarted) { _loopStarted = true; log_w("[DBG loop] started"); delay(30); }
  for(int i = 0; i < 8; i++) switches[i].update();
  menuBtn.update();
  
  if (menuBtn.pressed()) {
    currentIdleScreen = (currentIdleScreen + 1) % 3;
    if (currentIdleScreen == 2) {
       currentState = STATE_EYES;
       eyes.setMood(random(0, 4));
       eyeStateStartTime = millis();
    } else {
       currentState = STATE_IDLE;
    }
  }

  
  if (currentState == STATE_IDLE || currentState == STATE_ACTION || currentState == STATE_EYES) {
    parseSerialData();
    loopWiFi();
    
    for(int i = 0; i < 8; i++) {
      if(switches[i].pressed()) {
        if(bleKeyboard.isConnected()) {
          bleKeyboard.press(MACRO_KEYS[i]);
          delay(10);
          bleKeyboard.releaseAll();
        }
        sendDataToPC("BTN:" + String(i));
        lastActionKeyIndex = i;
        currentState = STATE_ACTION;
        actionStartTime = millis();
      }
    }
    
    // --- Encoder Button (Programmable) ---
    static unsigned long lastEncoderButtonPress = 0;
    if (digitalRead(ENCODER_SW) == LOW) {
      if (millis() - lastEncoderButtonPress > 200) {
        if (bleKeyboard.isConnected()) {
          bleKeyboard.press(KEY_F21);
          delay(10);
          bleKeyboard.releaseAll();
        }
        sendDataToPC("BTN:8");
        lastActionKeyIndex = 8;
        currentState = STATE_ACTION;
        actionStartTime = millis();
        lastEncoderButtonPress = millis();
      }
    }

    // --- Rotary Encoder ---
    static int lastEncoderSteps = 0;
    if (encoderSteps / 4 != lastEncoderSteps / 4) {
      noInterrupts();
      int currentSteps = encoderSteps;
      interrupts();
      
      int diff = (currentSteps / 4) - (lastEncoderSteps / 4);
      if (diff > 3) diff = 3;
      if (diff < -3) diff = -3;
      
      for(int i = 0; i < abs(diff); i++) {
        bool forward = (diff > 0);
        handleEncoderAction(forward);
        delay(15);
        
        if (encMode == 0 || encMode == 5) {
           visualVolume += forward ? 4 : -4;
           if (visualVolume < 0) visualVolume = 0;
           if (visualVolume > 100) visualVolume = 100;
        }
      }
      
      lastEncoderSteps = currentSteps;
      lastActionKeyIndex = -3;
      currentState = STATE_ACTION;
      actionStartTime = millis();
    }
    
    if (currentState == STATE_IDLE) {
      if (currentIdleScreen == 0) {
        drawIdle(); // PC Stats
      } else if (currentIdleScreen == 1) {
        drawTimeScreen();
      }
    } else if (currentState == STATE_ACTION) {
      drawAction();
    } else if (currentState == STATE_EYES) {
      eyes.update();
      // Change mood randomly
      if (millis() - eyeStateStartTime > 4000) {
        eyes.setMood(random(0, 4));
        eyeStateStartTime = millis();
      }
    }

    // --- Deep sleep: when no BT/USB activity for SLEEP_TIMEOUT, put ESP32
    // --- into deep sleep. Wakes on any button press via ext1 wakeup. ---
    bool hasBT = bleKeyboard.isConnected();
    bool hasUSB = (millis() - lastSerialTime < 3000);
    if (hasBT || hasUSB) {
      lastConnectionTime = millis();
    }

    if (!sleepPending && sleepEnabled && (millis() - lastConnectionTime >= sleepTimeoutMs)) {
      sleepPending = true;
      display.clearDisplay();
      display.display();
      SET_BRIGHTNESS(0);
#ifdef TARGET_ESP32C3
      // ESP32-C3: ext0/ext1 not available for deep sleep (SOC_PM_SUPPORT_EXT_WAKEUP
      // undefined). Use timer wakeup: wake every 5 s, loop() checks buttons and
      // goes back to sleep if nothing pressed. Less power cut than ext0 but
      // the only deep-sleep GPIO-free option on C3.
      esp_sleep_enable_timer_wakeup(5000000); // 5 s
      log_w("[DBG sleep] entering deep sleep (timer wake 5s)...");
#else
      // ESP32 (original): ext1 only supports ALL_LOW or ANY_HIGH. Buttons are
      // normally HIGH (pull-up) and go LOW when pressed, so we use ext0 on the
      // menu button as the primary wake source.
      pinMode(MENU_BTN, INPUT_PULLUP);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)MENU_BTN, 0);
      log_w("[DBG sleep] entering deep sleep (wake on MENU_BTN)...");
#endif
      delay(100);
      esp_deep_sleep_start();
    }
  }

#ifdef TARGET_ESP32C3
  // After timer wake, if no button was pressed during the wake window,
  // go back to sleep immediately to save power.
  if (sleepPending) {
    sleepPending = false;
    bool anyPressed = false;
    for (int i = 0; i < 8; i++) switches[i].update();
    menuBtn.update();
    for (int i = 0; i < 8; i++) {
      if (switches[i].pressed()) { anyPressed = true; break; }
    }
    if (!anyPressed && !menuBtn.pressed() && digitalRead(ENCODER_SW) == HIGH) {
      bool hasBT = bleKeyboard.isConnected();
      bool hasUSB = (millis() - lastSerialTime < 3000);
      if (!hasBT && !hasUSB) {
        lastConnectionTime = millis();
        display.clearDisplay();
        display.display();
        SET_BRIGHTNESS(0);
        esp_sleep_enable_timer_wakeup(5000000);
        log_w("[DBG sleep] timer wake, no activity — back to sleep");
        delay(100);
        esp_deep_sleep_start();
      }
    }
  }
#endif
}
