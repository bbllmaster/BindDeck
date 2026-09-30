// ---------------------------------------------------------------------------
// From-scratch BLE HID keyboard on NimBLE - no t-vk/ESP32 BLE Keyboard library.
//
// The only real difference from the earlier demos is that the pairing security
// is under our control (bond / MITM / Secure-Connections are build flags),
// because the failure we keep seeing is at the pairing/encryption step.
//
// It logs: isConnected every 500 ms + on every change, a 5 s flap counter, and
// the negotiated connection parameters. Every 3 s it sends Consumer
// VOLUME_UP, so the host-side effect is directly observable.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#ifndef SEC_BOND
#define SEC_BOND 1
#endif
#ifndef SEC_MITM
#define SEC_MITM 0
#endif
#ifndef SEC_SC
#define SEC_SC 0
#endif

#define DEVICE_NAME "C3 HID Test"

// HID report map: keyboard (report ID 1) + consumer control (report ID 2).
static const uint8_t hidReportMap[] = {
  // ---- Keyboard, Report ID 1 ----
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,        //   Usage Min (LeftControl)
  0x29, 0xE7,        //   Usage Max (Right GUI)
  0x15, 0x00,        //   Logical Min (0)
  0x25, 0x01,        //   Logical Max (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Const)
  0x95, 0x05,        //   Report Count (5)
  0x75, 0x01,        //   Report Size (1)
  0x05, 0x08,        //   Usage Page (LEDs)
  0x19, 0x01,        //   Usage Min (Num Lock)
  0x29, 0x05,        //   Usage Max (Kana)
  0x91, 0x02,        //   Output (Data,Var,Abs)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x03,        //   Report Size (3)
  0x91, 0x01,        //   Output (Const)
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Min (0)
  0x25, 0x65,        //   Logical Max (101)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0x00,        //   Usage Min (0)
  0x29, 0x65,        //   Usage Max (101)
  0x81, 0x00,        //   Input (Data,Array)
  0xC0,              // End Collection
  // ---- Consumer Control, Report ID 2 ----
  0x05, 0x0C,        // Usage Page (Consumer)
  0x09, 0x01,        // Usage (Consumer Control)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x02,        //   Report ID (2)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x01,        //   Report Count (1)
  0x15, 0x00,        //   Logical Min (0)
  0x26, 0xFF, 0x03,  //   Logical Max (1023)
  0x19, 0x00,        //   Usage Min (0)
  0x2A, 0xFF, 0x03,  //   Usage Max (1023)
  0x81, 0x00,        //   Input (Data,Array,Abs)
  0xC0               // End Collection
};

static NimBLEHIDDevice*    hid            = nullptr;
static NimBLECharacteristic* inputKbd     = nullptr;
static NimBLECharacteristic* inputConsumer = nullptr;
static NimBLEServer*       server         = nullptr;

static volatile bool gConnected = false;
static volatile int  gFlaps     = 0;
static unsigned long lastPrint      = 0;
static unsigned long lastFlapReport = 0;
static unsigned long lastKey        = 0;
static unsigned long lastConnLog    = 0;

class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s) override {
    gConnected = true;
    gFlaps++;
    Serial.println("[N] CONNECT");
  }
  void onDisconnect(NimBLEServer* s) override {
    gConnected = false;
    gFlaps++;
    Serial.println("[N] DISCONNECT -> re-advertising");
    NimBLEDevice::startAdvertising();
  }
};

static void sendConsumer(uint16_t usage) {
  uint8_t r[2] = { (uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8) };
  inputConsumer->setValue(r, sizeof(r));
  inputConsumer->notify();
  delay(15);
  uint8_t z[2] = { 0, 0 };
  inputConsumer->setValue(z, sizeof(z));
  inputConsumer->notify();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[N] ==== C3 NimBLE HID, from scratch (no t-vk) ====");

  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // max TX power
  NimBLEDevice::setSecurityAuth(SEC_BOND, SEC_MITM, SEC_SC);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  Serial.printf("[N] security: bond=%d mitm=%d sc=%d\n", SEC_BOND, SEC_MITM, SEC_SC);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new SrvCB());

  hid = new NimBLEHIDDevice(server);
  hid->manufacturer("Test");
  hid->pnp(0x02, 0x1234, 0x5678, 0x0100);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t*)hidReportMap, sizeof(hidReportMap));
  inputKbd      = hid->inputReport(1);
  inputConsumer = hid->inputReport(2);
  hid->startServices();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->start();
  Serial.println("[N] advertising as '" DEVICE_NAME "'");
}

void loop() {
  unsigned long now = millis();

  if (now - lastPrint >= 500) {
    lastPrint = now;
    Serial.printf("[N] %8lu ms connected=%d flaps=%d\n", now, gConnected ? 1 : 0, gFlaps);
  }
  if (now - lastFlapReport >= 5000) {
    lastFlapReport = now;
    Serial.printf("[N] %8lu ms --- %d flaps in the last 5s window ---\n", now, gFlaps);
    gFlaps = 0;
  }

  // Log the negotiated connection parameters once a second while connected.
  if (gConnected && now - lastConnLog >= 1000) {
    lastConnLog = now;
    if (server->getConnectedCount() > 0) {
      NimBLEConnInfo ci = server->getPeerInfo(0);
      Serial.printf("[N] conn: interval=%.2fms timeout=%dms latency=%d\n",
                    ci.getConnInterval() * 1.25f, ci.getConnTimeout() * 10, ci.getConnLatency());
    }
  }

  if (now - lastKey >= 3000) {
    lastKey = now;
    if (gConnected) {
      Serial.printf("[N] %8lu ms >>> VOLUME_UP\n", now);
      sendConsumer(0x00E9);   // Consumer Volume Increment
    } else {
      Serial.printf("[N] %8lu ms ... not connected, skipped key\n", now);
    }
  }

  delay(10);
}
