#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/bq76905_service.cpp"
#include "bq76905_service.h"

static TwoWire *BQWire = nullptr;

// Mirrors bq76905-controller register map
static const uint8_t REG_SUBCMD = 0x3E;
static const uint8_t REG_SUBCMD_DATA = 0x60;

static const uint16_t SET_CFGUPDATE = 0x0090;
static const uint16_t EXIT_CFGUPDATE = 0x0092;
static const uint16_t DEEPSLEEP = 0x000F;
static const uint16_t EXIT_DEEPSLEEP = 0x000E;
static const uint16_t SHUTDOWN = 0x0010;

// Host-controlled balancing active-cells subcommand (requires payload bitmask).
// Must be re-issued every ~18s; BQ aborts after ~20s for safety.
static const uint16_t START_BALANCE = 0x0083;

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
static const uint16_t ALARM_STATUS_CB_MASK = 0x0100;

static const uint16_t BALANCE_MIN_DELTA_MV = 20;
static const uint32_t BALANCE_RETRIGGER_MS = 18000; // BQ balancing command timeout is about 20s

static BqSnapshot snap = {};
static uint16_t frameCtr = 0;
static bool inDeepSleep = false;
static bool balanceEnabled = false;
static uint32_t lastBalanceCommandMs = 0;

static uint8_t checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return (uint8_t)(0xFF & ~sum);
}

static void i2c_write_raw(uint8_t reg, const uint8_t *data, uint8_t len) {
  if (!BQWire) {
    return;
  }
  BQWire->beginTransmission(BQ76905_ADDR);
  BQWire->write(reg);
  for (uint8_t i = 0; i < len; i++) {
    BQWire->write(data[i]);
  }
  BQWire->endTransmission();
  delay(2);
}

static uint16_t i2c_read16(uint8_t reg) {
  if (!BQWire) {
    return 0;
  }
  BQWire->beginTransmission(BQ76905_ADDR);
  BQWire->write(reg);
  if (BQWire->endTransmission(false) != 0) {
    return 0;
  }
  if (BQWire->requestFrom((int)BQ76905_ADDR, 2) != 2) {
    return 0;
  }
  uint8_t lo = BQWire->read();
  uint8_t hi = BQWire->read();
  return (uint16_t)lo | ((uint16_t)hi << 8);
}

static void send_subcommand(uint16_t cmd) {
  uint8_t buf[2] = {(uint8_t)(cmd & 0xFF), (uint8_t)(cmd >> 8)};
  i2c_write_raw(REG_SUBCMD, buf, sizeof(buf));
}

static void write_config8(uint16_t addr, uint8_t value) {
  uint8_t payload[3] = {(uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8), value};
  uint8_t cks = checksum(payload, sizeof(payload));
  uint8_t meta[2] = {cks, 0x05};
  i2c_write_raw(REG_SUBCMD, payload, sizeof(payload));
  i2c_write_raw(REG_SUBCMD_DATA, meta, sizeof(meta));
}

static void write_config16(uint16_t addr, uint16_t value) {
  uint8_t payload[4] = {(uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8), (uint8_t)(value & 0xFF),
                        (uint8_t)(value >> 8)};
  uint8_t cks = checksum(payload, sizeof(payload));
  uint8_t meta[2] = {cks, 0x06};
  i2c_write_raw(REG_SUBCMD, payload, sizeof(payload));
  i2c_write_raw(REG_SUBCMD_DATA, meta, sizeof(meta));
}

void bq_service_begin(TwoWire &wire) {
  BQWire = &wire;
}

bool bq_configure() {
  if (!BQWire) {
    return false;
  }
  send_subcommand(SET_CFGUPDATE);
  delay(10);

  write_config8(VcellMode, 0x02);
  write_config8(FETOptions, 0x1C);
  write_config8(EnabledProtectionsA, 0xC0);
  write_config8(EnabledProtectionsB, 0x00);
  write_config8(BalancingConfiguration, 0x02);
  write_config8(BalancingMinTempThreshold, 0xFF);
  write_config8(BalancingMaxTempThreshold, 0x00);
  write_config8(BalancingMaxInternalTemp, 110);
  write_config16(DefaultAlarmMask, 0xFFFF);

  write_config16(CellUndervoltageProtectionThreshold, 3000);
  write_config8(CellUndervoltageProtectionDelay, 10);
  write_config8(CellUndervoltageProtectionRecoveryHysteresis, 2);
  write_config16(CellOvervoltageProtectionThreshold, 4200);
  write_config8(CellOvervoltageProtectionDelay, 10);
  write_config8(CellOvervoltageProtectionRecoveryHysteresis, 2);

  send_subcommand(EXIT_CFGUPDATE);
  delay(100);
  return true;
}

bool bq_send_subcommand(uint16_t cmd) {
  send_subcommand(cmd);
  return true;
}

bool bq_toggle_deepsleep() {
  if (inDeepSleep) {
    send_subcommand(EXIT_DEEPSLEEP);
    inDeepSleep = false;
  } else {
    send_subcommand(DEEPSLEEP);
    send_subcommand(DEEPSLEEP);
    inDeepSleep = true;
  }
  return true;
}

// CB_ACTIVE_CELLS takes the same payload format as a config write: 8-bit value
// to subcommand address with checksum/length at 0x60.
static void send_balance_mask(uint8_t mask) {
  write_config8(START_BALANCE, mask);
}

static void balance_task() {
  if (!balanceEnabled || inDeepSleep) {
    return;
  }
  const uint32_t now = millis();
  if (lastBalanceCommandMs != 0 && (uint32_t)(now - lastBalanceCommandMs) < BALANCE_RETRIGGER_MS) {
    return;
  }
  // Balance whichever cell is higher, if the imbalance is meaningful.
  uint8_t mask = 0x00;
  if (snap.c1_mV > (uint16_t)(snap.c2_mV + BALANCE_MIN_DELTA_MV)) {
    mask = 0x02; // bit1 = first active cell (VC1-VC0)
  } else if (snap.c2_mV > (uint16_t)(snap.c1_mV + BALANCE_MIN_DELTA_MV)) {
    mask = 0x04; // bit2 = second active cell
  }
  send_balance_mask(mask);
  lastBalanceCommandMs = now;
}

bool bq_toggle_balancing() {
  balanceEnabled = !balanceEnabled;
  lastBalanceCommandMs = 0;
  if (!balanceEnabled) {
    send_balance_mask(0x00); // stop immediately
  }
  return balanceEnabled;
}

bool bq_enter_ship_mode() {
  send_subcommand(SHUTDOWN);
  send_subcommand(SHUTDOWN);
  return true;
}

void bq_service_poll() {
  if (!BQWire) {
    return;
  }

  BQWire->beginTransmission(BQ76905_ADDR);
  snap.i2cOk = (BQWire->endTransmission() == 0);

  uint16_t cell1 = i2c_read16(REG_CELL1_VOLTAGE);
  uint16_t cell2 = i2c_read16(REG_CELL2_VOLTAGE);
  bool ok = snap.i2cOk;

  snap.c1_mV = cell1;
  snap.c2_mV = cell2;
  snap.currentRaw = (int16_t)i2c_read16(REG_PACK_CURRENT);
  snap.intTempC = (int16_t)i2c_read16(REG_INT_TEMPERATURE);
  snap.tsRaw = i2c_read16(REG_TS_MEASUREMENT);
  snap.socPct = 0;
  snap.alarmStatus = i2c_read16(REG_ALARM_STATUS);
  snap.alarmRawStatus = i2c_read16(REG_ALARM_RAW_STATUS);
  snap.safetyAlertA = (uint8_t)(i2c_read16(REG_SAFETY_ALERT_A) & 0xFF);
  snap.safetyStatusA = (uint8_t)(i2c_read16(REG_SAFETY_STATUS_A) & 0xFF);
  snap.safetyAlertB = (uint8_t)(i2c_read16(REG_SAFETY_ALERT_B) & 0xFF);
  snap.safetyStatusB = (uint8_t)(i2c_read16(REG_SAFETY_STATUS_B) & 0xFF);
  snap.batteryStatus = i2c_read16(REG_BATTERY_STATUS);

  snap.frame = frameCtr++;
  if (ok) {
    snap.goodReads++;
    snap.lastReadMs = millis();
  } else {
    snap.badReads++;
  }

  snap.alarmSeen |= snap.alarmStatus;
  snap.safetyASeen |= (uint8_t)(snap.safetyAlertA | snap.safetyStatusA);
  snap.safetyBSeen |= (uint8_t)(snap.safetyAlertB | snap.safetyStatusB);
  snap.batterySeen |= snap.batteryStatus;

  balance_task();

  uint8_t flags = 0;
  if (inDeepSleep) {
    flags |= 0x01;
  }
  if (balanceEnabled) {
    flags |= 0x02;
  }
  if (snap.alarmRawStatus & ALARM_STATUS_CB_MASK) {
    flags |= 0x04;
  }
  if (snap.alarmStatus != 0) {
    flags |= 0x08;
  }
  if (snap.safetyStatusA != 0) {
    flags |= 0x10;
  }
  if (snap.safetyStatusB != 0) {
    flags |= 0x20;
  }
  if (snap.batteryStatus != 0) {
    flags |= 0x40;
  }
  snap.flags = flags;
}

const BqSnapshot &bq_snapshot() {
  return snap;
}
