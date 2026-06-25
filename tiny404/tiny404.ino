// ATtiny1604 BQ76905 host: configuration, button power modes, and host cell balancing.
// Status is read elsewhere on the I2C bus — this MCU only drives START_BALANCE when needed.
// Requires megaTinyCore in Arduino IDE or arduino-cli. Uses hardware TWI (`Wire`).

#include <Wire.h>

#define ENABLE_LED

// #ifndef PIN_PA0
// #define PIN_PA0 11
// #endif
// #ifndef PIN_PA1
// #define PIN_PA1 8
// #endif
// #ifndef PIN_PA2
// #define PIN_PA2 9
// #endif
// #ifndef PIN_PA3
// #define PIN_PA3 10
// #endif
// #ifndef PIN_PA4
// #define PIN_PA4 0
// #endif
// #ifndef PIN_PA5
// #define PIN_PA5 1
// #endif
// #ifndef PIN_PA6
// #define PIN_PA6 2
// #endif
// #ifndef PIN_PA7
// #define PIN_PA7 3
// #endif
// #ifndef PIN_PB0
// #define PIN_PB0 7
// #endif
// #ifndef PIN_PB1
// #define PIN_PB1 6
// #endif
// #ifndef PIN_PB2
// #define PIN_PB2 5
// #endif
// #ifndef PIN_PB3
// #define PIN_PB3 4
// #endif


static const uint8_t PWR_BUTTON_PIN = PIN_PB2;
static const uint8_t SHIP_BUTTON_PIN = PIN_PA3;
//static const uint8_t BQ_ALERT_PIN = PIN_PA7;
static const uint8_t PWR_CONTROL_PIN = PIN_PB3;
static const uint8_t LED_PIN = PIN_PA5;

// BQ7690x I2C address
static const uint8_t BQ_ADDR = 0x08;
static const uint8_t ESP_BQ_DISCOVERY_ADDR = 0x42;
static const uint8_t TINY_BQ_COORDINATOR_ADDR = 0x43;
static const uint8_t BQ_ROLE_MAGIC = 0xC5;
static const uint8_t BQ_ROLE_CMD_ESP_MASTER = 0xA1;
static const uint8_t BQ_ROLE_CMD_TINY_PING = 0x5A;

// Subcommand and data registers
static const uint8_t REG_SUBCMD = 0x3E;
static const uint8_t REG_SUBCMD_DATA = 0x60;
static const uint8_t REG_ALARM_RAW_STATUS = 0x64;
// static const uint8_t REG_FET_CONTROL = 0x68;
// static const uint8_t REGOUT_CONTROL = 0x69;
// static const uint8_t REGOUT_REG_EN = (1u << 3);

// Command-only subcommands
static const uint16_t SET_CFGUPDATE = 0x0090;
static const uint16_t EXIT_CFGUPDATE = 0x0092;
static const uint16_t DEEPSLEEP = 0x000F;
static const uint16_t EXIT_DEEPSLEEP = 0x000E;
static const uint16_t SHUTDOWN = 0x0010;
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

// Button timings (ms)
static const uint16_t TIME_SHORT = 200;
static const uint16_t TIME_LONG = 2000;
static const uint16_t ALARM_RAW_CB = (1u << 8);

// Host balancing poll interval
static const uint16_t BALANCE_POLL_PERIOD_MS = 100;

// Host balancing (START_BALANCE): not gated on charge current — only cell voltages.
// Balance when imbalance >= 50 mV and at least one cell is at or above 3.9 V.
static const uint16_t BALANCE_MIN_ANY_CELL_MV = 3100;
static const uint16_t BALANCE_MIN_DELTA_MV = 20;
static const uint32_t BALANCE_RETRIGGER_MS = 18000; // BQ balancing command timeout is about 20s
static const uint8_t BQ_RECOVERY_READ_FAILURES = 5;
static const uint32_t BQ_RECOVERY_PERIOD_MS = 1000;

// ESP32 handoff: Tiny owns the BQ by default, probes ESP every 3s, then becomes
// an I2C target while the ESP is the BQ controller. If ESP pings stop, Tiny
// falls back to BQ master mode and reconfigures the BQ.
static const uint32_t ESP_PROBE_PERIOD_MS = 3000;
static const uint32_t ESP_HANDOFF_SETTLE_MS = 500;
static const uint32_t ESP_PING_PERIOD_MS = 500;
static const uint32_t ESP_PING_TIMEOUT_MS = 1000;
static const uint32_t ESP_FIRST_PING_GRACE_MS = 8000;

#ifdef ENABLE_LED
static void flash() {
  digitalWrite(LED_PIN, LOW);
  delay(TIME_SHORT);
  digitalWrite(LED_PIN, HIGH);
  delay(TIME_SHORT);
}
#endif

static uint8_t checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return (uint8_t)(0xFF & ~sum);
}

static bool i2cWrite(uint8_t reg, const uint8_t *data, uint8_t len) {
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(reg);
  Wire.write(data, len);
  const uint8_t status = Wire.endTransmission();
  delay(2);
  return status == 0;
}

static uint16_t i2cRead16(uint8_t reg) {
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return 0;
  }

  if (Wire.requestFrom((int)BQ_ADDR, 2) != 2) {
    return 0;
  }

  const uint8_t lsb = Wire.read();
  const uint8_t msb = Wire.read();
  return (uint16_t)(msb << 8) | lsb;
}

static void sendSubcommand(uint16_t cmd) {
  uint8_t buf[2] = { (uint8_t)(cmd & 0xFF), (uint8_t)(cmd >> 8) };
  i2cWrite(REG_SUBCMD, buf, sizeof(buf));
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

static void writeSubcommand8(uint16_t cmd, uint8_t value) {
  uint8_t payload[3] = {
    (uint8_t)(cmd & 0xFF),
    (uint8_t)(cmd >> 8),
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


bool inDeepSleep = false;
static bool bqCellBalancingActive = false;

#ifdef ENABLE_LED
static uint32_t lastIdleLedChangeMs = 0;
static uint8_t idleLedBrightness = 6;
static int8_t idleLedDirection = 1;
static bool idleLedBlipOn = false;
static bool idleLedBreathing = true;
static bool analogLedFlashOn = false;
static bool slaveLedOn = false;

static bool idleLedShouldBreathe() {
  return idleLedBreathing;
}

static void toggleIdleLedMode() {
  idleLedBreathing = !idleLedBreathing;
  idleLedBlipOn = false;
  lastIdleLedChangeMs = millis();
  digitalWrite(LED_PIN, HIGH);
}

static void idleLedTask() {
  const uint32_t now = millis();

  // if (analogRead(SHIP_BUTTON_PIN) > 102) {
  //   if ((uint32_t)(now - lastIdleLedChangeMs) >= 50) {
  //     analogLedFlashOn = !analogLedFlashOn;
  //     digitalWrite(LED_PIN, analogLedFlashOn ? LOW : HIGH);
  //     lastIdleLedChangeMs = now;
  //   }
  //   return;
  // }

  if (analogLedFlashOn) {
    digitalWrite(LED_PIN, HIGH);
    analogLedFlashOn = false;
    lastIdleLedChangeMs = now;
  }

  if (!idleLedShouldBreathe()) {
    if (idleLedBlipOn) {
      if ((uint32_t)(now - lastIdleLedChangeMs) >= 50) {
        digitalWrite(LED_PIN, HIGH);
        idleLedBlipOn = false;
        lastIdleLedChangeMs = now;
      }
    } else if ((uint32_t)(now - lastIdleLedChangeMs) >= 1000) {
      digitalWrite(LED_PIN, LOW);
      idleLedBlipOn = true;
      lastIdleLedChangeMs = now;
    }
    return;
  }

  idleLedBlipOn = false;

  const uint8_t breathStepMs = bqCellBalancingActive ? 5 : 15;
  if ((uint32_t)(now - lastIdleLedChangeMs) < breathStepMs) {
    return;
  }

  lastIdleLedChangeMs = now;

  const int16_t nextBrightness = (int16_t)idleLedBrightness + idleLedDirection;
  if (nextBrightness >= 100) {
    idleLedBrightness = 100;
    idleLedDirection = -1;
  } else if (nextBrightness <= 4) {
    idleLedBrightness = 4;
    idleLedDirection = 1;
  } else {
    idleLedBrightness = (uint8_t)nextBrightness;
  }

  analogWrite(LED_PIN, 255 - idleLedBrightness);
}

static void slaveLedTask() {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastIdleLedChangeMs) < 500) {
    return;
  }

  lastIdleLedChangeMs = now;
  slaveLedOn = !slaveLedOn;
  digitalWrite(LED_PIN, slaveLedOn ? LOW : HIGH);
}
#endif

//unsigned int long lastDeepSleepToggleTime = 0;
//toggles deep sleep mode (REGOUT on but CHG & DISCHG FETs are off)
static void toggleDeepSleep() {

  // // in uncommented, a double-press re-writes the configuration to the BQ
  // if (millis() - lastDeepSleepToggleTime < 3000) {
  //   sendSubcommand(EXIT_DEEPSLEEP);
  //   inDeepSleep = false;
  //   delay(1000);
  //   configureBq76905();
  //   return;
  // }

  if (inDeepSleep) {
    sendSubcommand(EXIT_DEEPSLEEP);
    inDeepSleep = false;
  } else {
    sendSubcommand(DEEPSLEEP);
    sendSubcommand(DEEPSLEEP);
    inDeepSleep = true;
  }
  //lastDeepSleepToggleTime = millis();
  
}

static void enterShipMode() {
  sendSubcommand(SHUTDOWN);
  sendSubcommand(SHUTDOWN);
}

static void configureBq76905() {
  
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
  writeConfig16(CellUndervoltageProtectionThreshold, 3000);  // 3.0V
  writeConfig8(CellUndervoltageProtectionDelay, 10);         // 10 ADSCAN intervals
  writeConfig8(CellUndervoltageProtectionRecoveryHysteresis, 2); // +100mV, autonomous recovery enabled

  writeConfig16(CellOvervoltageProtectionThreshold, 4200);   // 4.20V
  writeConfig8(CellOvervoltageProtectionDelay, 10);          // 10 ADSCAN intervals
  writeConfig8(CellOvervoltageProtectionRecoveryHysteresis, 2); // -100mV, autonomous recovery enabled

  sendSubcommand(EXIT_CFGUPDATE);
  delay(100);
  
}

static uint32_t lastBalancePollMs = 0;
static uint32_t lastBalanceCommandMs = 0;
static uint32_t lastBqRecoveryMs = 0;
static uint8_t bqReadFailures = 0;
static uint32_t lastEspProbeMs = 0;
static uint32_t lastEspPingMs = 0;
static uint32_t slaveModeStartedMs = 0;
static volatile bool espPingPending = false;
static bool espPingSeen = false;

enum TinyBusRole : uint8_t {
  TINY_ROLE_BQ_MASTER = 0,
  TINY_ROLE_PING_TARGET = 1,
};

static TinyBusRole tinyBusRole = TINY_ROLE_BQ_MASTER;

static void restartMasterI2c(bool reconfigureBq) {
  Wire.end();
  delay(25);
  Wire.begin();
  Wire.setClock(100000);
  bqReadFailures = 0;
  lastBalanceCommandMs = 0;
  lastEspProbeMs = millis() - ESP_PROBE_PERIOD_MS;
  if (reconfigureBq) {
    configureBq76905();
  }
}

/** Periodically read cell voltages and issue START_BALANCE when rules are met. */
static void hostBalanceTask() {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastBalancePollMs) < BALANCE_POLL_PERIOD_MS) {
    return;
  }
  lastBalancePollMs = now;

  const uint16_t cell1Mv = i2cRead16(REG_CELL1_VOLTAGE);
  const uint16_t cell2Mv = i2cRead16(REG_CELL2_VOLTAGE);
  if (cell1Mv == 0 && cell2Mv == 0) {
    if (bqReadFailures < 0xFF) {
      bqReadFailures++;
    }
    if (bqReadFailures >= BQ_RECOVERY_READ_FAILURES &&
        (uint32_t)(now - lastBqRecoveryMs) >= BQ_RECOVERY_PERIOD_MS) {
      lastBqRecoveryMs = now;
      restartMasterI2c(true);
    }
    return;
  }
  bqReadFailures = 0;

  const uint16_t alarmRawStatus = i2cRead16(REG_ALARM_RAW_STATUS);
  bqCellBalancingActive = (alarmRawStatus & ALARM_RAW_CB) != 0;

  const bool anyCellAboveMin =
      (cell1Mv >= BALANCE_MIN_ANY_CELL_MV) || (cell2Mv >= BALANCE_MIN_ANY_CELL_MV);
  const uint16_t cellDiffMv =
      (cell1Mv >= cell2Mv) ? (uint16_t)(cell1Mv - cell2Mv) : (uint16_t)(cell2Mv - cell1Mv);
  const bool imbalanceEnough = cellDiffMv >= BALANCE_MIN_DELTA_MV;

  if (!inDeepSleep && anyCellAboveMin && imbalanceEnough) {
    uint8_t balanceMask = 0x00;
    if (cell1Mv > cell2Mv) {
      balanceMask = 0x02; // bit1 = first active cell (VC1-VC0)
    } else if (cell2Mv > cell1Mv) {
      balanceMask = 0x04; // bit2 = second active cell
    }

    if (balanceMask != 0 &&
        (lastBalanceCommandMs == 0 || (uint32_t)(now - lastBalanceCommandMs) >= BALANCE_RETRIGGER_MS)) {
      writeSubcommand8(START_BALANCE, balanceMask);
      lastBalanceCommandMs = now;
    }
  }
}

static void handleEspPing(int len) {
  uint8_t first = 0;
  uint8_t second = 0;
  if (len >= 1 && Wire.available()) {
    first = (uint8_t)Wire.read();
  }
  if (len >= 2 && Wire.available()) {
    second = (uint8_t)Wire.read();
  }
  while (Wire.available()) {
    Wire.read();
  }
  if (first == BQ_ROLE_MAGIC && second == BQ_ROLE_CMD_TINY_PING) {
    espPingPending = true;
  }
}

static void enterTinySlaveMode() {
  Wire.end();
  delay(10);
  Wire.onReceive(handleEspPing);
  Wire.begin(TINY_BQ_COORDINATOR_ADDR);
  tinyBusRole = TINY_ROLE_PING_TARGET;
  espPingPending = false;
  delay(ESP_HANDOFF_SETTLE_MS);
  slaveModeStartedMs = millis();
  lastEspPingMs = slaveModeStartedMs;
  espPingSeen = false;
#ifdef ENABLE_LED
  slaveLedOn = false;
  lastIdleLedChangeMs = 0;
  digitalWrite(LED_PIN, HIGH);
#endif
}

static void enterTinyMasterMode(bool reconfigureBq) {
  restartMasterI2c(reconfigureBq);
  tinyBusRole = TINY_ROLE_BQ_MASTER;
  espPingPending = false;
  espPingSeen = false;
  slaveModeStartedMs = 0;
  lastEspProbeMs = millis() - ESP_PROBE_PERIOD_MS;
#ifdef ENABLE_LED
  slaveLedOn = false;
  lastIdleLedChangeMs = millis();
  digitalWrite(LED_PIN, HIGH);
#endif
}

static bool requestEspBecomeMaster() {
  Wire.beginTransmission(ESP_BQ_DISCOVERY_ADDR);
  Wire.write(BQ_ROLE_MAGIC);
  Wire.write(BQ_ROLE_CMD_ESP_MASTER);
  return Wire.endTransmission() == 0;
}

static void espHandoffTask() {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastEspProbeMs) < ESP_PROBE_PERIOD_MS) {
    return;
  }
  lastEspProbeMs = now;

  if (requestEspBecomeMaster()) {
    enterTinySlaveMode();
  }
}

static void slaveWatchdogTask() {
  const uint32_t now = millis();
  if (espPingPending) {
    espPingPending = false;
    lastEspPingMs = now;
    espPingSeen = true;
  }

  const uint32_t timeoutMs = espPingSeen ? ESP_PING_TIMEOUT_MS : ESP_FIRST_PING_GRACE_MS;
  const uint32_t sinceMs = espPingSeen ? (uint32_t)(now - lastEspPingMs)
                                       : (uint32_t)(now - slaveModeStartedMs);
  if (sinceMs > timeoutMs) {
    enterTinyMasterMode(true);
  }
}

void setup() {
  pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SHIP_BUTTON_PIN, INPUT);
  pinMode(PWR_CONTROL_PIN, OUTPUT);
  #ifdef ENABLE_LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  #endif

  Wire.begin();
  Wire.setClock(100000);
  tinyBusRole = TINY_ROLE_BQ_MASTER;
  delay(50);
  configureBq76905();
}

static void buttonLongPress () {
  enterShipMode();
  #ifdef ENABLE_LED
  while(true){
    flash();
  }  
  #endif
}
static void buttonShortPress () {
  toggleDeepSleep();
  #ifdef ENABLE_LED
  toggleIdleLedMode();
  #endif
}

bool lastButtonState = false;
unsigned long lastBtnPressTime = 0;

void readButton() {
  if (digitalRead(PWR_BUTTON_PIN) == LOW) {
    if (!lastButtonState) {
      lastBtnPressTime = millis();
      lastButtonState = true;
    }
  } else {
    if (lastButtonState) {
      unsigned long duration = millis() - lastBtnPressTime;

      if (duration > TIME_LONG) {
        buttonLongPress();
      } else if (duration > TIME_SHORT) {
        buttonShortPress();
      }
    }
    lastButtonState = false;
  }
}

void loop() {
  if (tinyBusRole == TINY_ROLE_PING_TARGET) {
    slaveWatchdogTask();
#ifdef ENABLE_LED
    slaveLedTask();
#endif
    return;
  }

  readButton();
  espHandoffTask();
  hostBalanceTask();
#ifdef ENABLE_LED
  idleLedTask();
#endif
}

