// ---------------------------------------------------------------------------
// Plain ESP32-C3 BLE peripheral - NO HID, no extra libraries.
//
// Purpose: separate "C3 BLE stack / RF is unstable" from "the HID library is
// unstable". This just advertises a dummy service and logs connect/disconnect,
// with a 5 s flap counter - exactly like the HID demo, but without any HID.
//
// If THIS also flaps -> the C3's BLE link itself is the problem (RF/power/host).
// If THIS stays connected for minutes -> the BLE stack is fine and the fault is
// specific to the BLE-HID implementation.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

static const char* SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb";
static const char* CHAR_UUID    = "0000ffe1-0000-1000-8000-00805f9b34fb";

volatile bool lastConn = false;
volatile int  flaps    = 0;
unsigned long lastPrint  = 0;
unsigned long lastReport = 0;

class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    lastConn = true;
    flaps++;
    Serial.printf("[BLE] CONNECT    (change #%d)\n", flaps);
  }
  void onDisconnect(BLEServer* s) override {
    lastConn = false;
    flaps++;
    Serial.printf("[BLE] DISCONNECT (change #%d) -> re-advertising\n", flaps);
    delay(100);
    BLEDevice::startAdvertising();
  }
};

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[BLE] ==== plain BLE peripheral test (no HID) ====");
  BLEDevice::init("C3 BLE Test");
  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new SrvCb());
  BLEService* svc = srv->createService(SERVICE_UUID);
  BLECharacteristic* ch = svc->createCharacteristic(
      CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  ch->setValue("hello");
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->start();
  Serial.println("[BLE] advertising as 'C3 BLE Test' - connect from a phone or PC");
}

void loop() {
  unsigned long now = millis();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.printf("[BLE] %8lu ms  connected=%d  flaps=%d\n", now, lastConn ? 1 : 0, flaps);
  }
  if (now - lastReport >= 5000) {
    lastReport = now;
    Serial.printf("[BLE] %8lu ms  --- %d flaps in the last 5s window ---\n", now, flaps);
    flaps = 0;
  }
  delay(10);
}
