/*
 * Composite HID: Gamepad + Mouse for Arduino Mbed RP2040
 *
 * Requires to be installed in board manager:
 *   "Arduino Mbed OS RP2040 Boards" by Arduino
 *   v4.5.0 (or greater)
 *
 * Presents a single HID interface to the USB host with two Report IDs:
 *  - Report ID 0x01: Gamepad (compatible with PicoGamepad format)
 *  - Report ID 0x02: Mouse   (3 buttons + X/Y + Wheel)
 *
 * Only supports Arduino Mbed core (uses PluggableUSBHID).
 */

#ifndef COMPOSITE_GAMEPAD_MOUSE_H
#define COMPOSITE_GAMEPAD_MOUSE_H

#include "PluggableUSBHID.h"
#include "usb_phy_api.h"
#include "PlatformMutex.h"
#include <stdint.h>
#include <string.h>

// Reuse PicoGamepad index constants if available
// Define report layout offsets matching our report descriptor
// Layout (after Report ID):
// - 128 buttons  -> 16 bytes [0..15]
// - 2 axes x16b  ->  4 bytes [16..19]
// - 1 hat x4b    ->  1 byte  [20] (upper 4 bits are padding)
static constexpr uint8_t GAMEPAD_PAYLOAD_LEN = 16 + 4 + 1;  // 21 bytes
static constexpr uint8_t REPORT_ID_MOUSE = 0x01;
static constexpr uint8_t REPORT_ID_GAMEPAD = 0x02;
enum {
  BUTTONS_START = 0,
  BUTTONS_BYTES = 16,
  AXES_START = BUTTONS_START + BUTTONS_BYTES,  // 16
  AXIS_STRIDE = 2,
  X_AXIS_LSB = AXES_START + 0,
  X_AXIS_MSB = AXES_START + 1,
  Y_AXIS_LSB = AXES_START + 2,
  Y_AXIS_MSB = AXES_START + 3,
  HAT0 = AXES_START + 4  // byte 20
};

namespace arduino {

class CompositeHIDDevice : public USBHID {
public:
  explicit CompositeHIDDevice(uint16_t vendor_id = 0x1235,
                                 uint16_t product_id = 0x0051,
                                 uint16_t product_release = 0x0001)
    : USBHID(get_usb_phy(), 0, 0, vendor_id, product_id, product_release) {
    SetHat(0, 8);  // 8 = center
    _mouseButtons = 0;
  }

  virtual ~CompositeHIDDevice() {
    SetHat(0, 8);  // 8 = center
  }

  // Gamepad API (mirrors PicoGamepad minimal surface used by the sketch)
  void SetButton(int idx, bool val) {
    if (idx > 128 || idx < 0) {
      return;
    }
    bitWrite(_gamepadInput[idx / 8], idx % 8, val);
  }

  void SetX(int16_t val) {
    _gamepadInput[X_AXIS_LSB] = LSB(val);
    _gamepadInput[X_AXIS_MSB] = MSB(val);
  }
  void SetY(int16_t val) {
    _gamepadInput[Y_AXIS_LSB] = LSB(val);
    _gamepadInput[Y_AXIS_MSB] = MSB(val);
  }

  void SetHat(uint8_t hatIdx, uint8_t dir) {
    uint8_t hatDir[9][4] = {
      { 0, 0, 0, 0 },
      { 0, 0, 0, 1 },
      { 0, 0, 1, 0 },
      { 0, 0, 1, 1 },
      { 0, 1, 0, 0 },
      { 0, 1, 0, 1 },
      { 0, 1, 1, 0 },
      { 0, 1, 1, 1 },
      { 1, 0, 0, 0 }
    };
    if (hatIdx != 0) { return; }
    for (int i = 0; i < 4; i++) { bitWrite(_gamepadInput[HAT0], 3 - i, hatDir[dir][i]); }
  }

  bool send_update() {
    _mutex.lock();
    HID_REPORT report;
    report.data[0] = REPORT_ID_GAMEPAD;
    for (int i = 1; i <= GAMEPAD_PAYLOAD_LEN; i++) {
      report.data[i] = _gamepadInput[i - 1];
    }
    report.length = 1 + GAMEPAD_PAYLOAD_LEN;
    bool ok = send(&report);
    _mutex.unlock();
    return ok;
  }

  // Mouse API (minimal)
  void press(uint8_t mask) {
    _mouseButtons |= (mask & 0x1F);
  }
  void release(uint8_t mask) {
    _mouseButtons &= ~(mask & 0x1F);
  }
  void setButtons(uint8_t mask) {
    _mouseButtons = (mask & 0x1F);
  }

  bool move(int8_t dx, int8_t dy, int8_t wheel = 0) {
    _mutex.lock();
    HID_REPORT report;
    memset(report.data, 0, sizeof(report.data));
    report.data[0] = REPORT_ID_MOUSE;
    report.data[1] = _mouseButtons & 0x07;
    report.data[2] = (uint8_t)dx;
    report.data[3] = (uint8_t)dy;
    report.data[4] = (uint8_t)wheel;
    report.length = 5;
    bool ok = send(&report);
    _mutex.unlock();
    return ok;
  }

  // USBHID overrides
  virtual const uint8_t *report_desc();
  virtual const uint8_t *configuration_desc(uint8_t index);

private:
  uint8_t _gamepadInput[51] = { 0 };
  uint8_t _configuration_descriptor[41];
  PlatformMutex _mutex;
  uint8_t _mouseButtons;
};

}  // namespace arduino

#endif  // COMPOSITE_GAMEPAD_MOUSE_H
