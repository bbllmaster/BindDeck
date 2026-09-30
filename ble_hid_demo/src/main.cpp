// ---------------------------------------------------------------------------
// Minimal ESP32-C3 BLE HID isolation test.
//
// No OLED, no encoder, no buttons, no WiFi, no config mode - nothing but the
// BLE keyboard. This isolates whether the C3's BLE HID link is stable on its
// own, independent of the BindDeck firmware.
//
// What it does:
//   - prints isConnected() every 500 ms, and immediately on every change
//   - reports a "flap" count (connect<->disconnect transitions) per 5 s window
//   - every 3 s sends a media VOLUME_UP key, so you can literally watch the
//     PC volume go up. If the volume never moves, HID is not reaching the host.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <BleKeyboard.h>

BleKeyboard bleKeyboard("C3 HID Test", "Test", 100);

bool          lastConn       = false;
int           flaps          = 0;
unsigned long lastPrint      = 0;
unsigned long lastKey        = 0;
unsigned long lastFlapReport = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[TEST] ==== C3 BLE HID minimal test ====");
  bleKeyboard.begin();

#if defined(SEC_SC_BOND) && !defined(USE_NIMBLE)
  // The t-vk library asks for ESP_LE_AUTH_REQ_SC_MITM_BOND, but this board has
  // no display/keypad so the default IO capability is NoInputNoOutput - MITM
  // can then never be satisfied and some hosts abort/lose the link mid-pairing.
  // Override to Secure Connections + bonding, WITHOUT the MITM requirement.
  {
    uint8_t auth  = ESP_LE_AUTH_REQ_SC_BOND;
    uint8_t iocap = ESP_IO_CAP_NONE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    Serial.println("[TEST] security override: SC_BOND + IO_CAP_NONE (no MITM)");
  }
#elif defined(SEC_SC_OUT) && !defined(USE_NIMBLE)
  // Alternative: keep MITM but advertise Keyboard+Display so a passkey flow is
  // possible (some hosts want authenticated pairing for keyboards).
  {
    uint8_t auth  = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    uint8_t iocap = ESP_IO_CAP_KBDISP;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    Serial.println("[TEST] security override: SC_MITM_BOND + IO_CAP_KBDISP");
  }
#endif

  Serial.println("[TEST] advertising started, waiting for host to connect...");
}

void loop() {
  unsigned long now = millis();

  bool conn = bleKeyboard.isConnected();
  if (conn != lastConn) {
    lastConn = conn;
    flaps++;
    Serial.printf("[TEST] %8lu ms  isConnected CHANGED -> %d  (flap #%d)\n",
                  now, conn ? 1 : 0, flaps);
  }

  if (now - lastPrint >= 500) {
    lastPrint = now;
    Serial.printf("[TEST] %8lu ms  isConnected=%d  flaps=%d\n",
                  now, conn ? 1 : 0, flaps);
  }

  if (now - lastFlapReport >= 5000) {
    lastFlapReport = now;
    Serial.printf("[TEST] %8lu ms  --- %d flaps in the last 5s window ---\n",
                  now, flaps);
    flaps = 0;
  }

  if (now - lastKey >= 3000) {
    lastKey = now;
    if (bleKeyboard.isConnected()) {
      Serial.printf("[TEST] %8lu ms  >>> sending VOLUME_UP\n", now);
      bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
    } else {
      Serial.printf("[TEST] %8lu ms  ... not connected, skipped key\n", now);
    }
  }

  delay(10);
}
