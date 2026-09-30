#include <Arduino.h>
#include "nimble_hid.h"   // C3: from-scratch NimBLE HID; other targets: t-vk
#include "ble_config.h"   // C3: CFG/CMD over the BLE link (secondary GATT service)
#include <Wire.h>
#include <esp_log.h>
#include <Adafruit_GFX.h>
#include <Bounce2.h>
#include <Preferences.h>
#include "RoboEyes.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <esp_sleep.h>
#include <esp_timer.h>

// === ESP32-C3: free the encoder pins from UART0 ===
// On ESP32-C3 the default UART0 pins are GPIO20/21, which this firmware uses
// for the rotary encoder (ENCODER_CLK/DT). Calling Serial.begin() there drives
// GPIO21 as a UART TX output and breaks the encoder's DT line -> quadrature
// decode fails (the volume bar appears on rotation but never changes). The board
// has no UART header; the console is USB-CDC (log_e) and PC CFG/control is over
// WiFi UDP, so UART0 is unused on C3. Provide a no-op Serial so the legacy
// Serial.* debug/CFG calls stay harmless instead of crashing.
#ifdef TARGET_ESP32C3
class NoopStream : public Print {
public:
  void begin(unsigned long) {}
  void setTimeout(unsigned long) {}
  int available() { return 0; }
  String readStringUntil(char) { return String(); }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t* b, size_t n) override { (void)b; return n; }
};
static NoopStream g_nullPrintC3;
#define Serial g_nullPrintC3
#endif

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

// === Firmware identity =====================================================
// There are several board/display variants and it is easy to flash the wrong
// image. The firmware therefore knows exactly what it is: version, display
// driver and chip. It is reported to the PC app (CMD:VERSION) and shown on the
// OLED settings screen, so a board can always be identified.
#define FW_VERSION "1.0.0"

#ifdef USE_SH1107
  #define FW_DISPLAY "SH1107"
#elif defined(USE_SH1106)
  #define FW_DISPLAY "SH1106"
#else
  #define FW_DISPLAY "SSD1306"
#endif

#ifdef TARGET_ESP32C3
  #define FW_CHIP "C3"
#else
  #define FW_CHIP "ESP32"
#endif

#define FW_BUILD __DATE__ " " __TIME__

// WiFi credentials are NOT hardcoded. They are provisioned on the device: in
// config mode it raises its own access point and serves a small web page where
// you pick a network and type the password. The credentials are then stored in
// NVS. These empty fallbacks only exist so nothing ever calls WiFi.begin() with
// a garbage SSID.
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
WiFiUDP udp;
static WebServer cfgServer(80);   // provisioning portal, config mode only

// On ESP32-C3 WiFi and BLE share one 2.4 GHz radio. Leaving WiFi up during
// normal use makes the BLE HID link flap (connect/disconnect) even with a single
// host. So WiFi is OFF by default and only started in an explicit "config mode"
// (hold the encoder button while powering on). Config mode raises a setup AP for
// provisioning and, if credentials exist, also joins that network so the PC app
// can reach the UDP CFG channel on port 4210.
static bool g_configMode = false;
static unsigned long g_configModeStart = 0;
const unsigned long CONFIG_MODE_MS = 300000;   // 5 min - enough to provision

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
// ESP32-C3 usable GPIOs (per board IO map): 0,1,2,3,4,5,6,7,8,9,10,20,21.
// UNAVAILABLE: 11 (VDD_SPI / flash power), 12-17 (embedded flash IO lines),
// 18/19 (USB-CDC console), so never assign those to buttons/encoder/OLED.
// Pin budget: with flash(11-17)+USB(18/19) off-limits there are only 13 free
// GPIOs for 8 buttons + menu + 3 encoder + 2 OLED = 14 functions, so the
// rotary-push (ENCODER_SW) doubles as the menu button (common encoder design).
// If the board has a SEPARATE menu button, set its real pin via CFG:PINS.
#define ENCODER_CLK 20
#define ENCODER_DT 21
#define ENCODER_SW 1
#define OLED_SDA 4
#define OLED_SCL 5
#define MENU_BTN 1   // shared with ENCODER_SW (rotary push = menu)
const int DEFAULT_SWITCH_PINS[8] = {0, 2, 3, 6, 7, 8, 9, 10};
#endif

const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
volatile int encoderSteps = 0;
volatile uint8_t old_AB = 0;

// Called to sample the encoder. On ESP32-C3 this is driven by a 1 kHz
// esp_timer (see encoderTimerCb) so quadrature edges are never missed by the
// slow main loop. It only does digitalRead() + integer math, so it is safe to
// call from the esp_timer task context (default dispatch is ESP_TIMER_TASK).
void readEncoder() {
  old_AB <<= 2;
  uint8_t current = 0;
  if (digitalRead(ENCODER_CLK)) current |= 0x02;
  if (digitalRead(ENCODER_DT)) current |= 0x01;
  old_AB |= (current & 0x03);
  encoderSteps += enc_states[(old_AB & 0x0f)];
}

#ifdef TARGET_ESP32C3
// High-rate sampler: runs in the esp_timer task (NOT an IRAM ISR), so there is
// no flash/IRAM hazard and no interrupt storm. 1 kHz is far above any human
// EC11 rotation speed, guaranteeing every detent transition is captured.
static esp_timer_handle_t s_encoderTimer = NULL;
static void encoderTimerCb(void* arg) {
  (void)arg;
  readEncoder();
}
#endif

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
#ifdef TARGET_ESP32C3
  // ESP32-C3 only exposes GPIO 0-21.
  if (pin < 0 || pin > 21) return false;
  // Embedded flash occupies GPIO11 (VDD_SPI) and 12-17 (flash IO lines).
  // Using them as GPIO starves the flash -> CPU stall -> TG1WDT_SYS_RST.
  if (pin >= 11 && pin <= 17) return false;
#else
  if (pin < 0 || pin > 39) return false;            // ESP32 has GPIO 0-39
#endif
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
  log_e("[DBG apply] start (8 buttons)");
  for (int i = 0; i < 8; i++) {
    log_e("[DBG apply] i=%d pin=%d attach...", i, switchPins[i]);
    switches[i].attach(switchPins[i], INPUT_PULLUP);
    switches[i].interval(25);
    switches[i].setPressedState(LOW);
    log_e("[DBG apply] i=%d ok", i);
  }
  log_e("[DBG apply] done");
}
Bounce2::Button menuBtn;
int currentIdleScreen = 0; // 0=Stats, 1=Time, 2=Eyes, 3=Firmware info


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

// POSIX TZ string used for NTP. Configurable with CFG:TZ:<posix> (the PC app
// sends it from its timezone picker); previously this was hardcoded to Central
// European Time, so every other region showed the wrong clock.
char tzString[64] = "CET-1CEST,M3.5.0,M10.5.0/3";

void applyTimezone() {
  configTzTime(tzString, "pool.ntp.org");
}
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
  preferences.putString("tz", tzString);
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
void drawInfoScreen();
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
    log_e("[DBG load_config] NVS begin failed, using defaults");
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
  String tz = preferences.getString("tz", tzString);
  tz.trim();
  if (tz.length() > 0 && tz.length() < (int)sizeof(tzString)) {
    strncpy(tzString, tz.c_str(), sizeof(tzString) - 1);
    tzString[sizeof(tzString) - 1] = '\0';
  }
  preferences.end();
  log_e("[DBG load_config] switchPins={");
  for (int i = 0; i < 8; i++) { log_e("  pin%d=%d%s", i, switchPins[i], (i < 7) ? "," : ""); }
  log_e("[DBG load_config] switchPins end");
}

// Track last time we received data (Serial or WiFi)
unsigned long lastDataTime = 0;
IPAddress pcIP;
bool pcIpSet = false;

void sendDataToPC(String data) {
  Serial.println(data);
  // Same line goes out over the BLE CFG service when the PC is subscribed, so
  // the PC app works without WiFi; the WiFi UDP path below stays as a fallback.
  bleConfig.notify(data);
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
      log_e("[DBG CFG:PINS] REJECTED — keeping previous switchPins");
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
        // Keep the setup AP up so the provisioning page stays reachable.
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(ssid.c_str(), pwd.c_str());
  applyTimezone();
      }
    } else if (data.startsWith("CMD:VERSION")) {
      // VERSION:<fw>,<display>,<chip>,<build>  - lets the app (and us) tell
      // which variant a board is running.
      sendDataToPC(String("VERSION:") + FW_VERSION + "," + FW_DISPLAY + "," +
                   FW_CHIP + "," + FW_BUILD);
    } else if (data.startsWith("CFG:TZ:")) {
      // CFG:TZ:<posix tz> - the PC app sends the picked timezone. Everything
      // after the prefix is the payload, so the commas in DST rules
      // ("CET-1CEST,M3.5.0,M10.5.0/3") are fine.
      String tz = data.substring(7);
      tz.trim();
      if (tz.length() > 0 && tz.length() < (int)sizeof(tzString)) {
        strncpy(tzString, tz.c_str(), sizeof(tzString) - 1);
        tzString[sizeof(tzString) - 1] = '\0';
        saveConfig();
        applyTimezone();
        log_e("[cfg] tz set to %s", tzString);
      }
    } else if (data.startsWith("CMD:GET_TZ")) {
      sendDataToPC(String("TZ_INFO:") + tzString);
    } else if (data.startsWith("CMD:GET_WIFI")) {
      if (WiFi.status() == WL_CONNECTED) {
        preferences.begin("binddeck", true);
        String ssid = preferences.getString("wifiSSID", WIFI_SSID);
        preferences.end();
        // sendDataToPC goes over UDP too, so the Wi-Fi status panel works on
        // C3 (which has no usable USB serial). It also prints to Serial for
        // classic ESP32, preserving that behavior.
        sendDataToPC("WIFI_INFO:" + ssid + "," + WiFi.localIP().toString());
      } else {
        sendDataToPC("WIFI_INFO:DISCONNECTED,0.0.0.0");
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

    // Acknowledge CFG: commands. Without this a CFG: write is completely silent,
    // so neither the PC app nor a channel test can tell whether it was applied.
    if (data.startsWith("CFG:")) {
      sendDataToPC("ACK:" + data.substring(0, 96));
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

// Is a pack actually connected?
//
// With no pack attached the sense input floats, and a floating ESP32 ADC pin
// reads noise anywhere across the range - the old "raw < 1000" test let those
// readings through, and the UI happily showed "0%". A real pack - even a flat
// 3.0 V one - reads a *stable* ~1850 or more through the 1:2 divider, so
// require both a sensible level and a small spread over several samples.
bool batteryPresent(int* outAvg) {
  const int SAMPLES = 8;
  const int FLOOR = 1800;      // 3.0 V pack through the divider
  const int MAX_SPREAD = 150;  // a floating pin wanders much more than this
  int lo = 4096, hi = 0;
  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int v = analogRead(BATTERY_PIN);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    sum += v;
    delayMicroseconds(150);
  }
  int avg = (int)(sum / SAMPLES);
  if (outAvg) *outAvg = avg;
  return (avg >= FLOOR) && ((hi - lo) <= MAX_SPREAD);
}

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
  
  // Presence is re-evaluated once a second; averaging 8 ADC samples is too
  // expensive to do on every frame, and the answer cannot change that fast.
  static bool present = false;
  static bool loggedPresent = false;
  static unsigned long lastPresenceCheck = 0;
  static int lastRaw = 0;
  if (millis() - lastPresenceCheck > 1000) {
    lastPresenceCheck = millis();
    present = batteryPresent(&lastRaw);
    if (present != loggedPresent) {
      log_e("[bat] raw=%d present=%d", lastRaw, (int)present);
      loggedPresent = present;
    }
  }
  if (!present) {
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

// Firmware identity screen: which build / chip / display this board is running,
// so a board can always be matched to the right image without guessing.
void drawInfoScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);

  display.setCursor(0, 0);
  display.println("BindDeck");

  display.setCursor(0, 16);
  display.print("Chip: ");
  display.println(FW_CHIP);

  display.setCursor(0, 26);
  display.print("Disp: ");
  display.println(FW_DISPLAY);

  display.setCursor(0, 36);
  display.print("FW  : v");
  display.println(FW_VERSION);

  display.setCursor(0, 48);
  display.println(FW_BUILD);

  display.display();
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

// --- WiFi provisioning (no hardcoded credentials) --------------------------
// Credentials live in NVS. Empty = never provisioned.
static void loadWifiCreds(String& ssid, String& pwd) {
  preferences.begin("binddeck", true);
  ssid = preferences.getString("wifiSSID", "");
  pwd  = preferences.getString("wifiPwd", "");
  preferences.end();
}

static void saveWifiCreds(const String& ssid, const String& pwd) {
  preferences.begin("binddeck", false);
  preferences.putString("wifiSSID", ssid);
  preferences.putString("wifiPwd", pwd);
  preferences.end();
}

static String htmlEsc(const String& s) {
  String o;
  o.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if      (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

static String cfgPage() {
  String ssid, pwd;
  loadWifiCreds(ssid, pwd);

  String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>BindDeck</title></head>"
    "<body style='font-family:sans-serif;max-width:440px;margin:24px auto;padding:0 16px'>"
    "<h2>BindDeck WiFi 配网</h2><p>状态: ");
  if (WiFi.status() == WL_CONNECTED) {
    h += "已连接 <b>" + htmlEsc(WiFi.SSID()) + "</b> · IP " + WiFi.localIP().toString();
  } else if (ssid.length()) {
    h += "未连上，已保存 <b>" + htmlEsc(ssid) + "</b>，正在重试…";
  } else {
    h += "尚未配置";
  }
  h += F("</p><form method='POST' action='/save'>"
         "<label>WiFi 名称</label><br>"
         "<input name='ssid' list='nets' autocomplete='off' "
         "style='width:100%;padding:8px;box-sizing:border-box' value='");
  h += htmlEsc(ssid);
  h += F("'><br><br><label>密码</label><br>"
         "<input name='pwd' type='password' "
         "style='width:100%;padding:8px;box-sizing:border-box' value='");
  h += htmlEsc(pwd);
  h += F("'><br><br><button style='padding:10px 20px'>保存并连接</button></form>");

  // Nearby networks as an autocomplete list (no JS needed).
  int n = WiFi.scanNetworks();
  if (n > 0) {
    h += F("<datalist id='nets'>");
    for (int i = 0; i < n && i < 20; i++) {
      h += "<option value='" + htmlEsc(WiFi.SSID(i)) + "'>";
    }
    h += F("</datalist><p>附近网络（点击输入框选择）:</p><ul>");
    for (int i = 0; i < n && i < 12; i++) {
      h += "<li>" + htmlEsc(WiFi.SSID(i)) + " (" + String(WiFi.RSSI(i)) + " dBm)</li>";
    }
    h += F("</ul>");
  }
  h += F("<p style='color:#888'>设备只在配网模式开热点；配好后回到蓝牙模式，WiFi 会关闭。</p>"
         "</body></html>");
  return h;
}

static void handleCfgRoot() {
  cfgServer.send(200, "text/html; charset=utf-8", cfgPage());
}

static void handleCfgSave() {
  String ssid = cfgServer.arg("ssid");
  String pwd  = cfgServer.arg("pwd");
  ssid.trim();
  if (ssid.length() == 0) {
    cfgServer.send(400, "text/html; charset=utf-8",
                   "<meta charset='utf-8'><p>WiFi 名称不能为空。<a href='/'>返回</a></p>");
    return;
  }
  saveWifiCreds(ssid, pwd);
  log_e("[cfg] saved WiFi ssid='%s'", ssid.c_str());
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP_STA);          // keep the AP so this page stays reachable
  WiFi.begin(ssid.c_str(), pwd.c_str());
  applyTimezone();
  cfgServer.send(200, "text/html; charset=utf-8",
                 "<meta charset='utf-8'><p>已保存，正在连接 <b>" + htmlEsc(ssid) +
                 "</b>…</p><p><a href='/'>返回查看状态</a></p>");
}

void setupProvisioning() {
  // AP so a phone can reach the config page; STA so the device can join the
  // home network and then be reached by the PC app over UDP 4210.
  WiFi.mode(WIFI_AP_STA);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apName[32];
  snprintf(apName, sizeof(apName), "BindDeck-%02X%02X", mac[4], mac[5]);
  WiFi.softAP(apName);             // open AP, up only while in config mode
  String apIp = WiFi.softAPIP().toString();
  log_e("[cfg] AP '%s' up at http://%s", apName, apIp.c_str());

  String ssid, pwd;
  loadWifiCreds(ssid, pwd);
  if (ssid.length()) {
    WiFi.begin(ssid.c_str(), pwd.c_str());
    applyTimezone();
    log_e("[cfg] joining saved WiFi '%s'", ssid.c_str());
  } else {
    log_e("[cfg] no saved WiFi - join AP '%s' and open http://%s", apName, apIp.c_str());
  }

  cfgServer.on("/", HTTP_GET, handleCfgRoot);
  cfgServer.on("/save", HTTP_POST, handleCfgSave);
  cfgServer.onNotFound([]() {
    cfgServer.sendHeader("Location", "/", true);
    cfgServer.send(302, "text/plain", "");
  });
  cfgServer.begin();
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
  delay(1000);  // let the USB-CDC console enumerate before emitting diagnostics
  log_e("[DBG setup] serial ok"); delay(30);
  loadConfig();  // populates switchPins[] from Preferences (or defaults)
  log_e("[DBG setup] loadConfig ok"); delay(30);

  // Config mode: hold the encoder button CONTINUOUSLY for ~2s at boot to bring
  // WiFi up for configuration. A transient/floating read can't trigger it.
  // Otherwise WiFi stays OFF so the BLE HID link is stable.
  pinMode(ENCODER_SW, INPUT_PULLUP);
  {
    unsigned long holdStart = 0;
    bool triggered = false;
    for (int step = 0; step < 100 && !triggered; step++) {  // ~2s @ 20ms
      if (digitalRead(ENCODER_SW) == LOW) {
        if (holdStart == 0) holdStart = millis();
        else if (millis() - holdStart >= 2000) triggered = true;
      } else {
        holdStart = 0;
      }
      delay(20);
    }
    if (triggered) {
      g_configMode = true;
      g_configModeStart = millis();
      log_e("[DBG setup] CONFIG MODE (encoder btn held 2s) - AP + WiFi + portal");
      setupProvisioning();
    } else {
      log_e("[DBG setup] normal mode - WiFi OFF (BLE only)");
    }
  }

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
  log_e("[DBG setup] display ok"); delay(30);
  display.setRotation(DISPLAY_ROTATION);

  SET_BRIGHTNESS(brightness);
  display.clearDisplay();
  display.display();
  
  eyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 30);
  eyes.setAutoblinker(true, 2, 2);
  eyes.setIdleMode(true, 1, 2);
  log_e("[DBG setup] eyes ok"); delay(30);
  
  // BLE is always initialized: the keyboard must keep working even during the
  // brief config window, and the OLED must keep drawing (never blank).
  // Route CFG:/CMD: lines written to the BLE config characteristic into the
  // same command parser the WiFi UDP channel uses. Must be set before begin().
  bleConfig.onCommand = [](const String& cmd) {
    log_e("[cfg ble] rx: %s", cmd.c_str());
    processCommand(cmd);
  };
  log_e("[DBG setup] ble begin..."); delay(30);
  bleKeyboard.begin();
  log_e("[DBG setup] ble ok"); delay(30);

  // Encoder setup
  log_e("[DBG setup] enc pins..."); delay(30);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  log_e("[DBG setup] enc pins ok"); delay(30);
#ifdef TARGET_ESP32C3
  // Encoder is sampled by a 1 kHz esp_timer (encoderTimerCb) instead of once
  // per loop(). The main loop does OLED redraws + BLE and runs every ~tens of
  // ms, which previously missed quadrature transitions on fast rotation and
  // made the volume bar appear but never change. Sampling at 1 kHz captures
  // every edge. The timer runs in a task context, so digitalRead() is safe.
  if (s_encoderTimer == NULL) {
    esp_timer_create_args_t encTimerCfg = {
      .callback = &encoderTimerCb,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "enc"
    };
    esp_timer_create(&encTimerCfg, &s_encoderTimer);
    esp_timer_start_periodic(s_encoderTimer, 1000); // 1000 us = 1 ms
  }
  log_e("[DBG setup] enc timer 1kHz started"); delay(30);
#else
  log_e("[DBG setup] enc poll (loop)"); delay(30);
#endif

  log_e("[DBG setup] applyPins..."); delay(30);
applyButtonPins();
  log_e("[DBG setup] buttons ok"); delay(30);

  menuBtn.attach(MENU_BTN, INPUT_PULLUP);
  menuBtn.interval(25);
  menuBtn.setPressedState(LOW);

  log_e("[DBG setup] DONE - entering loop"); delay(30);

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
  if (!_loopStarted) { _loopStarted = true; log_e("[DBG loop] started"); delay(30); }
  static int _loopCount = 0;
  _loopCount++;
  if (_loopCount <= 3) { log_e("[DBG loop] iter %d", _loopCount); delay(30); }

  // Config mode: the setup AP + web portal are up so you can provision WiFi,
  // and the UDP CFG channel is exposed so the PC app can reach the device.
  // BLE stays on and the normal loop keeps running (OLED draws, keyboard
  // works). Exit by holding the encoder button ~1s, or it auto-disables WiFi
  // after the timeout. There is NO reboot path, so a stuck button can never
  // cause a boot loop.
  if (g_configMode) {
    cfgServer.handleClient();
    loopWiFi();
    static unsigned long _cfgExitHold = 0;
    if (digitalRead(ENCODER_SW) == LOW) {
      if (_cfgExitHold == 0) _cfgExitHold = millis();
      else if (millis() - _cfgExitHold >= 1000) {
        log_e("[DBG cfg] exit requested, WiFi off -> BLE mode");
        WiFi.mode(WIFI_OFF);
        g_configMode = false;
        _cfgExitHold = 0;
      }
    } else {
      _cfgExitHold = 0;
    }
    if (g_configMode && (millis() - g_configModeStart > CONFIG_MODE_MS)) {
      log_e("[DBG cfg] timeout, WiFi off -> BLE mode");
      WiFi.mode(WIFI_OFF);
      g_configMode = false;
    }
  }

  for(int i = 0; i < 8; i++) switches[i].update();
  menuBtn.update();
  
  if (menuBtn.pressed()) {
    currentIdleScreen = (currentIdleScreen + 1) % 4;
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
    // WiFi/UDP is serviced only in config mode (see the g_configMode branch at
    // the top of loop). In normal mode WiFi is off, so this is a no-op here.

    // --- BLE connection diagnostic (C3 especially: "paired" in OS != HID
    // channel open). Logs the firmware's view of the BLE HID connection so we
    // can tell a missing key-send apart from a key-send the OS ignores. ---
    // Keep the device discoverable while connected, otherwise a config app can
    // never find it (the stack stops advertising once the link is up).
#ifdef TARGET_ESP32C3
    static unsigned long _lastAdvKick = 0;
    if (millis() - _lastAdvKick > 1500) {
      _lastAdvKick = millis();
      bleKeyboard.keepAdvertising();
    }
#endif

    static unsigned long _lastBleLog = 0;
    if (millis() - _lastBleLog > 3000) {
      _lastBleLog = millis();
      log_e("[DBG ble] isConnected=%d", (int)bleKeyboard.isConnected());
    }

    for(int i = 0; i < 8; i++) {
      if(switches[i].pressed()) {
        log_e("[DBG btn] i=%d pressed (ble=%d)", i, (int)bleKeyboard.isConnected());
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
    // On ESP32-C3, encoderSteps is updated by the 1 kHz esp_timer
    // (encoderTimerCb). On classic ESP32 there is no timer; keep polling here
    // so its behavior is unchanged from before the C3 timer rework.
#ifndef TARGET_ESP32C3
    readEncoder();
#endif
    static int lastEncoderSteps = 0;
    if (encoderSteps / 4 != lastEncoderSteps / 4) {
      noInterrupts();
      int currentSteps = encoderSteps;
      interrupts();
      log_e("[DBG enc] diff=%d ble=%d", (currentSteps / 4) - (lastEncoderSteps / 4), (int)bleKeyboard.isConnected());
      
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

#ifdef TARGET_ESP32C3
    // Diagnostic: confirm the encoder counter actually moves when rotating.
    // If steps stays 0 while you turn the knob, the EC11 is NOT on GPIO20/21
    // (wrong board wiring) rather than a decode bug.
    static unsigned long lastEncLog = 0;
    if (millis() - lastEncLog > 1000) {
      log_e("[DBG enc] steps=%d vol=%d", encoderSteps, visualVolume);
      lastEncLog = millis();
    }
#endif
    
    if (currentState == STATE_IDLE) {
      if (currentIdleScreen == 0) {
        drawIdle(); // PC Stats
      } else if (currentIdleScreen == 1) {
        drawTimeScreen();
      } else if (currentIdleScreen == 3) {
        drawInfoScreen(); // Firmware identity (chip / display / version)
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
      log_e("[DBG sleep] entering deep sleep (timer wake 5s)...");
#else
      // ESP32 (original): ext1 only supports ALL_LOW or ANY_HIGH. Buttons are
      // normally HIGH (pull-up) and go LOW when pressed, so we use ext0 on the
      // menu button as the primary wake source.
      pinMode(MENU_BTN, INPUT_PULLUP);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)MENU_BTN, 0);
      log_e("[DBG sleep] entering deep sleep (wake on MENU_BTN)...");
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
        log_e("[DBG sleep] timer wake, no activity — back to sleep");
        delay(100);
        esp_deep_sleep_start();
      }
    }
  }
#endif
}
