/*
 * Composite HID: Gamepad + Mouse + Touch Screen for Arduino Mbed RP2040
 *
 * Requires to be installed in board manager:
 *   "Arduino Mbed OS RP2040 Boards" by Arduino
 *   v4.5.0 (or greater)
 *
 * Presents a single HID interface to the USB host with three Report IDs:
 *  - Report ID 0x01: Mouse        (3 buttons + X/Y + Wheel)
 *  - Report ID 0x02: Gamepad      (compatible with PicoGamepad format)
 *  - Report ID 0x03: Touch Screen (5 absolute contacts)
 *
 * Only supports Arduino Mbed core (uses PluggableUSBHID).
 */

#ifndef COMPOSITE_GAMEPAD_MOUSE_H
#define COMPOSITE_GAMEPAD_MOUSE_H

#include "TouchDisplayConfig.h"
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
static constexpr uint8_t REPORT_ID_TOUCH = 0x03;
static constexpr uint8_t TOUCH_MAX_CONTACTS = 5;
static constexpr uint16_t TOUCH_LOGICAL_MAX_X = TOUCH_HID_LOGICAL_MAX_X;
static constexpr uint16_t TOUCH_LOGICAL_MAX_Y = TOUCH_HID_LOGICAL_MAX_Y;
static constexpr uint8_t TOUCH_CONTACT_PAYLOAD_LEN = 6;
static constexpr uint8_t TOUCH_PAYLOAD_LEN = (TOUCH_MAX_CONTACTS * TOUCH_CONTACT_PAYLOAD_LEN) + 1;
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

struct HIDTouchContact {
  bool active;
  uint8_t id;
  uint16_t x;
  uint16_t y;
};

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

  bool sendTouch(const HIDTouchContact contacts[], uint8_t contactCount) {
    _mutex.lock();
    HID_REPORT report;
    memset(report.data, 0, sizeof(report.data));
    report.data[0] = REPORT_ID_TOUCH;

    uint8_t currentIds[TOUCH_MAX_CONTACTS] = { 0 };
    uint16_t currentX[TOUCH_MAX_CONTACTS] = { 0 };
    uint16_t currentY[TOUCH_MAX_CONTACTS] = { 0 };
    uint8_t currentCount = 0;
    uint8_t reportContactCount = 0;
    uint8_t payloadOffset = 1;

    for (uint8_t i = 0; i < contactCount && currentCount < TOUCH_MAX_CONTACTS; i++) {
      if (!contacts[i].active) {
        continue;
      }
      bool duplicateId = false;
      for (uint8_t j = 0; j < currentCount; j++) {
        if (currentIds[j] == contacts[i].id) {
          duplicateId = true;
          break;
        }
      }
      if (duplicateId) {
        continue;
      }

      currentIds[currentCount] = contacts[i].id;
      currentX[currentCount] = contacts[i].x;
      currentY[currentCount] = contacts[i].y;
      currentCount++;
      report.data[payloadOffset + 0] = 0x03;  // Tip Switch + In Range
      report.data[payloadOffset + 1] = contacts[i].id;
      report.data[payloadOffset + 2] = LSB(contacts[i].x);
      report.data[payloadOffset + 3] = MSB(contacts[i].x);
      report.data[payloadOffset + 4] = LSB(contacts[i].y);
      report.data[payloadOffset + 5] = MSB(contacts[i].y);
      reportContactCount++;
      payloadOffset += TOUCH_CONTACT_PAYLOAD_LEN;
    }

    for (uint8_t i = 0; i < TOUCH_MAX_CONTACTS && payloadOffset < 1 + TOUCH_PAYLOAD_LEN - 1; i++) {
      if (!_touchPreviousActive[i]) {
        continue;
      }

      bool stillActive = false;
      for (uint8_t j = 0; j < currentCount; j++) {
        if (currentIds[j] == _touchPreviousIds[i]) {
          stillActive = true;
          break;
        }
      }
      if (stillActive) {
        continue;
      }

      // Send a zero-tip record for the exact contact ID that disappeared.
      report.data[payloadOffset + 0] = 0x00;
      report.data[payloadOffset + 1] = _touchPreviousIds[i];
      report.data[payloadOffset + 2] = LSB(_touchPreviousX[i]);
      report.data[payloadOffset + 3] = MSB(_touchPreviousX[i]);
      report.data[payloadOffset + 4] = LSB(_touchPreviousY[i]);
      report.data[payloadOffset + 5] = MSB(_touchPreviousY[i]);
      reportContactCount++;
      payloadOffset += TOUCH_CONTACT_PAYLOAD_LEN;
    }

    while (payloadOffset < 1 + TOUCH_PAYLOAD_LEN - 1) {
      report.data[payloadOffset + 0] = 0x00;
      report.data[payloadOffset + 1] = 0x00;
      payloadOffset += TOUCH_CONTACT_PAYLOAD_LEN;
    }

    for (uint8_t i = 0; i < TOUCH_MAX_CONTACTS; i++) {
      _touchPreviousActive[i] = i < currentCount;
      _touchPreviousIds[i] = i < currentCount ? currentIds[i] : 0;
      _touchPreviousX[i] = i < currentCount ? currentX[i] : 0;
      _touchPreviousY[i] = i < currentCount ? currentY[i] : 0;
    }

    report.data[payloadOffset] = reportContactCount;
    report.length = 1 + TOUCH_PAYLOAD_LEN;
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
  uint8_t _touchPreviousIds[TOUCH_MAX_CONTACTS] = { 0 };
  uint16_t _touchPreviousX[TOUCH_MAX_CONTACTS] = { 0 };
  uint16_t _touchPreviousY[TOUCH_MAX_CONTACTS] = { 0 };
  bool _touchPreviousActive[TOUCH_MAX_CONTACTS] = { false };
};

}  // namespace arduino

#endif  // COMPOSITE_GAMEPAD_MOUSE_H
