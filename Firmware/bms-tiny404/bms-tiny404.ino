// ATtiny404 BQ76905 host: configuration, power control, indicators, and host cell balancing.
// Requires megaTinyCore in Arduino IDE or arduino-cli.
// I2C to the BQ is bit-banged on PA1/PA2: the 404 (tinyAVR 0-series) has no
// alternate TWI pin position, and its hardware TWI pins (PB0/PB1) drive LEDs.

#define DEBUG 1
#define ENABLE_SHIP_BUTTON 0

// test swaps
static const uint8_t LED_PIN = PIN_PB3;
static const uint8_t BATT_LEVEL1_LED_PIN = PIN_PA4;

// rev 1b layout
static const uint8_t PWR_BUTTON_PIN = PIN_PA5;
static const uint8_t SHIP_BUTTON_PIN = PIN_PA6;
static const uint8_t PWR_CONTROL_PIN = PIN_PA3;
//static const uint8_t LED_PIN = PIN_PA4;
static const uint8_t CM5_SOFT_PWR_OUT = PIN_PA7;

//static const uint8_t BATT_LEVEL1_LED_PIN = PIN_PB3;
static const uint8_t BATT_LEVEL2_LED_PIN = PIN_PB2;
static const uint8_t BATT_LEVEL3_LED_PIN = PIN_PB1;
static const uint8_t BATT_LEVEL4_LED_PIN = PIN_PB0;

// BQ7690x I2C address
static const uint8_t BQ_ADDR = 0x08;

// Subcommand and data registers
static const uint8_t REG_SUBCMD = 0x3E;
static const uint8_t REG_SUBCMD_DATA = 0x60;
static const uint8_t REG_SAFETY_STATUS_A = 0x03;
static const uint8_t REG_BATTERY_STATUS = 0x12;
static const uint8_t REG_ALARM_RAW_STATUS = 0x64;
// static const uint8_t REG_FET_CONTROL = 0x68;
// static const uint8_t REGOUT_CONTROL = 0x69;
// static const uint8_t REGOUT_REG_EN = (1u << 3);

// Command-only subcommands
static const uint16_t SET_CFGUPDATE = 0x0090;
static const uint16_t EXIT_CFGUPDATE = 0x0092;
static const uint16_t DEEPSLEEP = 0x000F;
static const uint16_t EXIT_DEEPSLEEP = 0x000E;
#if ENABLE_SHIP_BUTTON
static const uint16_t SHUTDOWN = 0x0010;
#endif
// static const uint16_t FET_ENABLE = 0x0022;
// static const uint16_t SLEEP_DISABLE = 0x009A; // not needed - sleep mode is autonomous and desirable.

// Host-controlled balancing active-cells subcommand (requires payload bitmask).
// Required to be re-issued every ~18s; BQ aborts after ~20s for safety.
static const uint16_t START_BALANCE = 0x0083; 

// Configuration register addresses
static const uint16_t VcellMode = 0x901B;
static const uint16_t DefaultAlarmMask = 0x901C;
static const uint16_t FETOptions = 0x901E;
static const uint16_t EnabledProtectionsA = 0x9024;
static const uint16_t EnabledProtectionsB = 0x9025;
static const uint16_t BalancingConfiguration = 0x9020;
static const uint16_t BalancingMinTempThreshold = 0x9021;
static const uint16_t BalancingMaxTempThreshold = 0x9022;
static const uint16_t BalancingMaxInternalTemp = 0x9023;
static const uint16_t CellUndervoltageProtectionThreshold = 0x902E;
static const uint16_t CellUndervoltageProtectionDelay = 0x9030;
static const uint16_t CellUndervoltageProtectionRecoveryHysteresis = 0x9031;
static const uint16_t CellOvervoltageProtectionThreshold = 0x9032;
static const uint16_t CellOvervoltageProtectionDelay = 0x9034;
static const uint16_t CellOvervoltageProtectionRecoveryHysteresis = 0x9035;

// Cell voltage registers (for host balancing decisions only)
static const uint8_t REG_CELL1_VOLTAGE = 0x14;
static const uint8_t REG_CELL2_VOLTAGE = 0x16;

// Button, power-control, and indicator timings (ms)
static const uint16_t SHORT_PRESS_MIN_MS = 80;
static const uint16_t LONG_PRESS_MS = 3000;
static const uint16_t CM5_SOFT_PRESS_MS = 300;
static const uint16_t POWER_SOFT_PRESS_ARM_MS = 15000;
static const uint8_t POWER_LED_IDLE_BREATH_STEP_MS = 18;
static const uint8_t FAST_LED_BREATH_STEP_MS = 5;
static const uint16_t STATUS_LED_BLINK_MS = 120;
static const uint16_t SHIP_LED_BLINK_MS = 100;
static const uint16_t PROTECTION_LED_MIN_MS = 3000;

static const uint8_t POWER_LED_ON_DUTY = 180;
static const uint8_t POWER_LED_BREATH_MIN_DUTY = 5;
static const uint8_t POWER_LED_BREATH_MAX_DUTY = 100;
static const uint8_t BATT_LED_BREATH_MIN_DUTY = 5;
static const uint8_t BATT_LED_BREATH_MAX_DUTY = 180;

static const uint16_t ALARM_RAW_CB = (1u << 8);
static const uint8_t SAFETY_STATUS_A_CUV = (1u << 6);
static const uint8_t SAFETY_STATUS_A_COV = (1u << 7);

// 2S Li-ion display thresholds. LED1 is the low/end-of-pack indicator.
static const uint16_t CELL_UV_THRESHOLD_MV = 3000;
static const uint16_t CELL_OV_THRESHOLD_MV = 4200;
static const uint16_t LOW_BATT_WARN_CELL_MV = 3150;
static const uint16_t BATT_LED1_PACK_MV = 6100;
static const uint16_t BATT_LED2_PACK_MV = 6900;
static const uint16_t BATT_LED3_PACK_MV = 7400;
static const uint16_t BATT_LED4_PACK_MV = 7900;

// Host balancing poll interval
static const uint16_t BALANCE_POLL_PERIOD_MS = 100;

// Host balancing (START_BALANCE): not gated on charge current or device power state.
// Balance whichever cell is higher once the voltage and imbalance thresholds are met.
static const uint16_t BALANCE_MIN_ANY_CELL_MV = 3100;
static const uint16_t BALANCE_MIN_DELTA_MV = 20;
static const uint16_t BALANCE_RETRIGGER_MS = 18000; // BQ balancing command timeout is about 20s
static const uint16_t BQ_CONFIG_RETRY_MS = 1000;

#if DEBUG
static void debugBegin() {
  pinModeFast(BATT_LEVEL2_LED_PIN, OUTPUT);
  USART0.BAUD = (uint16_t)((4UL * F_CPU) / 115200);
  USART0.CTRLC = USART_CHSIZE_8BIT_gc;
  USART0.CTRLB = USART_TXEN_bm;
}

static void debugChar(char c) {
  while (!(USART0.STATUS & USART_DREIF_bm)) {}
  USART0.TXDATAL = c;
}

static void debugUint(uint16_t value) {
  char buf[5];
  uint8_t i = 0;
  do {
    buf[i++] = (char)('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (i != 0) {
    debugChar(buf[--i]);
  }
}

static void debugNl() {
  debugChar('\n');
}
#else
#define debugBegin() do {} while (0)
#define debugChar(c) do {} while (0)
#define debugUint(v) do {} while (0)
#define debugNl() do {} while (0)
#endif

static uint8_t checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return (uint8_t)(0xFF & ~sum);
}

// Bit-banged open-drain I2C master on PA1 (SDA) / PA2 (SCL).
// The ATtiny404 is a tinyAVR 0-series part: its hardware TWI exists only on
// PB0/PB1 (no PORTMUX alternate position — that is a 1-series feature), but
// the board routes the BQ bus to PA1/PA2. External 10K pullups; lines are
// driven low by flipping DIR (OUT stays 0), released by tri-stating.
static const uint8_t I2C_SDA_bm = (1 << 1); // PA1
static const uint8_t I2C_SCL_bm = (1 << 2); // PA2

// Error codes kept compatible with Wire/debug protocol:
// 0 ok, 2 address NACK, 3 data NACK, 5 clock-stretch timeout.
static uint8_t lastI2cError = 0;

static inline void i2cDelay() { delayMicroseconds(4); } // ~100 kHz

static bool i2cSclRelease() {
  VPORTA.DIR &= ~I2C_SCL_bm;
  // Allow slave clock stretching, with a timeout so a dead bus can't hang us.
  for (uint16_t i = 0; i < 10000; i++) {
    if (VPORTA.IN & I2C_SCL_bm) {
      return true;
    }
  }
  lastI2cError = 5;
  return false;
}

static void i2cStart() {
  VPORTA.DIR &= ~I2C_SDA_bm;
  i2cSclRelease();
  i2cDelay();
  VPORTA.DIR |= I2C_SDA_bm;  // SDA low while SCL high = START
  i2cDelay();
  VPORTA.DIR |= I2C_SCL_bm;
}

static void i2cStop() {
  VPORTA.DIR |= I2C_SDA_bm;
  i2cDelay();
  i2cSclRelease();
  i2cDelay();
  VPORTA.DIR &= ~I2C_SDA_bm; // SDA released while SCL high = STOP
  i2cDelay();
}

// Returns true if the slave ACKed the byte.
static bool i2cWriteByte(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; bit++) {
    if (value & 0x80) {
      VPORTA.DIR &= ~I2C_SDA_bm;
    } else {
      VPORTA.DIR |= I2C_SDA_bm;
    }
    value <<= 1;
    i2cDelay();
    if (!i2cSclRelease()) {
      return false;
    }
    i2cDelay();
    VPORTA.DIR |= I2C_SCL_bm;
  }

  VPORTA.DIR &= ~I2C_SDA_bm; // release for ACK bit
  i2cDelay();
  if (!i2cSclRelease()) {
    return false;
  }
  const bool acked = (VPORTA.IN & I2C_SDA_bm) == 0;
  i2cDelay();
  VPORTA.DIR |= I2C_SCL_bm;
  return acked;
}

static uint8_t i2cReadByte(bool ack) {
  uint8_t value = 0;
  VPORTA.DIR &= ~I2C_SDA_bm;
  for (uint8_t bit = 0; bit < 8; bit++) {
    i2cDelay();
    if (!i2cSclRelease()) {
      return 0xFF;
    }
    value = (uint8_t)(value << 1);
    if (VPORTA.IN & I2C_SDA_bm) {
      value |= 1;
    }
    i2cDelay();
    VPORTA.DIR |= I2C_SCL_bm;
  }

  if (ack) {
    VPORTA.DIR |= I2C_SDA_bm;
  }
  i2cDelay();
  if (!i2cSclRelease()) {
    return value;
  }
  i2cDelay();
  VPORTA.DIR |= I2C_SCL_bm;
  VPORTA.DIR &= ~I2C_SDA_bm;
  return value;
}

static void i2cInit() {
  VPORTA.OUT &= ~(I2C_SDA_bm | I2C_SCL_bm); // low when driven
  VPORTA.DIR &= ~(I2C_SDA_bm | I2C_SCL_bm); // released (pulled up) when idle
}

static uint8_t i2cWrite(uint8_t reg, const uint8_t *data, uint8_t len) {
  lastI2cError = 0;
  i2cStart();
  if (!i2cWriteByte((uint8_t)(BQ_ADDR << 1))) {
    if (lastI2cError == 0) {
      lastI2cError = 2;
    }
  } else if (!i2cWriteByte(reg)) {
    if (lastI2cError == 0) {
      lastI2cError = 3;
    }
  } else {
    for (uint8_t i = 0; i < len; i++) {
      if (!i2cWriteByte(data[i])) {
        if (lastI2cError == 0) {
          lastI2cError = 3;
        }
        break;
      }
    }
  }
  i2cStop();
  delay(2);
  return lastI2cError;
}

static bool i2cRead16(uint8_t reg, uint16_t *value) {
  lastI2cError = 0;
  i2cStart();
  if (!i2cWriteByte((uint8_t)(BQ_ADDR << 1)) || !i2cWriteByte(reg)) {
    if (lastI2cError == 0) {
      lastI2cError = 2;
    }
    i2cStop();
    return false;
  }

  i2cStart(); // repeated START
  if (!i2cWriteByte((uint8_t)((BQ_ADDR << 1) | 1))) {
    if (lastI2cError == 0) {
      lastI2cError = 2;
    }
    i2cStop();
    return false;
  }

  const uint8_t lsb = i2cReadByte(true);
  const uint8_t msb = i2cReadByte(false);
  i2cStop();
  if (lastI2cError != 0) {
    return false;
  }
  *value = (uint16_t)(msb << 8) | lsb;
  return true;
}

static uint8_t sendSubcommand(uint16_t cmd) {
  uint8_t buf[2] = { (uint8_t)(cmd & 0xFF), (uint8_t)(cmd >> 8) };
  return i2cWrite(REG_SUBCMD, buf, sizeof(buf));
}

static void writeConfig8(uint16_t addr, uint8_t value) {
  uint8_t payload[3] = {
    (uint8_t)(addr & 0xFF),
    (uint8_t)(addr >> 8),
    value
  };
  uint8_t cks = checksum(payload, sizeof(payload));
  uint8_t meta[2] = { cks, 0x05 };
  i2cWrite(REG_SUBCMD, payload, sizeof(payload));
  i2cWrite(REG_SUBCMD_DATA, meta, sizeof(meta));
}

static void writeConfig16(uint16_t addr, uint16_t value) {
  uint8_t payload[4] = {
    (uint8_t)(addr & 0xFF),
    (uint8_t)(addr >> 8),
    (uint8_t)(value & 0xFF),
    (uint8_t)(value >> 8)
  };
  uint8_t cks = checksum(payload, sizeof(payload));
  uint8_t meta[2] = { cks, 0x06 };
  i2cWrite(REG_SUBCMD, payload, sizeof(payload));
  i2cWrite(REG_SUBCMD_DATA, meta, sizeof(meta));
}

static bool bqCellBalancingActive = false;
static bool bqReadValid = false;
static uint16_t latestBatteryStatus = 0;
static bool bqConfigured = false;
static uint16_t latestCell1Mv = 0;
static uint16_t latestCell2Mv = 0;
static uint16_t protectionBlinkUntilMs = 0;
static uint8_t protectionBlinkFlags = 0;
static uint16_t lastBqConfigAttemptMs = 0;
#if DEBUG
static uint16_t lastDebugStatusMs = 0;
#endif

static bool devicePowerOn = false;
static uint16_t devicePowerOnStartedMs = 0;
static uint8_t powerLedBreathDuty = POWER_LED_BREATH_MIN_DUTY;
static int8_t powerLedBreathDirection = 1;
static uint16_t powerLedLastStepMs = 0;
static uint8_t battLedBreathDuty = BATT_LED_BREATH_MIN_DUTY;
static int8_t battLedBreathDirection = 1;
static uint16_t battLedLastStepMs = 0;
static uint16_t lastStatusBlinkMs = 0;
static bool statusBlinkOn = false;

static void writeActiveLowPwm(uint8_t pin, uint8_t duty) {
  analogWrite(pin, 255 - duty);
}

static uint8_t breathingDuty(uint8_t *duty, int8_t *direction, uint16_t *lastStepMs,
                             uint16_t now, uint8_t stepMs,
                             uint8_t minDuty, uint8_t maxDuty) {
  if ((uint16_t)(now - *lastStepMs) >= stepMs) {
    *lastStepMs = now;
    const int16_t nextDuty = (int16_t)(*duty) + *direction;
    if (nextDuty >= maxDuty) {
      *duty = maxDuty;
      *direction = -1;
    } else if (nextDuty <= minDuty) {
      *duty = minDuty;
      *direction = 1;
    } else {
      *duty = (uint8_t)nextDuty;
    }
  }
  return *duty;
}

static void releasePowerControl() {
  digitalWriteFast(PWR_CONTROL_PIN, HIGH);
  //pinModeFast(PWR_CONTROL_PIN, INPUT_PULLUP);
}

static void releaseCm5SoftPower() {
  digitalWriteFast(CM5_SOFT_PWR_OUT, HIGH);
  pinModeFast(CM5_SOFT_PWR_OUT, INPUT_PULLUP);
}

static void setBatteryLedsOff() {
  digitalWriteFast(BATT_LEVEL1_LED_PIN, HIGH);
#if !DEBUG
  digitalWriteFast(BATT_LEVEL2_LED_PIN, HIGH);
#endif
  digitalWriteFast(BATT_LEVEL3_LED_PIN, HIGH);
  digitalWriteFast(BATT_LEVEL4_LED_PIN, HIGH);
}

static void setDevicePower(bool on) {
  if (on) {
    if (!devicePowerOn) {
      devicePowerOnStartedMs = (uint16_t)millis();
    }
    pinModeFast(PWR_CONTROL_PIN, OUTPUT);
    digitalWriteFast(PWR_CONTROL_PIN, LOW);
    devicePowerOn = true;
    debugChar('P');
    debugChar('1');
    debugNl();
    return;
  }

  releasePowerControl();
  releaseCm5SoftPower();
  setBatteryLedsOff();
  devicePowerOn = false;
  debugChar('P');
  debugChar('0');
  debugNl();
}

static void startCm5SoftPowerPress() {
  if (!devicePowerOn) {
    return;
  }

  pinModeFast(CM5_SOFT_PWR_OUT, OUTPUT);
  digitalWriteFast(CM5_SOFT_PWR_OUT, LOW);
  delay(CM5_SOFT_PRESS_MS);
  releaseCm5SoftPower();
}

static void powerLedTask() {
  const uint16_t now = (uint16_t)millis();

  if (bqCellBalancingActive) {
    writeActiveLowPwm(LED_PIN, breathingDuty(&powerLedBreathDuty, &powerLedBreathDirection,
                                             &powerLedLastStepMs, now, FAST_LED_BREATH_STEP_MS,
                                             POWER_LED_BREATH_MIN_DUTY,
                                             POWER_LED_BREATH_MAX_DUTY));
  } else if (devicePowerOn) {
    writeActiveLowPwm(LED_PIN, POWER_LED_ON_DUTY);
  } else {
    writeActiveLowPwm(LED_PIN, breathingDuty(&powerLedBreathDuty, &powerLedBreathDirection,
                                             &powerLedLastStepMs, now, POWER_LED_IDLE_BREATH_STEP_MS,
                                             POWER_LED_BREATH_MIN_DUTY,
                                             POWER_LED_BREATH_MAX_DUTY));
  }
}

static uint8_t batteryLedCount() {
  if (!bqReadValid) {
    return 0;
  }

  const uint16_t packMv = latestCell1Mv + latestCell2Mv;
  if (packMv >= BATT_LED4_PACK_MV) {
    return 4;
  }
  if (packMv >= BATT_LED3_PACK_MV) {
    return 3;
  }
  if (packMv >= BATT_LED2_PACK_MV) {
    return 2;
  }
  if (packMv >= BATT_LED1_PACK_MV) {
    return 1;
  }
  return 0;
}

static bool lowBatteryWarningActive() {
  if (!bqReadValid) {
    return false;
  }

  return latestCell1Mv <= LOW_BATT_WARN_CELL_MV || latestCell2Mv <= LOW_BATT_WARN_CELL_MV;
}

static void batteryLedTask() {
  if (!devicePowerOn) {
    setBatteryLedsOff();
    return;
  }

  const uint16_t now = (uint16_t)millis();
  if ((uint16_t)(now - lastStatusBlinkMs) >= STATUS_LED_BLINK_MS) {
    lastStatusBlinkMs = now;
    statusBlinkOn = !statusBlinkOn;
  }
  if (protectionBlinkFlags != 0 && (int16_t)(now - protectionBlinkUntilMs) >= 0) {
    protectionBlinkFlags = 0;
  }
  const bool showUnderVoltage = (protectionBlinkFlags & SAFETY_STATUS_A_CUV) != 0;
  const bool showOverVoltage = (protectionBlinkFlags & SAFETY_STATUS_A_COV) != 0;

  if (showUnderVoltage || showOverVoltage) {
    digitalWriteFast(BATT_LEVEL1_LED_PIN, showUnderVoltage && statusBlinkOn ? LOW : HIGH);
#if !DEBUG
    digitalWriteFast(BATT_LEVEL2_LED_PIN, showUnderVoltage && statusBlinkOn ? LOW : HIGH);
#endif
    digitalWriteFast(BATT_LEVEL3_LED_PIN, showOverVoltage && statusBlinkOn ? LOW : HIGH);
    digitalWriteFast(BATT_LEVEL4_LED_PIN, showOverVoltage && statusBlinkOn ? LOW : HIGH);
    return;
  }

  if (lowBatteryWarningActive()) {
    writeActiveLowPwm(BATT_LEVEL1_LED_PIN, breathingDuty(&battLedBreathDuty, &battLedBreathDirection,
                                                        &battLedLastStepMs, now, FAST_LED_BREATH_STEP_MS,
                                                        BATT_LED_BREATH_MIN_DUTY,
                                                        BATT_LED_BREATH_MAX_DUTY));
#if !DEBUG
    digitalWriteFast(BATT_LEVEL2_LED_PIN, HIGH);
#endif
    digitalWriteFast(BATT_LEVEL3_LED_PIN, HIGH);
    digitalWriteFast(BATT_LEVEL4_LED_PIN, HIGH);
    return;
  }

  const uint8_t litCount = batteryLedCount();
  digitalWriteFast(BATT_LEVEL1_LED_PIN, litCount >= 1 ? LOW : HIGH);
#if !DEBUG
  digitalWriteFast(BATT_LEVEL2_LED_PIN, litCount >= 2 ? LOW : HIGH);
#endif
  digitalWriteFast(BATT_LEVEL3_LED_PIN, litCount >= 3 ? LOW : HIGH);
  digitalWriteFast(BATT_LEVEL4_LED_PIN, litCount >= 4 ? LOW : HIGH);
}

#if ENABLE_SHIP_BUTTON
static void enterShipMode() {
  sendSubcommand(SHUTDOWN);
  sendSubcommand(SHUTDOWN);

  setDevicePower(false);
  while (true) {
    digitalWriteFast(LED_PIN, LOW);
    delay(SHIP_LED_BLINK_MS);
    digitalWriteFast(LED_PIN, HIGH);
    delay(SHIP_LED_BLINK_MS);
  }
}
#endif

static bool bqPresent() {
  lastI2cError = 0;
  i2cStart();
  if (!i2cWriteByte((uint8_t)(BQ_ADDR << 1)) && lastI2cError == 0) {
    lastI2cError = 2;
  }
  i2cStop();
  return lastI2cError == 0;
}

static bool configureBq76905() {
  if (!bqPresent()) {
    // N<err> <pinlevels>: pinlevels bit0=SDA, bit1=SCL (3 = bus idle high).
    debugChar('N');
    debugUint(lastI2cError);
    debugChar(' ');
    debugChar((char)('0' + ((VPORTA.IN >> 1) & 0x03)));
    debugNl();
    return false;
  }

  sendSubcommand(SET_CFGUPDATE);
  delay(10);

  // 2S Li-ion defaults (VcellMode uses 0x00 for 2-series)
  writeConfig8(VcellMode, 0x02);

  // Enable autonomous FET control and allow CHG FET in SLEEP mode.
  // Bit map (Table 12-8): SLEEPCHG=bit4, SFET=bit3, FET_EN=bit2.
  writeConfig8(FETOptions, 0x1C);
  //writeConfig8(FETOptions, 0x04);

  // Enable only CUV/COV safety protections.
  // SafetyStatusA bit mapping uses COV=bit7, CUV=bit6 -> mask 0xC0.
  // Use 8-bit writes here because EnabledProtectionsA/B are 1-byte fields.
  writeConfig8(EnabledProtectionsA, 0xC0);
  writeConfig8(EnabledProtectionsB, 0x00);
  // Cell balancing settings:
  // - CB_NO_CMD=0 (allow CB_ACTIVE_CELLS host command), CBDLY=1ms (0x02 default)
  // - very permissive balance temperature window during bring-up
  writeConfig8(BalancingConfiguration, 0x02);
  writeConfig8(BalancingMinTempThreshold, 0xFF);
  writeConfig8(BalancingMaxTempThreshold, 0x00);
  writeConfig8(BalancingMaxInternalTemp, 110);
  // Debug-friendly: enable all alarm sources so AlarmStatus can latch SSA/SSB
  // and other events for host visibility.
  writeConfig16(DefaultAlarmMask, 0xFFFF);

  // Typical 2S Li-ion per-cell thresholds.
  // Thresholds are mV (I2). Delay/hysteresis are compact encoded fields (U1/H1).
  writeConfig16(CellUndervoltageProtectionThreshold, CELL_UV_THRESHOLD_MV);
  writeConfig8(CellUndervoltageProtectionDelay, 10);         // 10 ADSCAN intervals
  writeConfig8(CellUndervoltageProtectionRecoveryHysteresis, 2); // +100mV, autonomous recovery enabled

  writeConfig16(CellOvervoltageProtectionThreshold, CELL_OV_THRESHOLD_MV);
  writeConfig8(CellOvervoltageProtectionDelay, 10);          // 10 ADSCAN intervals
  writeConfig8(CellOvervoltageProtectionRecoveryHysteresis, 2); // -100mV, autonomous recovery enabled

  const bool ok = sendSubcommand(EXIT_CFGUPDATE) == 0;
  delay(100);
  debugChar(ok ? 'O' : 'E');
  debugNl();
  return ok;
}

static uint16_t lastBalancePollMs = 0;
static uint16_t lastBalanceCommandMs = 0;

/** Periodically read cell voltages and issue START_BALANCE when rules are met. */
static void hostBalanceTask() {
  const uint16_t now = (uint16_t)millis();
  if (!bqConfigured) {
    if ((uint16_t)(now - lastBqConfigAttemptMs) >= BQ_CONFIG_RETRY_MS) {
      lastBqConfigAttemptMs = now;
      bqConfigured = configureBq76905();
    }
    return;
  }

  if ((uint16_t)(now - lastBalancePollMs) < BALANCE_POLL_PERIOD_MS) {
    return;
  }
  lastBalancePollMs = now;

  uint16_t cell1Mv = 0;
  uint16_t cell2Mv = 0;
  if (!i2cRead16(REG_CELL1_VOLTAGE, &cell1Mv) ||
      !i2cRead16(REG_CELL2_VOLTAGE, &cell2Mv)) {
    bqReadValid = false;
    bqCellBalancingActive = false;
    bqConfigured = false;
    return;
  }
  bqReadValid = true;
  latestCell1Mv = cell1Mv;
  latestCell2Mv = cell2Mv;

  uint16_t regValue = 0;
  if (i2cRead16(REG_ALARM_RAW_STATUS, &regValue)) {
    bqCellBalancingActive = (regValue & ALARM_RAW_CB) != 0;
  }

  i2cRead16(REG_BATTERY_STATUS, &latestBatteryStatus);

  if (i2cRead16(REG_SAFETY_STATUS_A, &regValue)) {
    const uint8_t protectionA = (uint8_t)(regValue & 0xFF);
    if ((protectionA & (SAFETY_STATUS_A_CUV | SAFETY_STATUS_A_COV)) != 0) {
      protectionBlinkFlags |= protectionA & (SAFETY_STATUS_A_CUV | SAFETY_STATUS_A_COV);
      protectionBlinkUntilMs = now + PROTECTION_LED_MIN_MS;
    }
  }

  const bool anyCellAboveMin =
      (cell1Mv >= BALANCE_MIN_ANY_CELL_MV) || (cell2Mv >= BALANCE_MIN_ANY_CELL_MV);
  const uint16_t cellDiffMv =
      (cell1Mv >= cell2Mv) ? (uint16_t)(cell1Mv - cell2Mv) : (uint16_t)(cell2Mv - cell1Mv);
  const bool imbalanceEnough = cellDiffMv >= BALANCE_MIN_DELTA_MV;

  if (anyCellAboveMin && imbalanceEnough) {
    uint8_t balanceMask = 0x00;
    if (cell1Mv > cell2Mv) {
      balanceMask = 0x02; // bit1 = first active cell (VC1-VC0)
    } else if (cell2Mv > cell1Mv) {
      balanceMask = 0x04; // bit2 = second active cell
    }

    if (balanceMask != 0 &&
        (lastBalanceCommandMs == 0 || (uint16_t)(now - lastBalanceCommandMs) >= BALANCE_RETRIGGER_MS)) {
      writeConfig8(START_BALANCE, balanceMask);
      lastBalanceCommandMs = now;
    }
  }
}

#if DEBUG
static void debugStatusTask() {
  const uint16_t now = (uint16_t)millis();
  if ((uint16_t)(now - lastDebugStatusMs) < 1000) {
    return;
  }
  lastDebugStatusMs = now;

  debugChar('S');
  debugChar(bqReadValid ? '1' : '0');
  debugChar(' ');
  debugUint(latestCell1Mv);
  debugChar(',');
  debugUint(latestCell2Mv);
  debugChar(' ');
  debugChar(bqCellBalancingActive ? 'B' : '-');
  debugChar(devicePowerOn ? 'P' : 'p');
  debugChar(' ');
  debugUint(lastI2cError);
  debugChar(' ');
  debugUint(latestBatteryStatus);
  debugNl();
}
#else
#define debugStatusTask() do {} while (0)
#endif

void setup() {
  debugBegin();
  debugChar('B');
  debugNl();
  pinModeFast(PWR_BUTTON_PIN, INPUT_PULLUP);
#if ENABLE_SHIP_BUTTON
  pinModeFast(SHIP_BUTTON_PIN, INPUT_PULLUP);
#endif
  releasePowerControl();
  releaseCm5SoftPower();

  pinModeFast(LED_PIN, OUTPUT);
  digitalWriteFast(LED_PIN, HIGH);

  pinModeFast(BATT_LEVEL1_LED_PIN, OUTPUT);
#if !DEBUG
  pinModeFast(BATT_LEVEL2_LED_PIN, OUTPUT);
#endif
  pinModeFast(BATT_LEVEL3_LED_PIN, OUTPUT);
  pinModeFast(BATT_LEVEL4_LED_PIN, OUTPUT);
  setBatteryLedsOff();

  i2cInit();
  delay(50);
  bqConfigured = configureBq76905();
}

static void handlePowerShortPress() {
  if (!devicePowerOn) {
    setDevicePower(true);
    return;
  }

  if ((uint16_t)((uint16_t)millis() - devicePowerOnStartedMs) < POWER_SOFT_PRESS_ARM_MS) {
    setDevicePower(false);
  } else {
    startCm5SoftPowerPress();
  }
}

static bool powerButtonDown = false;
static bool powerButtonLongHandled = false;
static uint16_t powerButtonPressedAtMs = 0;

static void powerButtonTask() {
  const uint16_t now = (uint16_t)millis();
  const bool pressed = digitalReadFast(PWR_BUTTON_PIN) == LOW;
  if (pressed) {
    if (!powerButtonDown) {
      powerButtonDown = true;
      powerButtonLongHandled = false;
      powerButtonPressedAtMs = now;
      debugChar('D');
      debugNl();
    }
    if (!powerButtonLongHandled &&
        (uint16_t)(now - powerButtonPressedAtMs) >= LONG_PRESS_MS) {
      powerButtonLongHandled = true;
      debugChar('L');
      debugNl();
      setDevicePower(false);
    }
    return;
  }

  if (!powerButtonDown) {
    return;
  }
  powerButtonDown = false;

  if (!powerButtonLongHandled &&
      (uint16_t)(now - powerButtonPressedAtMs) >= SHORT_PRESS_MIN_MS) {
    debugChar('R');
    debugNl();
    handlePowerShortPress();
  }
}

#if ENABLE_SHIP_BUTTON
static void shipButtonTask() {
  if (digitalReadFast(SHIP_BUTTON_PIN) != LOW) {
    return;
  }

  const uint16_t pressedAtMs = (uint16_t)millis();
  while (digitalReadFast(SHIP_BUTTON_PIN) == LOW) {
    if ((uint16_t)((uint16_t)millis() - pressedAtMs) >= LONG_PRESS_MS) {
      enterShipMode();
    }
  }
}
#endif

void loop() {
  powerButtonTask();
#if ENABLE_SHIP_BUTTON
  shipButtonTask();
#endif
  hostBalanceTask();

  powerLedTask();
  batteryLedTask();
  debugStatusTask();
}

