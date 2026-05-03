// ATtiny1604 BQ76905 bring-up / telemetry controller
// Requires megaTinyCore in Arduino IDE or arduino-cli.
// Uses hardware TWI (`Wire`) and hardware UART (`Serial`).

#include <Wire.h>

#ifndef PIN_PA7
#define PIN_PA7 7
#endif
#ifndef PIN_PA3
#define PIN_PA3 3
#endif
#ifndef PIN_PA6
#define PIN_PA6 6
#endif


static const uint8_t BUTTON_PIN = PIN_PA3;
//#define ENABLE_LED
#ifdef ENABLE_LED
static const uint8_t LED_PIN = PIN_PA7;
#endif

// BQ7690x I2C address
static const uint8_t BQ_ADDR = 0x08;

// Subcommand and data registers
static const uint8_t REG_SUBCMD = 0x3E;
static const uint8_t REG_SUBCMD_DATA = 0x60;
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

// Direct command registers (read-only status)
static const uint8_t REG_CELL1_VOLTAGE = 0x14;
static const uint8_t REG_CELL2_VOLTAGE = 0x16;
static const uint8_t REG_PACK_CURRENT = 0x3A;
static const uint8_t REG_INT_TEMPERATURE = 0x28;
static const uint8_t REG_TS_MEASUREMENT = 0x2A;
static const uint8_t REG_ALARM_STATUS = 0x62;
static const uint8_t REG_ALARM_RAW_STATUS = 0x64;
static const uint8_t REG_SAFETY_ALERT_A = 0x02;
static const uint8_t REG_SAFETY_STATUS_A = 0x03;
static const uint8_t REG_SAFETY_ALERT_B = 0x04;
static const uint8_t REG_SAFETY_STATUS_B = 0x05;
static const uint8_t REG_BATTERY_STATUS = 0x12;
static const uint16_t ALARM_STATUS_CB_MASK = 0x0100; // Alarm/AlarmRaw bit8 = CB (cell balancing active)

// Button timings (ms)
static const uint16_t TIME_SHORT = 200;
static const uint16_t TIME_LONG = 2000;

// Hardware UART telemetry.
static const uint32_t SERIAL_BAUD = 115200;
static const uint16_t TELEMETRY_PERIOD_MS = 100; // 10 Hz stream rate
static const uint8_t TELEMETRY_SYNC = 0xA5;
static const uint8_t TELEMETRY_SYNC2 = 0x5A;
static const uint16_t BALANCE_CELL_THRESHOLD_MV = 3600;
static const uint16_t BALANCE_MIN_DELTA_MV = 20;
static const uint32_t BALANCE_RETRIGGER_MS = 18000; // BQ balancing command timeout is about 20s

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
  while (true) {
    delay(1000);
  }
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

static void telemetryTxByte(uint8_t byteVal) {
  Serial.write(byteVal);
}

static uint16_t telemetryFrame = 0;
static uint32_t lastTelemetryMs = 0;
static uint32_t lastBalanceCommandMs = 0;
static bool balancingActive = false;

static void sendTelemetryFrame(
  uint16_t frame,
  uint16_t cell1Mv,
  uint16_t cell2Mv,
  int16_t currentRaw,
  int16_t intTempC,
  uint16_t tsRaw,
  uint8_t socPct,
  uint16_t batteryStatus,
  uint16_t alarmStatus,
  uint16_t alarmRawStatus,
  uint8_t safetyAlertA,
  uint8_t safetyStatusA,
  uint8_t safetyAlertB,
  uint8_t safetyStatusB,
  uint8_t flags
) {
  uint8_t cs = 0;

  telemetryTxByte(TELEMETRY_SYNC);
  cs ^= TELEMETRY_SYNC;
  telemetryTxByte(TELEMETRY_SYNC2);
  cs ^= TELEMETRY_SYNC2;

  telemetryTxByte((uint8_t)(frame & 0xFF));
  cs ^= (uint8_t)(frame & 0xFF);
  telemetryTxByte((uint8_t)(frame >> 8));
  cs ^= (uint8_t)(frame >> 8);

  telemetryTxByte((uint8_t)(cell1Mv & 0xFF));
  cs ^= (uint8_t)(cell1Mv & 0xFF);
  telemetryTxByte((uint8_t)(cell1Mv >> 8));
  cs ^= (uint8_t)(cell1Mv >> 8);

  telemetryTxByte((uint8_t)(cell2Mv & 0xFF));
  cs ^= (uint8_t)(cell2Mv & 0xFF);
  telemetryTxByte((uint8_t)(cell2Mv >> 8));
  cs ^= (uint8_t)(cell2Mv >> 8);

  telemetryTxByte((uint8_t)(currentRaw & 0xFF));
  cs ^= (uint8_t)(currentRaw & 0xFF);
  telemetryTxByte((uint8_t)(((uint16_t)currentRaw) >> 8));
  cs ^= (uint8_t)(((uint16_t)currentRaw) >> 8);
  telemetryTxByte((uint8_t)(intTempC & 0xFF));
  cs ^= (uint8_t)(intTempC & 0xFF);
  telemetryTxByte((uint8_t)(((uint16_t)intTempC) >> 8));
  cs ^= (uint8_t)(((uint16_t)intTempC) >> 8);
  telemetryTxByte((uint8_t)(tsRaw & 0xFF));
  cs ^= (uint8_t)(tsRaw & 0xFF);
  telemetryTxByte((uint8_t)(tsRaw >> 8));
  cs ^= (uint8_t)(tsRaw >> 8);

  telemetryTxByte(socPct);
  cs ^= socPct;
  telemetryTxByte((uint8_t)(batteryStatus & 0xFF));
  cs ^= (uint8_t)(batteryStatus & 0xFF);
  telemetryTxByte((uint8_t)(batteryStatus >> 8));
  cs ^= (uint8_t)(batteryStatus >> 8);

  telemetryTxByte((uint8_t)(alarmStatus & 0xFF));
  cs ^= (uint8_t)(alarmStatus & 0xFF);
  telemetryTxByte((uint8_t)(alarmStatus >> 8));
  cs ^= (uint8_t)(alarmStatus >> 8);

  telemetryTxByte((uint8_t)(alarmRawStatus & 0xFF));
  cs ^= (uint8_t)(alarmRawStatus & 0xFF);
  telemetryTxByte((uint8_t)(alarmRawStatus >> 8));
  cs ^= (uint8_t)(alarmRawStatus >> 8);

  telemetryTxByte(safetyAlertA);
  cs ^= safetyAlertA;
  telemetryTxByte(safetyStatusA);
  cs ^= safetyStatusA;
  telemetryTxByte(safetyAlertB);
  cs ^= safetyAlertB;
  telemetryTxByte(safetyStatusB);
  cs ^= safetyStatusB;

  telemetryTxByte(flags);
  cs ^= flags;

  telemetryTxByte(cs);
}

static void telemetryTask() {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastTelemetryMs) < TELEMETRY_PERIOD_MS) {
    return;
  }
  lastTelemetryMs = now;

  const uint16_t cell1Mv = i2cRead16(REG_CELL1_VOLTAGE);
  const uint16_t cell2Mv = i2cRead16(REG_CELL2_VOLTAGE);
  const int16_t currentRaw = (int16_t)i2cRead16(REG_PACK_CURRENT);
  const int16_t intTempC = (int16_t)i2cRead16(REG_INT_TEMPERATURE);
  const uint16_t tsRaw = i2cRead16(REG_TS_MEASUREMENT);
  const uint16_t alarmStatus = i2cRead16(REG_ALARM_STATUS);
  const uint16_t alarmRawStatus = i2cRead16(REG_ALARM_RAW_STATUS);
  const uint8_t safetyAlertA = (uint8_t)(i2cRead16(REG_SAFETY_ALERT_A) & 0x00FF);
  const uint8_t safetyStatusA = (uint8_t)(i2cRead16(REG_SAFETY_STATUS_A) & 0x00FF);
  const uint8_t safetyAlertB = (uint8_t)(i2cRead16(REG_SAFETY_ALERT_B) & 0x00FF);
  const uint8_t safetyStatusB = (uint8_t)(i2cRead16(REG_SAFETY_STATUS_B) & 0x00FF);
  const uint16_t batteryStatus = i2cRead16(REG_BATTERY_STATUS);
  const uint8_t socPct = 0;

  const bool cellHigh = (cell1Mv > BALANCE_CELL_THRESHOLD_MV) || (cell2Mv > BALANCE_CELL_THRESHOLD_MV);
  balancingActive = false;
  if (!inDeepSleep && cellHigh) {
    uint8_t balanceMask = 0x00;
    if (cell1Mv > (uint16_t)(cell2Mv + BALANCE_MIN_DELTA_MV)) {
      balanceMask = 0x02; // bit1 = first active cell (VC1-VC0)
    } else if (cell2Mv > (uint16_t)(cell1Mv + BALANCE_MIN_DELTA_MV)) {
      balanceMask = 0x04; // bit2 = second active cell
    }

    if (lastBalanceCommandMs == 0 || (uint32_t)(now - lastBalanceCommandMs) >= BALANCE_RETRIGGER_MS) {
      writeSubcommand8(START_BALANCE, balanceMask);
      lastBalanceCommandMs = now;
    }
    // Consider balancing active while conditions hold and command was issued recently.
    if (lastBalanceCommandMs != 0 && (uint32_t)(now - lastBalanceCommandMs) < (BALANCE_RETRIGGER_MS + 2000UL)) {
      balancingActive = true;
    }
  }

  uint8_t flags = 0;
  if (inDeepSleep) flags |= 0x01;
  if (balancingActive) flags |= 0x02;
  if (alarmRawStatus & ALARM_STATUS_CB_MASK) flags |= 0x04;
  if (alarmStatus != 0) flags |= 0x08;
  if (safetyStatusA != 0) flags |= 0x10;
  if (safetyStatusB != 0) flags |= 0x20;
  if (batteryStatus != 0) flags |= 0x40;

  sendTelemetryFrame(
    telemetryFrame++, cell1Mv, cell2Mv, currentRaw, intTempC, tsRaw, socPct,
    batteryStatus, alarmStatus, alarmRawStatus, safetyAlertA, safetyStatusA, safetyAlertB, safetyStatusB, flags
  );
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  #ifdef ENABLE_LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  #endif

  Serial.begin(SERIAL_BAUD);
  Wire.begin();
  Wire.setClock(100000);
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
}

bool lastButtonState = false;
unsigned long lastBtnPressTime = 0;

void readButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {
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
  readButton();
  telemetryTask();
}

