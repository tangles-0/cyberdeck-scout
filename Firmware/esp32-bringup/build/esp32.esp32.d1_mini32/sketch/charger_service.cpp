#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/charger_service.cpp"
#include "charger_service.h"
#include "config.h"
#include <cstring>

static TwoWire *CW = nullptr;

static bool probe_addr(uint8_t addr);

static uint8_t tpsAddr = TPS25751_DEFAULT_ADDR;
static bool tpsPresent = false;
static bool bqPresent = false;
static uint32_t lastI2CrMs = 0;
static uint32_t lastI2CrAttemptMs = 0;

static const uint8_t BQ_VIA_START_REG = 0x1B;
static const uint8_t BQ_VIA_LEN = 0x07; // REG1B..REG21: status and faults only.

struct BqViaSnapshot {
  bool valid;
  uint32_t readMs;
  uint8_t regs[BQ_VIA_LEN];
};

static BqViaSnapshot bqVia = {false, 0, {0}};

static uint32_t lastPollMs = 0;
static bool monitoring_enabled = true;

void charger_set_monitoring_enabled(bool enabled) {
  monitoring_enabled = enabled;
}

static void probe_devices() {
  if (!CW) {
    return;
  }
  if (AUTO_DETECT_TPS25751) {
    tpsPresent = false;
    for (size_t i = 0; i < sizeof(TPS25751_ADDR_CANDIDATES) / sizeof(TPS25751_ADDR_CANDIDATES[0]);
         i++) {
      uint8_t a = TPS25751_ADDR_CANDIDATES[i];
      if (probe_addr(a)) {
        tpsAddr = a;
        tpsPresent = true;
        break;
      }
    }
  } else {
    tpsPresent = probe_addr(tpsAddr);
  }
  bqPresent = probe_addr(BQ25792_ADDR);
}

void charger_reprobe() {
  probe_devices();
}

static bool probe_addr(uint8_t addr) {
  if (!CW) {
    return false;
  }
  CW->beginTransmission(addr);
  return CW->endTransmission() == 0;
}

static bool read_tps_register(uint8_t reg, uint8_t *buf, size_t expectedLen, uint8_t &actualLen) {
  if (!CW || !tpsPresent) {
    return false;
  }
  CW->beginTransmission(tpsAddr);
  CW->write(reg);
  if (CW->endTransmission(false) != 0) {
    return false;
  }
  size_t toRead = 1 + expectedLen;
  size_t got = CW->requestFrom((int)tpsAddr, (int)toRead);
  if (got < 1) {
    return false;
  }
  uint8_t count = CW->read();
  actualLen = (count < expectedLen) ? count : (uint8_t)expectedLen;
  for (uint8_t i = 0; i < actualLen; ++i) {
    if (!CW->available()) {
      return false;
    }
    buf[i] = CW->read();
  }
  while (CW->available()) {
    CW->read();
  }
  return true;
}

static bool write_tps_register(uint8_t reg, const uint8_t *data, size_t len) {
  if (!CW || len > 255) {
    return false;
  }
  CW->beginTransmission(tpsAddr);
  CW->write(reg);
  CW->write((uint8_t)len);
  CW->write(data, len);
  return CW->endTransmission() == 0;
}

static bool read_tps_cmd(uint8_t out[4]) {
  uint8_t actual = 0;
  if (!read_tps_register(0x08, out, 4, actual) || actual < 4) {
    return false;
  }
  return true;
}

static bool tps_cmd_zero(const uint8_t cmd[4]) {
  return cmd[0] == 0 && cmd[1] == 0 && cmd[2] == 0 && cmd[3] == 0;
}

static bool wait_tps_cmd(uint32_t timeoutMs) {
  uint32_t start = millis();
  uint8_t cmd[4] = {0};
  while (millis() - start < timeoutMs) {
    if (!read_tps_cmd(cmd)) {
      return false;
    }
    if (tps_cmd_zero(cmd)) {
      return true;
    }
    if (cmd[0] == '!' && cmd[1] == 'C' && cmd[2] == 'M' && cmd[3] == 'D') {
      return false;
    }
    delay(10);
  }
  return false;
}

static bool write_tps_cmd4(const char cmd[4]) {
  return write_tps_register(0x08, (const uint8_t *)cmd, 4);
}

static bool write_tps_data1(const uint8_t *data, size_t len) {
  return write_tps_register(0x09, data, len);
}

static bool read_tps_data1(uint8_t *out, size_t len) {
  uint8_t actual = 0;
  if (!read_tps_register(0x09, out, len, actual) || actual < len) {
    return false;
  }
  return true;
}

static bool read_bq_reg(uint8_t reg, uint8_t &value) {
  if (!CW || !bqPresent) {
    return false;
  }
  CW->beginTransmission(BQ25792_ADDR);
  CW->write(reg);
  if (CW->endTransmission(false) != 0) {
    return false;
  }
  if (CW->requestFrom((int)BQ25792_ADDR, 1) != 1) {
    return false;
  }
  value = CW->read();
  return true;
}

static bool read_bq_reg16(uint8_t reg, uint16_t &value) {
  if (!CW || !bqPresent) {
    return false;
  }
  CW->beginTransmission(BQ25792_ADDR);
  CW->write(reg);
  if (CW->endTransmission(false) != 0) {
    return false;
  }
  if (CW->requestFrom((int)BQ25792_ADDR, 2) != 2) {
    return false;
  }
  uint8_t lo = CW->read();
  uint8_t hi = CW->read();
  value = (uint16_t)lo | ((uint16_t)hi << 8);
  return true;
}

static uint64_t le_u64(const uint8_t *buf, size_t len) {
  uint64_t v = 0;
  for (size_t i = 0; i < len; ++i) {
    v |= ((uint64_t)buf[i]) << (8 * i);
  }
  return v;
}

static bool read_bq_via_tps(bool &cooldown) {
  cooldown = false;
  if (!tpsPresent) {
    return false;
  }
  uint32_t now = millis();
  if (now - lastI2CrAttemptMs < 5000) {
    cooldown = true;
    return false;
  }
  lastI2CrAttemptMs = now;

  uint8_t data1[3] = {0};
  data1[0] = BQ25792_ADDR & 0x7F;
  data1[1] = BQ_VIA_START_REG;
  data1[2] = BQ_VIA_LEN;
  if (!write_tps_data1(data1, sizeof(data1)) || !write_tps_cmd4("I2Cr")) {
    return false;
  }
  if (!wait_tps_cmd(6000)) {
    return false;
  }
  uint8_t out[1 + BQ_VIA_LEN] = {0};
  if (!read_tps_data1(out, sizeof(out))) {
    return false;
  }
  if (out[0] != 0) {
    return false;
  }
  memcpy(bqVia.regs, out + 1, BQ_VIA_LEN);
  bqVia.valid = true;
  bqVia.readMs = now;
  lastI2CrMs = now;
  return true;
}

void charger_begin(TwoWire &wireI2Ct) {
  CW = &wireI2Ct;
  probe_devices();
}

void charger_poll() {
  if (!CW || !monitoring_enabled) {
    return;
  }
  if (millis() - lastPollMs < 500) {
    return;
  }
  lastPollMs = millis();
  if (!AUTO_DETECT_TPS25751) {
    tpsPresent = probe_addr(tpsAddr);
  }
  bqPresent = probe_addr(BQ25792_ADDR);
}

bool charger_fill_json(char *buf, size_t bufLen) {
  if (!buf || bufLen < 64) {
    return false;
  }

  if (!monitoring_enabled) {
    snprintf(buf, bufLen,
             "{\"charger_monitor_suspended\":true,\"reason\":\"eeprom_flash_or_bus_switch\","
             "\"bus\":\"I2Ct_monitoring_paused\"}");
    return strlen(buf) < bufLen;
  }

  charger_poll();

  uint8_t mode[4] = {0};
  uint8_t act = 0;
  bool have_mode = read_tps_register(0x03, mode, 4, act) && act >= 4;

  uint8_t st[5] = {0};
  bool have_st = read_tps_register(0x1A, st, 5, act) && act >= 5;
  uint64_t statusBits = have_st ? le_u64(st, 5) : 0;

  uint8_t boot[5] = {0};
  bool have_boot = read_tps_register(0x2D, boot, 5, act) && act >= 5;

  bool bqViaCooldown = false;
  bool have_bq_via_read = read_bq_via_tps(bqViaCooldown);
  bool have_bq_via = have_bq_via_read || bqVia.valid;

  uint8_t r1b = 0, r1c = 0, r1d = 0, r1e = 0, r1f = 0, r20 = 0, r21 = 0;
  bool have_bq_dir = false;
  if (bqPresent) {
    have_bq_dir = read_bq_reg(0x1B, r1b) && read_bq_reg(0x1C, r1c) && read_bq_reg(0x20, r20) &&
                  read_bq_reg(0x21, r21);
  }

  int16_t ibus = 0, ibat = 0;
  uint16_t vbat = 0, vsys = 0, vbus = 0;
  bool have_bq_adc = false;
  if (bqPresent) {
    uint16_t rawIbus = 0, rawIbat = 0;
    have_bq_adc = read_bq_reg16(0x31, rawIbus) && read_bq_reg16(0x33, rawIbat) &&
                   read_bq_reg16(0x35, vbus) && read_bq_reg16(0x3B, vbat) &&
                   read_bq_reg16(0x3D, vsys);
    ibus = (int16_t)rawIbus;
    ibat = (int16_t)rawIbat;
  }
  if (bqVia.valid) {
    r1b = bqVia.regs[0x1B - BQ_VIA_START_REG];
    r1c = bqVia.regs[0x1C - BQ_VIA_START_REG];
    r1d = bqVia.regs[0x1D - BQ_VIA_START_REG];
    r1e = bqVia.regs[0x1E - BQ_VIA_START_REG];
    r1f = bqVia.regs[0x1F - BQ_VIA_START_REG];
    r20 = bqVia.regs[0x20 - BQ_VIA_START_REG];
    r21 = bqVia.regs[0x21 - BQ_VIA_START_REG];
  }

  const uint8_t bqChgStat = (r1c >> 5) & 0x07;
  const uint8_t bqVbusStat = (r1c >> 1) & 0x0F;
  const uint32_t inputPowerMw = (ibus > 0) ? ((uint32_t)vbus * (uint32_t)ibus / 1000UL) : 0;
  const uint32_t chargePowerMw = (ibat > 0) ? ((uint32_t)vbat * (uint32_t)ibat / 1000UL) : 0;

  int n = snprintf(
      buf, bufLen,
      "{\"tps_present\":%s,\"tps_addr\":\"0x%02X\",\"bq_present\":%s,"
      "\"have_mode\":%s,\"mode_hex\":\"%02X%02X%02X%02X\","
      "\"have_status\":%s,\"conn_state\":%u,\"vbus_stat\":%u,"
      "\"have_boot_flags\":%s,"
      "\"bq_direct_ok\":%s,\"reg1b\":%u,\"reg1c\":%u,\"reg1d\":%u,\"reg1e\":%u,\"reg1f\":%u,"
      "\"fault0\":%u,\"fault1\":%u,"
      "\"bq_chg_stat\":%u,\"bq_vbus_stat\":%u,"
      "\"bq_vbus_present\":%s,\"bq_power_good\":%s,\"bq_vbat_present\":%s,"
      "\"bq_iindpm\":%s,\"bq_vindpm\":%s,\"bq_treg\":%s,"
      "\"bq_adc_ok\":%s,\"ibus_mA\":%d,\"ibat_mA\":%d,"
      "\"vbat_mV\":%u,\"vsys_mV\":%u,\"vbus_mV\":%u,"
      "\"input_power_mW\":%lu,\"charge_power_mW\":%lu,"
      "\"bq_via_tps_ok\":%s,\"bq_via_read_ok\":%s,\"bq_via_cooldown\":%s,"
      "\"bq_via_r1b\":%u,\"bq_via_r1c\":%u,"
      "\"last_i2cr_age_ms\":%lu,\"last_i2cr_attempt_age_ms\":%lu}",
      tpsPresent ? "true" : "false", tpsAddr, bqPresent ? "true" : "false",
      have_mode ? "true" : "false", have_mode ? mode[0] : 0, have_mode ? mode[1] : 0,
      have_mode ? mode[2] : 0, have_mode ? mode[3] : 0, have_st ? "true" : "false",
      have_st ? (unsigned)((statusBits >> 1) & 0x07) : 0,
      have_st ? (unsigned)((statusBits >> 20) & 0x03) : 0, have_boot ? "true" : "false",
      have_bq_dir ? "true" : "false", (unsigned)r1b, (unsigned)r1c, (unsigned)r1d,
      (unsigned)r1e, (unsigned)r1f, (unsigned)r20, (unsigned)r21, (unsigned)bqChgStat,
      (unsigned)bqVbusStat, (r1b & 0x01) ? "true" : "false",
      (r1b & 0x08) ? "true" : "false", (r1d & 0x01) ? "true" : "false",
      (r1b & 0x80) ? "true" : "false", (r1b & 0x40) ? "true" : "false",
      (r1d & 0x04) ? "true" : "false", have_bq_adc ? "true" : "false", (int)ibus,
      (int)ibat, (unsigned)vbat, (unsigned)vsys, (unsigned)vbus,
      (unsigned long)inputPowerMw, (unsigned long)chargePowerMw, have_bq_via ? "true" : "false",
      have_bq_via_read ? "true" : "false", bqViaCooldown ? "true" : "false",
      bqVia.valid ? (unsigned)bqVia.regs[0] : 0, bqVia.valid ? (unsigned)bqVia.regs[1] : 0,
      (unsigned long)((lastI2CrMs == 0) ? 999999UL : (millis() - lastI2CrMs)),
      (unsigned long)((lastI2CrAttemptMs == 0) ? 999999UL : (millis() - lastI2CrAttemptMs)));

  return n > 0 && (size_t)n < bufLen;
}
