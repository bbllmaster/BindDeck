#include "nimble_hid.h"

#if defined(TARGET_ESP32C3)

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "ble_config.h"   // secondary GATT service for CFG/CMD over BLE

// ---------------------------------------------------------------------------
// HID report map: keyboard (report ID 1) + consumer control (report ID 2).
// ---------------------------------------------------------------------------
static const uint8_t HID_REPORT_MAP[] = {
  // ---- Keyboard, Report ID 1 ----
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05,
  0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
  0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xC0,
  // ---- Consumer Control, Report ID 2 ----
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
  0x75, 0x10, 0x95, 0x01, 0x15, 0x00, 0x26, 0xFF,
  0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x81, 0x00,
  0xC0
};

// --- ASCII -> HID usage map (copied from the t-vk library, identical) -------
#define SHIFT 0x80
static const uint8_t ASCII_MAP[128] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,        // 0x00-0x07
  0x2a,0x2b,0x28,0x00,0x00,0x00,0x00,0x00,        // 0x08-0x0F (BS,TAB,LF)
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,        // 0x10-0x17
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,        // 0x18-0x1F
  0x2c,0x1e|SHIFT,0x34|SHIFT,0x20|SHIFT,0x21|SHIFT,0x22|SHIFT,0x24|SHIFT,0x34, // 0x20-0x27
  0x26|SHIFT,0x27|SHIFT,0x25|SHIFT,0x2e|SHIFT,0x36,0x2d,0x37,0x38,             // 0x28-0x2F
  0x27,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,        // 0x30-0x37  (0-7)
  0x25,0x26,0x33|SHIFT,0x33,0x36|SHIFT,0x2e,0x37|SHIFT,0x38|SHIFT, // 0x38-0x3F
  0x1f|SHIFT,0x04|SHIFT,0x05|SHIFT,0x06|SHIFT,0x07|SHIFT,0x08|SHIFT,0x09|SHIFT,0x0a|SHIFT, // @ A-G
  0x0b|SHIFT,0x0c|SHIFT,0x0d|SHIFT,0x0e|SHIFT,0x0f|SHIFT,0x10|SHIFT,0x11|SHIFT,0x12|SHIFT, // H-O
  0x13|SHIFT,0x14|SHIFT,0x15|SHIFT,0x16|SHIFT,0x17|SHIFT,0x18|SHIFT,0x19|SHIFT,0x1a|SHIFT, // P-W
  0x1b|SHIFT,0x1c|SHIFT,0x1d|SHIFT,0x2f,0x31,0x30,0x23|SHIFT,0x2d|SHIFT, // X-Z [ \ ] ^ _
  0x35,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,        // ` a-g
  0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,        // h-o
  0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,        // p-w
  0x1b,0x1c,0x1d,0x2f|SHIFT,0x31|SHIFT,0x30|SHIFT,0x35|SHIFT,0x00 // x-z { | } ~ DEL
};

// --- connection state -------------------------------------------------------
static volatile bool s_connected = false;

class HidServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* /*s*/) override { s_connected = true; }
  void onDisconnect(NimBLEServer* /*s*/) override {
    s_connected = false;
    NimBLEDevice::startAdvertising();
  }
};

BleKeyboard::BleKeyboard(const char* deviceName, const char* deviceManufacturer, uint8_t batteryLevel)
  : _name(deviceName), _man(deviceManufacturer), _battery(batteryLevel) {}

void BleKeyboard::begin() {
  NimBLEDevice::init(_name);
  NimBLEDevice::setMTU(517);                                      // room for long CFG lines
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);                         // max TX power
  // Bonding + LEGACY pairing, no MITM: the C3 has no display/keypad, and
  // requiring MITM is exactly what made the t-vk build unusable.
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  _server = NimBLEDevice::createServer();
  _server->setCallbacks(new HidServerCB());

  _hid = new NimBLEHIDDevice(_server);
  _hid->manufacturer(_man);
  _hid->pnp(0x02, 0x1234, 0x5678, 0x0100);
  _hid->hidInfo(0x00, 0x01);
  _hid->reportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
  _inputKbd      = _hid->inputReport(1);
  _inputConsumer = _hid->inputReport(2);
  _hid->setBatteryLevel(_battery);
  // Create the CFG/CMD service on the same server BEFORE services are started.
  bleConfig.begin(_server);
  _hid->startServices();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(_hid->hidService()->getUUID());
  adv->start();
}

bool BleKeyboard::isConnected() { return s_connected; }

void BleKeyboard::setBatteryLevel(uint8_t level) {
  _battery = level;
  if (_hid) _hid->setBatteryLevel(level);
}

void BleKeyboard::sendKeyboard() {
  if (!s_connected || !_inputKbd) return;
  uint8_t report[8] = { _modifiers, 0, _keys[0], _keys[1], _keys[2], _keys[3], _keys[4], _keys[5] };
  _inputKbd->setValue(report, sizeof(report));
  _inputKbd->notify();
}

size_t BleKeyboard::press(uint8_t k) {
  uint8_t i;
  if (k >= 136) {              // non-printing key
    k = k - 136;
  } else if (k >= 128) {       // modifier key
    _modifiers |= (1 << (k - 128));
    k = 0;
  } else {                     // printing key
    k = ASCII_MAP[k];
    if (!k) return 0;
    if (k & 0x80) {            // capital / shifted character
      _modifiers |= 0x02;
      k &= 0x7F;
    }
  }

  if (_keys[0] != k && _keys[1] != k && _keys[2] != k &&
      _keys[3] != k && _keys[4] != k && _keys[5] != k) {
    for (i = 0; i < 6; i++) {
      if (_keys[i] == 0x00) { _keys[i] = k; break; }
    }
    if (i == 6) return 0;
  }
  sendKeyboard();
  return 1;
}

size_t BleKeyboard::release(uint8_t k) {
  if (k >= 136) {
    k = k - 136;
  } else if (k >= 128) {
    _modifiers &= ~(1 << (k - 128));
    k = 0;
  } else {
    k = ASCII_MAP[k];
    if (!k) return 0;
    if (k & 0x80) { _modifiers &= ~0x02; k &= 0x7F; }
  }
  for (uint8_t i = 0; i < 6; i++) {
    if (k && _keys[i] == k) _keys[i] = 0;
  }
  sendKeyboard();
  return 1;
}

size_t BleKeyboard::releaseAll() {
  _keys[0] = _keys[1] = _keys[2] = _keys[3] = _keys[4] = _keys[5] = 0;
  _modifiers = 0;
  sendKeyboard();
  return 1;
}

size_t BleKeyboard::write(uint8_t c) {
  uint8_t p = press(c);
  release(c);
  return p;
}

size_t BleKeyboard::write(const MediaKeyReport& k) {
  if (!s_connected || !_inputConsumer) return 0;
  uint8_t on[2]  = { (uint8_t)(k.usage & 0xFF), (uint8_t)(k.usage >> 8) };
  uint8_t off[2] = { 0, 0 };
  _inputConsumer->setValue(on, sizeof(on));
  _inputConsumer->notify();
  delay(10);
  _inputConsumer->setValue(off, sizeof(off));
  _inputConsumer->notify();
  return 1;
}

#endif  // TARGET_ESP32C3
