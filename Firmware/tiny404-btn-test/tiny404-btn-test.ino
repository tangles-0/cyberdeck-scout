static const uint8_t PWR_BUTTON_PIN = PIN_PA5;
static const uint8_t SHIP_BUTTON_PIN = PIN_PA6;
static const uint8_t PWR_CONTROL_PIN = PIN_PA3;
static const uint8_t LED_PIN = PIN_PA4;
static const uint8_t CM5_SOFT_PWR_OUT = PIN_PA7;
static const uint8_t BATT_LEVEL1_LED_PIN = PIN_PB3;
static const uint8_t BATT_LEVEL2_LED_PIN = PIN_PB2;
static const uint8_t BATT_LEVEL3_LED_PIN = PIN_PB1;
static const uint8_t BATT_LEVEL4_LED_PIN = PIN_PB0;

void setup() {

  pinModeFast(PWR_BUTTON_PIN, INPUT_PULLUP);
  pinModeFast(SHIP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinModeFast(BATT_LEVEL1_LED_PIN, OUTPUT);
  pinModeFast(BATT_LEVEL2_LED_PIN, OUTPUT);
  pinModeFast(BATT_LEVEL3_LED_PIN, OUTPUT);
  pinModeFast(BATT_LEVEL4_LED_PIN, OUTPUT);
  pinModeFast(PWR_CONTROL_PIN, OUTPUT);
  pinModeFast(CM5_SOFT_PWR_OUT, OUTPUT);
  digitalWrite(PWR_CONTROL_PIN, LOW);
}

int batt_led = 0;
unsigned int long batt_led_time = 0;
void loop() {
  // put your main code here, to run repeatedly:
  uint8_t pwr_btn = digitalRead(PWR_BUTTON_PIN) ? 0 : 1;
  digitalWrite(LED_PIN, pwr_btn);

  if (pwr_btn) {
    digitalWrite(BATT_LEVEL1_LED_PIN, batt_led != 0);
    digitalWrite(BATT_LEVEL2_LED_PIN, batt_led != 1);
    digitalWrite(BATT_LEVEL3_LED_PIN, batt_led != 2);
    digitalWrite(BATT_LEVEL4_LED_PIN, batt_led != 3);
    if (millis() - batt_led_time > 100) {
      batt_led++;
      if (batt_led == 4) {
        batt_led = 0;
      }
      batt_led_time = millis();
    }
  } else {
    batt_led = 0;
    digitalWrite(BATT_LEVEL1_LED_PIN, HIGH);
    digitalWrite(BATT_LEVEL2_LED_PIN, HIGH);
    digitalWrite(BATT_LEVEL3_LED_PIN, HIGH);
    digitalWrite(BATT_LEVEL4_LED_PIN, HIGH);
  }
}

