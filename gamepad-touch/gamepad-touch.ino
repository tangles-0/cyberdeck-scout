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

#include "CompositeHIDDevice.h"
#include "LittleFileSystem.h"
#include "mbed.h"
#include "BlockDevice.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define ENABLE_SERIAL_DIAGNOSTICS
//#define DISABLE_GAMEPAD_REPORTS  // Bench-test mode: uncomment when the gamepad controls are disconnected.
//#define DISABLE_MOUSE_REPORTS    // Bench-test mode: uncomment when the mouse controls are disconnected.
//#define DISABLE_FLASH            // Bench-test mode: uncomment to skip filesystem mode/calibration reads.

#define MODE_SWAP_BTN_HOLD_TIME 1000
#define MOUSE_SWAP_BTN_HOLD_TIME 50

#define DISABLE_JOYSTICK

#define MOUSE_DPAD_ACCEL 0.1
#define MOUSE_DPAD_BASE_SPEED 10
#define MOUSE_JOYSTICK_SPEED 30 

#define FORCE_REFORMAT false
//#define USE_MULTIPLEXER // if using a multiplexer chip for more analog inputs. tested successfully on prototype board
//#define DISABLE_FLASH

// ignored if USE_MULTIPLEXER is defined
#define yAxisPin 26
#define xAxisPin 27

// ignored if USE_MULTIPLEXER is not defined
#define adc0Pin 26
#define mpxS0Pin 18
#define mpxS1Pin 19
#define mpxS2Pin 20

#define GAMEPAD_X 8
#define GAMEPAD_Y 9
#define GAMEPAD_SELECT 11
#define GAMEPAD_START 10
#define GAMEPAD_HAT_DOWN 12
#define GAMEPAD_HAT_LEFT 13
#define GAMEPAD_HAT_UP 14
#define GAMEPAD_HAT_RIGHT 15
#define GAMEPAD_RB 21
#define GAMEPAD_LB 22
#define GAMEPAD_JOY 20

#define CALIBRATION_HOLD_BTN GAMEPAD_SELECT
#define CALIBRATION_HOLD_TIME 3000

// determines button report order
int buttonPins[] = {
  GAMEPAD_A,
  GAMEPAD_B,
  GAMEPAD_X,
  GAMEPAD_Y,
  GAMEPAD_LB,
  GAMEPAD_RB,
  GAMEPAD_START,
  GAMEPAD_SELECT,
  GAMEPAD_HAT_UP,
  GAMEPAD_HAT_DOWN,
  GAMEPAD_HAT_LEFT,
  GAMEPAD_HAT_RIGHT,
#ifndef DISABLE_JOYSTICK
  GAMEPAD_JOY
#endif
};
#define modeSwapPin 16 
#define leftClickPin GAMEPAD_A
#define rightClickPin GAMEPAD_B
#define backButtonPin GAMEPAD_X
#define middleClickPin GAMEPAD_Y

#define GAMEPAD_AXIS_LIMIT 32767
//#define GAMEPAD_AXIS_LIMIT 4096
#define MOUSE_DEADZONE 15

mbed::BlockDevice *bd = mbed::BlockDevice::get_default_instance();
static mbed::LittleFileSystem fs("fs");

static inline int16_t applyMouseCurve(int16_t val) {
  // Square-law curve keeps precise low-speed motion yet preserves max range
  int32_t magnitude = (val >= 0) ? val : -val;
  // cubic: val * |val|^2 keeps output tiny until the stick is near full throw
  int32_t curved = (static_cast<int32_t>(val) * magnitude * magnitude * magnitude) / (MOUSE_JOYSTICK_SPEED * MOUSE_JOYSTICK_SPEED * MOUSE_JOYSTICK_SPEED);
  return static_cast<int16_t>(curved);
}

CompositeHIDDevice hid;

static constexpr uint8_t FT5206_ADDR = 0x38;
static constexpr uint8_t FT5206_REG_DEVICE_MODE = 0x00;
static constexpr uint8_t FT5206_REG_TD_STATUS = 0x02;
static constexpr uint8_t FT5206_REG_CHIP_ID = 0xA3;
static constexpr uint8_t FT5206_REG_G_MODE = 0xA4;
static constexpr uint8_t FT5206_REG_FIRMWARE_ID = 0xA6;
static constexpr uint8_t FT5206_REG_VENDOR_ID = 0xA8;
static constexpr uint8_t FT5206_TOUCH_BYTES = 6;

static constexpr uint8_t GT911_ADDR = 0x14;
static constexpr uint16_t GT911_REG_PRODUCT_ID = 0x8140;
static constexpr uint16_t GT911_REG_CONFIG_VERSION = 0x8047;
static constexpr uint16_t GT911_REG_RESOLUTION = 0x8048;
static constexpr uint16_t GT911_REG_STATUS = 0x814E;
static constexpr uint16_t GT911_REG_POINT_DATA = 0x8150;
static constexpr uint8_t GT911_POINT_BYTES = 8;

static constexpr uint32_t TOUCH_I2C_CLOCK_HZ = 400000;
static constexpr uint32_t TOUCH_I2C_TIMEOUT_US = 25000;
static constexpr uint16_t TOUCH_POLL_INTERVAL_MS = 30;
static constexpr uint16_t TOUCH_DIAG_INTERVAL_MS = 500;
static constexpr uint16_t TOUCH_STARTUP_IGNORE_MS = 2500;
static constexpr uint8_t TOUCH_RESET_AFTER_FAILURES = 8;
static constexpr uint16_t TOUCH_RELEASE_TIMEOUT_MS = 150;
static constexpr uint8_t TOUCH_READ_RETRIES = 3;
static constexpr uint8_t TOUCH_RETRY_DELAY_MS = 2;
static constexpr uint8_t TOUCH_BAD_PACKET_RESET_THRESHOLD = 4;
static constexpr uint8_t TOUCH_INT_ACTIVE = LOW;

bool isInMouseMode = false;
unsigned long lastCalibTime = 0;
unsigned long lastTouchPollTime = 0;
unsigned long lastTouchDiagTime = 0;
unsigned long lastTouchActivityTime = 0;
bool touchWasActive = false;
uint8_t lastTouchDiagContactCount = 0xFF;
bool lastTouchReadOk = true;
uint8_t consecutiveTouchReadFailures = 0;
uint8_t consecutiveBadTouchPackets = 0;
bool lastTouchFailureWasBadPacket = false;
unsigned long touchIgnoreUntil = 0;
volatile bool touchInterruptPending = false;

void onTouchInterrupt() {
  touchInterruptPending = true;
}

void diagPrintln(const char *message) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
  Serial.println(message);
#else
  (void)message;
#endif
}

void diagPrintTouchContacts(const HIDTouchContact contacts[], uint8_t contactCount) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
  Serial.print(TOUCH_CONTROLLER_NAME);
  Serial.print(" touches: ");
  Serial.println(contactCount);
  for (uint8_t i = 0; i < contactCount; i++) {
    Serial.print("  id=");
    Serial.print(contacts[i].id);
    Serial.print(" x=");
    Serial.print(contacts[i].x);
    Serial.print(" y=");
    Serial.println(contacts[i].y);
  }
#else
  (void)contacts;
  (void)contactCount;
#endif
}

void initTouchI2CBus() {
  i2c_deinit(i2c0);
  _i2c_init(i2c0, TOUCH_I2C_CLOCK_HZ);
  gpio_set_function(DISP_SDA, GPIO_FUNC_I2C);
  gpio_set_function(DISP_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(DISP_SDA);
  gpio_pull_up(DISP_SCL);
}

void recoverTouchI2CBus() {
  initTouchI2CBus();
}

bool ft5206WriteReg(uint8_t reg, uint8_t value) {
  uint8_t data[] = { reg, value };
  const int status = i2c_write_timeout_us(i2c0, FT5206_ADDR, data, sizeof(data), false, TOUCH_I2C_TIMEOUT_US);
#ifdef ENABLE_SERIAL_DIAGNOSTICS
  Serial.print("FT5206 write reg 0x");
  Serial.print(reg, HEX);
  Serial.print(" = 0x");
  Serial.print(value, HEX);
  Serial.println(status == (int)sizeof(data) ? " OK" : " failed");
#endif
  if (status < 0) {
    recoverTouchI2CBus();
  }
  return status == (int)sizeof(data);
}

bool ft5206ReadReg(uint8_t reg, uint8_t &value) {
  int status = i2c_write_timeout_us(i2c0, FT5206_ADDR, &reg, 1, true, TOUCH_I2C_TIMEOUT_US);
  if (status != 1) {
    if (status < 0) {
      recoverTouchI2CBus();
    }
    return false;
  }
  status = i2c_read_timeout_us(i2c0, FT5206_ADDR, &value, 1, false, TOUCH_I2C_TIMEOUT_US);
  if (status != 1) {
    if (status < 0) {
      recoverTouchI2CBus();
    }
    return false;
  }
  return true;
}

bool touchWriteReg16(uint8_t addr, uint16_t reg, const uint8_t *data, size_t len) {
  uint8_t buf[2 + 8] = { 0 };
  if (len > sizeof(buf) - 2) {
    return false;
  }
  buf[0] = MSB(reg);
  buf[1] = LSB(reg);
  memcpy(buf + 2, data, len);
  const int status = i2c_write_timeout_us(i2c0, addr, buf, len + 2, false, TOUCH_I2C_TIMEOUT_US);
  if (status < 0) {
    recoverTouchI2CBus();
  }
  return status == (int)(len + 2);
}

bool touchWriteReg16(uint8_t addr, uint16_t reg, uint8_t value) {
  return touchWriteReg16(addr, reg, &value, 1);
}

bool touchReadReg16(uint8_t addr, uint16_t reg, uint8_t *data, size_t len) {
  uint8_t regBytes[] = { MSB(reg), LSB(reg) };
  int status = i2c_write_timeout_us(i2c0, addr, regBytes, sizeof(regBytes), true, TOUCH_I2C_TIMEOUT_US);
  if (status != (int)sizeof(regBytes)) {
    if (status < 0) {
      recoverTouchI2CBus();
    }
    return false;
  }
  status = i2c_read_timeout_us(i2c0, addr, data, len, false, TOUCH_I2C_TIMEOUT_US);
  if (status < 0) {
    recoverTouchI2CBus();
  }
  return status == (int)len;
}

void scanTouchI2CBus() {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
  Serial.print("I2C scan:");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 0x7F; addr++) {
    uint8_t dummy = 0;
    if (i2c_read_timeout_us(i2c0, addr, &dummy, 1, false, TOUCH_I2C_TIMEOUT_US) == 1) {
      Serial.print(" 0x");
      if (addr < 0x10) {
        Serial.print("0");
      }
      Serial.print(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.print(" no devices");
  }
  Serial.println();
#endif
}

void diagPrintTouchReg(const char *name, uint8_t reg) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
  uint8_t value = 0;
  Serial.print("FT5206 ");
  Serial.print(name);
  Serial.print(" (0x");
  Serial.print(reg, HEX);
  Serial.print("): ");
  if (ft5206ReadReg(reg, value)) {
    Serial.print("0x");
    if (value < 0x10) {
      Serial.print("0");
    }
    Serial.println(value, HEX);
  } else {
    Serial.println("read failed");
  }
#else
  (void)name;
  (void)reg;
#endif
}

void resetTouchController() {
  digitalWrite(DISP_RESET, LOW);
  delay(10);
  digitalWrite(DISP_RESET, HIGH);
  delay(300);
  touchIgnoreUntil = millis() + TOUCH_STARTUP_IGNORE_MS;
  lastTouchActivityTime = millis();
  lastTouchDiagContactCount = 0xFF;
}

bool configureTouchController() {
  bool modeOk = ft5206WriteReg(FT5206_REG_DEVICE_MODE, 0x00);  // Normal operating mode
  bool irqOk = ft5206WriteReg(FT5206_REG_G_MODE, 0x01);         // Trigger mode: /INT indicates touch data.
  if (modeOk && irqOk) {
    diagPrintln("FT5206 setup: controller acknowledged configuration");
  } else {
    diagPrintln("FT5206 setup: controller did not acknowledge one or more writes");
  }
  return modeOk && irqOk;
}

void resetAndConfigureTouchController(const char *reason) {
  diagPrintln(reason);
  if (touchWasActive) {
    HIDTouchContact contacts[TOUCH_MAX_CONTACTS] = {};
    hid.sendTouch(contacts, 0);
    touchWasActive = false;
  }
#if defined(TOUCH_CONTROLLER_GT911)
  gt911ResetController();
#else
  resetTouchController();
  configureTouchController();
#endif
  consecutiveTouchReadFailures = 0;
  consecutiveBadTouchPackets = 0;
  lastTouchReadOk = true;
  touchInterruptPending = false;
}

bool ft5206ReadTouches(HIDTouchContact contacts[], uint8_t &contactCount) {
  lastTouchFailureWasBadPacket = false;
  uint8_t reg = FT5206_REG_TD_STATUS;
  int status = i2c_write_timeout_us(i2c0, FT5206_ADDR, &reg, 1, true, TOUCH_I2C_TIMEOUT_US);
  if (status != 1) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
    if (lastTouchReadOk || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
      Serial.print("FT5206 read address/register failed, status=");
      Serial.println(status);
      lastTouchDiagTime = millis();
    }
#endif
    if (status < 0) {
      recoverTouchI2CBus();
    }
    return false;
  }

  const uint8_t bytesToRead = 1 + (TOUCH_MAX_CONTACTS * FT5206_TOUCH_BYTES);
  uint8_t touchData[1 + (TOUCH_MAX_CONTACTS * FT5206_TOUCH_BYTES)] = {};
  const int bytesRead = i2c_read_timeout_us(i2c0, FT5206_ADDR, touchData, bytesToRead, false, TOUCH_I2C_TIMEOUT_US);
  if (bytesRead != bytesToRead) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
    if (lastTouchReadOk || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
      Serial.print(bytesRead < 0 ? "FT5206 touch burst timeout/error: " : "FT5206 touch burst short read: ");
      Serial.print(bytesRead);
      Serial.print("/");
      Serial.println(bytesToRead);
      lastTouchDiagTime = millis();
    }
#endif
    if (bytesRead < 0) {
      recoverTouchI2CBus();
    }
    return false;
  }

  const uint8_t statusByte = touchData[0];
  uint8_t touched = statusByte & 0x0F;
  if ((statusByte & 0xF0) != 0) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
    if (lastTouchReadOk || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
      Serial.print("FT5206 invalid TD_STATUS: 0x");
      Serial.println(statusByte, HEX);
      lastTouchDiagTime = millis();
    }
#endif
    return false;
  }
  if (touched > TOUCH_MAX_CONTACTS) {
    lastTouchFailureWasBadPacket = true;
    return false;
  }

  contactCount = 0;
  if (touched == 0) {
    return true;
  }

  for (uint8_t i = 0; i < touched; i++) {
    const uint8_t offset = 1 + (i * FT5206_TOUCH_BYTES);
    const uint8_t xh = touchData[offset + 0];
    const uint8_t xl = touchData[offset + 1];
    const uint8_t yh = touchData[offset + 2];
    const uint8_t yl = touchData[offset + 3];

    const uint8_t event = xh >> 6;
    if (event == 0x03) {
      lastTouchFailureWasBadPacket = true;
#ifdef ENABLE_SERIAL_DIAGNOSTICS
      if (lastTouchReadOk || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
        Serial.println("FT5206 reserved touch event flag; rejecting packet");
        lastTouchDiagTime = millis();
      }
#endif
      return false;
    }
    if (event == 0x01) {
      continue;  // Up/release event; the contact count report below clears it.
    }

    const uint8_t touchId = yh >> 4;
    const uint16_t x = ((uint16_t)(xh & 0x0F) << 8) | xl;
    const uint16_t y = ((uint16_t)(yh & 0x0F) << 8) | yl;
    if (touchId == 0x0F || x > TOUCH_LOGICAL_MAX_X || y > TOUCH_LOGICAL_MAX_Y) {
      lastTouchFailureWasBadPacket = true;
#ifdef ENABLE_SERIAL_DIAGNOSTICS
      if (lastTouchReadOk || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
        Serial.println("FT5206 invalid touch record; rejecting packet");
        lastTouchDiagTime = millis();
      }
#endif
      return false;
    }

    contacts[contactCount].active = true;
    contacts[contactCount].id = touchId;
    contacts[contactCount].x = x;
    contacts[contactCount].y = y;
    contactCount++;
  }

  return true;
}

bool ft5206ReadTouchesWithRetry(HIDTouchContact contacts[], uint8_t &contactCount) {
  for (uint8_t attempt = 0; attempt < TOUCH_READ_RETRIES; attempt++) {
    contactCount = 0;
    if (ft5206ReadTouches(contacts, contactCount)) {
      if (attempt > 0) {
        diagPrintln("FT5206 read succeeded after retry");
      }
      return true;
    }
    delay(TOUCH_RETRY_DELAY_MS);
  }
  return false;
}

void gt911ResetController() {
  pinMode(DISP_INT, INPUT_PULLUP);  // Pull high during reset selects GT911 I2C address 0x14 on this module.
  pinMode(DISP_RESET, OUTPUT);
  digitalWrite(DISP_RESET, LOW);
  delay(40);
  digitalWrite(DISP_RESET, HIGH);
  delay(200);
  pinMode(DISP_INT, INPUT_PULLUP);
  touchIgnoreUntil = millis() + TOUCH_STARTUP_IGNORE_MS;
  lastTouchActivityTime = millis();
  lastTouchDiagContactCount = 0xFF;
}

void gt911MapToLandscape(uint16_t rawX, uint16_t rawY, uint16_t &x, uint16_t &y) {
  // GT911 config reports the 720x1280 portrait sensor. The display is mounted as 1280x720 landscape.
  x = rawY;
  y = rawX;
  if (x > TOUCH_LOGICAL_MAX_X) {
    x = TOUCH_LOGICAL_MAX_X;
  }
  if (y > TOUCH_LOGICAL_MAX_Y) {
    y = TOUCH_LOGICAL_MAX_Y;
  }
}

bool gt911ReadTouches(HIDTouchContact contacts[], uint8_t &contactCount) {
  uint8_t status = 0;
  if (!touchReadReg16(GT911_ADDR, GT911_REG_STATUS, &status, 1)) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
    if (lastTouchReadOk || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
      Serial.println("GT911 status read failed");
      lastTouchDiagTime = millis();
    }
#endif
    return false;
  }

  if ((status & 0x80) == 0) {
    contactCount = 0;
    return true;
  }

  const uint8_t touched = status & 0x0F;
  if (touched > TOUCH_MAX_CONTACTS) {
    touchWriteReg16(GT911_ADDR, GT911_REG_STATUS, (uint8_t)0x00);
    lastTouchFailureWasBadPacket = true;
    return false;
  }

  contactCount = 0;
  if (touched > 0) {
    uint8_t touchData[TOUCH_MAX_CONTACTS * GT911_POINT_BYTES] = { 0 };
    if (!touchReadReg16(GT911_ADDR, GT911_REG_POINT_DATA, touchData, touched * GT911_POINT_BYTES)) {
#ifdef ENABLE_SERIAL_DIAGNOSTICS
      if (lastTouchReadOk || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
        Serial.println("GT911 point data read failed");
        lastTouchDiagTime = millis();
      }
#endif
      return false;
    }

    for (uint8_t i = 0; i < touched; i++) {
      const uint8_t offset = i * GT911_POINT_BYTES;
      const uint8_t touchId = i;
      const uint16_t rawX = ((uint16_t)touchData[offset + 1] << 8) | touchData[offset + 0];
      const uint16_t rawY = ((uint16_t)touchData[offset + 3] << 8) | touchData[offset + 2];
      uint16_t x = 0;
      uint16_t y = 0;
      gt911MapToLandscape(rawX, rawY, x, y);

      contacts[contactCount].active = true;
      contacts[contactCount].id = touchId;
      contacts[contactCount].x = x;
      contacts[contactCount].y = y;
      contactCount++;
    }
  }

  touchWriteReg16(GT911_ADDR, GT911_REG_STATUS, (uint8_t)0x00);
  return true;
}

bool gt911ReadTouchesWithRetry(HIDTouchContact contacts[], uint8_t &contactCount) {
  for (uint8_t attempt = 0; attempt < TOUCH_READ_RETRIES; attempt++) {
    contactCount = 0;
    if (gt911ReadTouches(contacts, contactCount)) {
      if (attempt > 0) {
        diagPrintln("GT911 read succeeded after retry");
      }
      return true;
    }
    delay(TOUCH_RETRY_DELAY_MS);
  }
  return false;
}

void setupTouchController() {
#if defined(TOUCH_CONTROLLER_GT911)
  diagPrintln("GT911 setup: resetting controller");
  initTouchI2CBus();
  gt911ResetController();
  scanTouchI2CBus();

  uint8_t productId[5] = { 0 };
  if (touchReadReg16(GT911_ADDR, GT911_REG_PRODUCT_ID, productId, 4)) {
    productId[4] = 0;
    Serial.print("GT911 product ID: ");
    Serial.println((char *)productId);
  } else {
    diagPrintln("GT911 product ID read failed");
  }

  uint8_t cfgVersion = 0;
  if (touchReadReg16(GT911_ADDR, GT911_REG_CONFIG_VERSION, &cfgVersion, 1)) {
    Serial.print("GT911 config version: 0x");
    Serial.println(cfgVersion, HEX);
  }

  uint8_t resolution[4] = { 0 };
  if (touchReadReg16(GT911_ADDR, GT911_REG_RESOLUTION, resolution, sizeof(resolution))) {
    Serial.print("GT911 raw resolution: ");
    Serial.print(((uint16_t)resolution[1] << 8) | resolution[0]);
    Serial.print("x");
    Serial.println(((uint16_t)resolution[3] << 8) | resolution[2]);
  }

  touchInterruptPending = false;
  attachInterrupt(digitalPinToInterrupt(DISP_INT), onTouchInterrupt, FALLING);
  diagPrintln("GT911 setup: INT trigger enabled");
  return;
#endif

  diagPrintln("FT5206 setup: resetting controller");
  pinMode(DISP_INT, INPUT_PULLUP);
  pinMode(DISP_RESET, OUTPUT);
  digitalWrite(DISP_RESET, HIGH);

  initTouchI2CBus();
#ifdef ENABLE_SERIAL_DIAGNOSTICS
  Serial.print("FT5206 setup: i2c0 started on Pico GPIO4 SDA / GPIO5 SCL at ");
  Serial.print(TOUCH_I2C_CLOCK_HZ);
  Serial.println("Hz");
#endif

  resetTouchController();
  scanTouchI2CBus();
  diagPrintTouchReg("device mode", FT5206_REG_DEVICE_MODE);
  diagPrintTouchReg("chip id", FT5206_REG_CHIP_ID);
  diagPrintTouchReg("firmware id", FT5206_REG_FIRMWARE_ID);
  diagPrintTouchReg("vendor id", FT5206_REG_VENDOR_ID);

  configureTouchController();

  touchInterruptPending = false;
  attachInterrupt(digitalPinToInterrupt(DISP_INT), onTouchInterrupt, FALLING);
  diagPrintln("FT5206 setup: INT trigger enabled");
}

void pollTouchController() {
#if defined(TOUCH_CONTROLLER_GT911)
  if (millis() - lastTouchPollTime < TOUCH_POLL_INTERVAL_MS) {
    return;
  }
  lastTouchPollTime = millis();

  HIDTouchContact contacts[TOUCH_MAX_CONTACTS] = {};
  uint8_t contactCount = 0;

  const bool intAsserted = digitalRead(DISP_INT) == TOUCH_INT_ACTIVE;
  const bool intPending = touchInterruptPending;
  if (!intAsserted && !intPending) {
    if (touchWasActive && millis() - lastTouchActivityTime >= TOUCH_RELEASE_TIMEOUT_MS) {
      hid.sendTouch(contacts, 0);
      touchWasActive = false;
      lastTouchDiagContactCount = 0;
      diagPrintTouchContacts(contacts, 0);
    }
    return;
  }

  if (intPending) {
    touchInterruptPending = false;
  }

  if (gt911ReadTouchesWithRetry(contacts, contactCount)) {
    consecutiveTouchReadFailures = 0;
    lastTouchReadOk = true;

    if (millis() < touchIgnoreUntil) {
      if (contactCount > 0 && millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
        diagPrintln("GT911 startup touch data ignored");
        diagPrintTouchContacts(contacts, contactCount);
        lastTouchDiagTime = millis();
      }
      return;
    }

    if (contactCount > 0 || touchWasActive) {
      hid.sendTouch(contacts, contactCount);
    }
    if (contactCount > 0) {
      lastTouchActivityTime = millis();
    }
    if (contactCount != lastTouchDiagContactCount || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
      diagPrintTouchContacts(contacts, contactCount);
      lastTouchDiagContactCount = contactCount;
      lastTouchDiagTime = millis();
    }
    touchWasActive = contactCount > 0;
    return;
  }

  if (intPending || intAsserted) {
    touchInterruptPending = true;
  }
  consecutiveTouchReadFailures++;
  lastTouchReadOk = false;
  if (consecutiveTouchReadFailures >= TOUCH_RESET_AFTER_FAILURES) {
    resetAndConfigureTouchController("GT911 read failures exceeded threshold; resetting controller");
  }
  return;
#endif

  if (millis() - lastTouchPollTime < TOUCH_POLL_INTERVAL_MS) {
    return;
  }

  if (ft5206ReadTouchesWithRetry(contacts, contactCount)) {
    if (!lastTouchReadOk && consecutiveTouchReadFailures >= TOUCH_RESET_AFTER_FAILURES) {
      diagPrintln("FT5206 read recovered");
    }
    consecutiveTouchReadFailures = 0;
    lastTouchReadOk = true;

    if (millis() < touchIgnoreUntil) {
      if (contactCount > 0 && millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
        diagPrintln("FT5206 startup touch data ignored");
        diagPrintTouchContacts(contacts, contactCount);
        lastTouchDiagTime = millis();
      }
      return;
    }

    if (contactCount > 0 || touchWasActive) {
      hid.sendTouch(contacts, contactCount);
    }
    if (contactCount > 0) {
      lastTouchActivityTime = millis();
    }
    if (contactCount != lastTouchDiagContactCount || millis() - lastTouchDiagTime >= TOUCH_DIAG_INTERVAL_MS) {
      diagPrintTouchContacts(contacts, contactCount);
      lastTouchDiagContactCount = contactCount;
      lastTouchDiagTime = millis();
    }
    touchWasActive = contactCount > 0;
    return;
  }

  if (intPending || intAsserted) {
    touchInterruptPending = true;
  }
  consecutiveTouchReadFailures++;
  if (lastTouchFailureWasBadPacket) {
    consecutiveBadTouchPackets++;
  }
  lastTouchReadOk = false;
  if (consecutiveTouchReadFailures >= TOUCH_RESET_AFTER_FAILURES || consecutiveBadTouchPackets >= TOUCH_BAD_PACKET_RESET_THRESHOLD) {
    resetAndConfigureTouchController("FT5206 read failures exceeded threshold; resetting controller");
  }
}

struct Calibration {
  int minXCal;
  int maxXCal;
  int minYCal;
  int maxYCal;
};

static const char *MODE_PATH = "/fs/mode.bin";
static const char *CALIB_PATH = "/fs/calib.bin";
Calibration calib = { 135, 950, 135, 950 };


// Binary file helpers
bool writeFileBin(const char *filename, const void *data, size_t size) {
  FILE *f = fopen(filename, "wb");
  if (!f) {
    Serial.print("Failed to open for write: ");
    Serial.println(filename);
    return false;
  }
  size_t w = fwrite(data, 1, size, f);
  fclose(f);
  return w == size;
}

size_t readFileBin(const char *filename, void *buf, size_t size) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    return 0;
  }
  size_t n = fread(buf, 1, size, f);
  fclose(f);
  return n;
}

void writeFile(const char *filename, const char *text) {
  Serial.print("Write to \"");
  Serial.print(filename);
  Serial.println("\"... ");
  FILE *f = fopen(filename, "wb");
  if (!f) {
    Serial.println("File not found");
    return;
  }
  size_t w = fwrite(text, 1, strlen(text), f);
  Serial.print("Wrote ");
  Serial.print(w);
  Serial.println(" chars.");
  fclose(f);
}

size_t readFile(const char *filename, char *buf, size_t size) {
  Serial.print("Read \"");
  Serial.print(filename);
  Serial.println("\"... ");
  if (size == 0) {
    return 0;
  }
  FILE *f = fopen(filename, "rb");
  if (!f) {
    buf[0] = 0;
    return 0;
  }
  size_t n = fread(buf, 1, size - 1, f);
  buf[n] = 0;
  fclose(f);
  return n;
}


void unmountFs() {
  int err = fs.unmount();
  if (err < 0) {
    Serial.println("Fail unmounting filesystem");
    Serial.println(strerror(-err));
  }
}

void mountFs() {
  Serial.print("Mounting filesystem /fs/... ");
  int err = fs.mount(bd);
  Serial.println((err ? "Fail :(" : "OK!"));
  if (err || FORCE_REFORMAT) {
    Serial.println("Formatting... ");
    err = fs.reformat(bd);
    Serial.println((err ? "Fail :(" : "OK!"));
  }
}

void setUpModeState() {
  mountFs();
  char out[16];
  size_t n = readFile(MODE_PATH, out, sizeof(out));
  if (n == 0) {
    Serial.println("mode.bin missing, defaulting to gamepad");
    writeFile(MODE_PATH, "gamepad");
    strncpy(out, "gamepad", sizeof(out));
    out[sizeof(out) - 1] = 0;
  }
  Serial.print("Mode set: ");
  Serial.println(out);
  isInMouseMode = strcmp(out, "mouse") == 0;
  unmountFs();
}

void changeMode() {
  isInMouseMode = !isInMouseMode;
  #ifndef DISABLE_FLASH
  mountFs();
  writeFile(MODE_PATH, isInMouseMode ? "mouse" : "gamepad");
  unmountFs();
  #endif
  Serial.println("Mode swapped to HID " + String(isInMouseMode ? "Mouse" : "GamePad"));
}

void loadCalibration() {
  mountFs();
  Calibration tmp;
  size_t n = readFileBin(CALIB_PATH, &tmp, sizeof(tmp));
  if (n == sizeof(tmp)) {
    // Basic sanity
    if (tmp.maxXCal > tmp.minXCal && tmp.maxYCal > tmp.minYCal) {
      calib = tmp;
    }
  } else {
    // Write defaults on first boot
    writeFileBin(CALIB_PATH, &calib, sizeof(calib));
  }
  unmountFs();
}

void saveCalibration() {
  mountFs();
  writeFileBin(CALIB_PATH, &calib, sizeof(calib));
  unmountFs();
}

void blinkLed(int times, int onMs = 100, int offMs = 100) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(onMs);
    digitalWrite(LED_BUILTIN, LOW);
    delay(offMs);
  }
}

void runCalibration() {
  Serial.println("Calibration: starting. Move stick to all extremes...");
  blinkLed(3, 150, 150);
  unsigned long start = millis();
  Calibration tmpCalib = { 4095, 0, 4095, 0 };
  while (millis() - start < 8000) {
#ifdef USE_MULTIPLEXER
    digitalWrite(mpxS0Pin, LOW);
    int x = analogRead(adc0Pin);
    digitalWrite(mpxS0Pin, HIGH);
    int y = analogRead(adc0Pin);
#else
    int x = analogRead(xAxisPin);
    int y = analogRead(yAxisPin);
#endif
    tmpCalib.minXCal = min(tmpCalib.minXCal, x);
    tmpCalib.maxXCal = max(tmpCalib.maxXCal, x);
    tmpCalib.minYCal = min(tmpCalib.minYCal, y);
    tmpCalib.maxYCal = max(tmpCalib.maxYCal, y);
    delay(5);
  }
  calib = tmpCalib;
  saveCalibration();
  Serial.print("Calibration saved - X:[");
  Serial.print(calib.minXCal);
  Serial.print(", ");
  Serial.print(calib.maxXCal);
  Serial.print("] Y:[");
  Serial.print(calib.minYCal);
  Serial.print(", ");
  Serial.print(calib.maxYCal);
  Serial.println("]");
  blinkLed(5, 60, 60);
}

void setup() {

  Serial.begin(115200);
  diagPrintln("Composite HID boot");
#ifdef DISABLE_GAMEPAD_REPORTS
  diagPrintln("Gamepad reports disabled");
#else
  diagPrintln("Gamepad reports enabled");
#endif
#ifdef DISABLE_MOUSE_REPORTS
  diagPrintln("Mouse reports disabled");
#else
  diagPrintln("Mouse reports enabled");
#endif

  for (unsigned int i = 0; i < sizeof(buttonPins) / sizeof(buttonPins[0]); i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  pinMode(modeSwapPin, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(mpxS0Pin, OUTPUT);
  pinMode(mpxS1Pin, OUTPUT);
  pinMode(mpxS2Pin, OUTPUT);
  digitalWrite(mpxS0Pin, LOW);
  digitalWrite(mpxS1Pin, LOW);
  digitalWrite(mpxS2Pin, LOW);

  setupTouchController();

  delay(2000);

  #ifndef DISABLE_FLASH
  setUpModeState();
  loadCalibration();
  #endif
}

unsigned int long lastModePressTime = 0;
bool lastModePressState = false;
bool mouseUseHatInsteadOfJoystick = true;
float mouseHatAccel = 0;

void modeLongPress() {
  changeMode();
  if (isInMouseMode) {
    mouseUseHatInsteadOfJoystick = true;
  }
}
void modeShortPress() {
  if (isInMouseMode) {
    #ifndef DISABLE_JOYSTICK
    mouseUseHatInsteadOfJoystick = !mouseUseHatInsteadOfJoystick;
    #endif
  }
}

void loop() {

  pollTouchController();

#if defined(DISABLE_GAMEPAD_REPORTS) && defined(DISABLE_MOUSE_REPORTS)
  delay(TOUCH_POLL_INTERVAL_MS);
  return;
#endif

  if (digitalRead(modeSwapPin) == LOW) {
    if (!lastModePressState) {
      lastModePressState = true;
      lastModePressTime = millis();
    }
  } else {
    if (lastModePressState) {
      if (millis() - lastModePressTime > MODE_SWAP_BTN_HOLD_TIME) {
        modeLongPress();
      } else if (millis() - lastModePressTime > MOUSE_SWAP_BTN_HOLD_TIME) {
        modeShortPress();
      }
    }
    lastModePressState = false;
  }

  // Use current calibration values
  int16_t minX = calib.minXCal;
  int16_t maxX = calib.maxXCal;
  int16_t minY = calib.minYCal;
  int16_t maxY = calib.maxYCal;

#ifdef USE_MULTIPLEXER
  digitalWrite(mpxS0Pin, LOW);
  int xAxisValue = analogRead(adc0Pin);
  digitalWrite(mpxS0Pin, HIGH);
  int yAxisValue = analogRead(adc0Pin);
#else
  int xAxisValue = analogRead(xAxisPin);
  int yAxisValue = analogRead(yAxisPin);
#endif

  if (xAxisValue > maxX) {
    xAxisValue = maxX;
  }
  if (xAxisValue < minX) {
    xAxisValue = minX;
  }
  if (yAxisValue > maxY) {
    yAxisValue = maxY;
  }
  if (yAxisValue < minY) {
    yAxisValue = minY;
  }

  if (isInMouseMode) {

#ifndef DISABLE_MOUSE_REPORTS
    int16_t valX = map(xAxisValue, minX, maxX, -MOUSE_JOYSTICK_SPEED, MOUSE_JOYSTICK_SPEED);
    int16_t valY = map(yAxisValue, minY, maxY, -MOUSE_JOYSTICK_SPEED, MOUSE_JOYSTICK_SPEED);

    // Apply small deadzone to prevent drift when near center
    if (valX > -MOUSE_DEADZONE && valX < MOUSE_DEADZONE) {
      valX = 0;
    }
    if (valY > -MOUSE_DEADZONE && valY < MOUSE_DEADZONE) {
      valY = 0;
    }

    // Curve the remaining range for finer low-speed motion
    int16_t curvedX = applyMouseCurve(valX);
    int16_t curvedY = applyMouseCurve(valY);

    // Map button pins to mouse buttons
    // bit0: Left, bit1: Right, bit2: Middle, bit3: Back (Button 4)
    uint8_t mouseButtons = 0;
    mouseButtons |= (!digitalRead(leftClickPin) ? 0x01 : 0x00);
    mouseButtons |= (!digitalRead(rightClickPin) ? 0x02 : 0x00);
    mouseButtons |= (!digitalRead(middleClickPin) ? 0x04 : 0x00);
    mouseButtons |= (!digitalRead(backButtonPin) ? 0x08 : 0x00);
    hid.setButtons(mouseButtons);

    if (mouseUseHatInsteadOfJoystick) {
      int xVel = 0;
      int yVel = 0;
      if (!digitalRead(GAMEPAD_HAT_LEFT)) xVel -= MOUSE_DPAD_BASE_SPEED;
      if (!digitalRead(GAMEPAD_HAT_RIGHT)) xVel += MOUSE_DPAD_BASE_SPEED;
      if (!digitalRead(GAMEPAD_HAT_UP)) yVel -= MOUSE_DPAD_BASE_SPEED;
      if (!digitalRead(GAMEPAD_HAT_DOWN)) yVel += MOUSE_DPAD_BASE_SPEED;
      if (xVel != 0 || yVel != 0) {
        mouseHatAccel += MOUSE_DPAD_ACCEL;
      } else {
        mouseHatAccel = 0;
      }
      int xVelAccel = xVel * mouseHatAccel;
      int yVelAccel = yVel * mouseHatAccel;
      hid.move(xVelAccel, yVelAccel, 0);
    } else {
      // Movement
      hid.move((int8_t)curvedX, (int8_t)curvedY, 0);
      if (!digitalRead(GAMEPAD_HAT_DOWN)) hid.move(0, 0, -1);  // scroll down
      else if (!digitalRead(GAMEPAD_HAT_UP)) hid.move(0, 0, 1);   // scroll up
    }

    // Example: Scrolling (uncomment to use)
    // hid.move(0, 0, 1);   // scroll up
    // hid.move(0, 0, -1);  // scroll down
#endif
    delay(15);

  } else {

#ifndef DISABLE_GAMEPAD_REPORTS
    // enter calibration mode when button is held
    #ifndef DISABLE_JOYSTICK
    if (digitalRead(CALIBRATION_HOLD_BTN) == LOW) {
      if (millis() - lastCalibTime > CALIBRATION_HOLD_TIME) {
        runCalibration();
        lastCalibTime = millis();
      }
    } else {
      lastCalibTime = millis();
    }
    

    int16_t valX = map(xAxisValue, minX, maxX, -GAMEPAD_AXIS_LIMIT, GAMEPAD_AXIS_LIMIT);
    int16_t valY = map(yAxisValue, minY, maxY, -GAMEPAD_AXIS_LIMIT, GAMEPAD_AXIS_LIMIT);

    hid.SetX(valX);
    hid.SetY(valY);
    #endif
    hid.SetHat(0, 8);
    for (unsigned int i = 0; i < sizeof(buttonPins) / sizeof(buttonPins[0]); i++) {
      hid.SetButton(i, !digitalRead(buttonPins[i]));
    }
    hid.send_update();
#endif
    delay(30);

  }
}
