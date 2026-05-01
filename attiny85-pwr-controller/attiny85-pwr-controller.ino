#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>

#define BUILTIN_LED 1
#define PWR_BTN 0
#define PWR_OUTPUT 2

//#define USE_SLEEP

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(PWR_BTN, INPUT_PULLUP);
  pinMode(PWR_OUTPUT, OUTPUT);
  digitalWrite(PWR_OUTPUT, LOW);
}

#ifdef USE_SLEEP
ISR(PCINT0_vect) {
  // Wake from sleep on any pin change.
}

static void sleepPowerDown() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  ADCSRA &= ~(1 << ADEN); // reduce power while sleeping

  GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PWR_BTN);
  GIFR |= (1 << PCIF); // clear pending pin-change interrupt

  sei();
  sleep_cpu();

  sleep_disable();
  ADCSRA |= (1 << ADEN);
}
#endif

unsigned int long lastRelease = 0;
unsigned int long lastPulse = 0;
bool btnState = false;
bool pwrState = false;

void loop() {
  bool pwr = digitalRead(PWR_BTN);
  if (!pwrState) {
    if (millis() - lastPulse > 2000) {
      lastPulse = millis();
      digitalWrite(BUILTIN_LED, HIGH);
      delay(5);
      digitalWrite(BUILTIN_LED, LOW);
    }
  }
  
  if (!pwr) {
    btnState = true;
  } else {
    //btn was released
    if (btnState == true) {
      btnState = false;
      if (millis() - lastRelease > 200) {
        lastRelease = millis();
        pwrState = !pwrState;
        digitalWrite(PWR_OUTPUT, pwrState);
        digitalWrite(BUILTIN_LED, pwrState);
      }
    }

    // sleep when button isn't pressed and power is off
    #ifdef USE_SLEEP
    if (!pwrState) {
      sleepPowerDown();
    }
    #endif
  }

}
