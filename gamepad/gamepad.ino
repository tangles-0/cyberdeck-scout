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

#include "CompositeHIDDevice.h"
#include "LittleFileSystem.h"
#include "mbed.h"
#include "BlockDevice.h"

#define MODE_SWAP_BTN_HOLD_TIME 1000
#define MOUSE_SWAP_BTN_HOLD_TIME 50

//#define DISABLE_JOYSTICK

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

#define GAMEPAD_A 6
#define GAMEPAD_B 7
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

bool isInMouseMode = false;
unsigned long lastCalibTime = 0;

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
    delay(15);

  } else {

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
    delay(30);

  }
}
