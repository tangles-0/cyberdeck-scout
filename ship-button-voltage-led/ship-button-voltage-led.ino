// ATtiny1604 diagnostic: map PA3 voltage to PA5 LED brightness.
// Requires megaTinyCore. PA5 LED is active-low in this hardware.

#ifndef PIN_PA3
#define PIN_PA3 10
#endif

#ifndef PIN_PA5
#define PIN_PA5 1
#endif

static const uint8_t SHIP_BUTTON_PIN = PIN_PA3;
static const uint8_t LED_PIN = PIN_PA5;

void setup() {
  pinMode(SHIP_BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  const uint16_t raw = analogRead(SHIP_BUTTON_PIN);
  const uint8_t brightness = raw >> 2;
  analogWrite(LED_PIN, 255 - brightness);
}
