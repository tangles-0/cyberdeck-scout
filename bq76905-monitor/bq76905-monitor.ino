#include <Wire.h>

// BQ76905 I2C address (7-bit). Manual lists 0x10 write / 0x11 read (8-bit).
static const uint8_t kBq76905Addr = 0x08;

// Direct command/register addresses from the BQ76905 TRM.
static const uint8_t kRegSafetyAlertA = 0x02;
static const uint8_t kRegSafetyStatusA = 0x03;
static const uint8_t kRegSafetyAlertB = 0x04;
static const uint8_t kRegSafetyStatusB = 0x05;
static const uint8_t kRegBatteryStatus = 0x12;   // 16-bit
static const uint8_t kRegCellVoltage[5] = {0x14, 0x16, 0x18, 0x1A, 0x1C}; // 16-bit each
static const uint8_t kRegCurrent = 0x3A;         // 16-bit signed, userA (mA default with 1 mOhm)
static const uint8_t kRegAlarmStatus = 0x62;     // 16-bit
static const uint8_t kRegAlarmRawStatus = 0x64;  // 16-bit

static const uint32_t kPollIntervalMs = 20;

struct StatusSnapshot {
  uint8_t safetyAlertA = 0;
  uint8_t safetyStatusA = 0;
  uint8_t safetyAlertB = 0;
  uint8_t safetyStatusB = 0;
  uint16_t batteryStatus = 0;
  uint16_t cellVoltageMv[5] = {0, 0, 0, 0, 0};
  int16_t currentUserA = 0;
  uint16_t alarmStatus = 0;
  uint16_t alarmRawStatus = 0;
  bool valid = false;
};

bool readBytes(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(kBq76905Addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  size_t readCount = Wire.requestFrom(kBq76905Addr, static_cast<uint8_t>(len));
  if (readCount != len) {
    while (Wire.available()) {
      (void)Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

bool readByte(uint8_t reg, uint8_t &value) {
  return readBytes(reg, &value, 1);
}

bool readWordLE(uint8_t reg, uint16_t &value) {
  uint8_t buf[2] = {0, 0};
  if (!readBytes(reg, buf, 2)) {
    return false;
  }
  value = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  return true;
}

bool readInt16LE(uint8_t reg, int16_t &value) {
  uint16_t raw = 0;
  if (!readWordLE(reg, raw)) {
    return false;
  }
  value = static_cast<int16_t>(raw);
  return true;
}

void printHeader(const char *label) {
  Serial.print('[');
  Serial.print(millis());
  Serial.print(" ms] ");
  Serial.println(label);
}

void printBits8(uint8_t value, const char *const *names, uint8_t count) {
  for (uint8_t bit = 0; bit < count; ++bit) {
    Serial.print("  ");
    Serial.print(names[bit]);
    Serial.print(": ");
    Serial.println((value >> bit) & 0x01);
  }
}

void printBatteryStatus(uint16_t value) {
  Serial.print("Battery Status (0x12): raw=0x");
  if (value < 0x1000) Serial.print('0');
  if (value < 0x100) Serial.print('0');
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
  Serial.print(" bits=");

  bool any = false;
  auto emit = [&](const char *name, uint8_t bit) {
    if (value & (1u << bit)) {
      if (any) Serial.print('|');
      Serial.print(name);
      any = true;
    }
  };

  emit("SLEEP", 15);
  emit("DEEPSLEEP", 14);
  emit("SA", 13);
  emit("SS", 12);
  emit("SEC1", 11);
  emit("SEC0", 10);
  emit("FET_EN", 8);
  emit("POR", 7);
  emit("SLEEP_EN", 6);
  emit("CFGUPDATE", 5);
  emit("ALERTPIN", 4);
  emit("CHG", 3);
  emit("DSG", 2);
  emit("CHGDETFLAG", 1);

  if (!any) {
    Serial.print("none");
  }
  Serial.println();
}

void printAlarmStatus(uint16_t value, const char *label, bool rawHasCdRaw) {
  Serial.print(label);
  Serial.print(" raw=0x");
  if (value < 0x1000) Serial.print('0');
  if (value < 0x100) Serial.print('0');
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
  Serial.print(" bits=");

  bool any = false;
  auto emit = [&](const char *name, uint8_t bit) {
    if (value & (1u << bit)) {
      if (any) Serial.print('|');
      Serial.print(name);
      any = true;
    }
  };

  emit("SSA", 15);
  emit("SSB", 14);
  emit("SAA", 13);
  emit("SAB", 12);
  emit("XCHG", 11);
  emit("XDSG", 10);
  emit("SHUTV", 9);
  emit("CB", 8);
  emit("FULLSCAN", 7);
  emit("ADSCAN", 6);
  emit("WAKE", 5);
  emit("SLEEP", 4);
  emit("TIMER_ALARM", 3);
  emit("INITCOMP", 2);
  if (rawHasCdRaw) {
    emit("CDRAW", 1);
  } else {
    emit("CDTOGGLE", 1);
  }
  emit("POR", 0);

  if (!any) {
    Serial.print("none");
  }
  Serial.println();
}

void printSafetyAlertA(uint8_t value) {
  Serial.print("Safety Alert A (0x02): raw=0x");
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
  Serial.print(" bits=");
  bool any = false;
  auto emit = [&](const char *name, uint8_t bit) {
    if (value & (1u << bit)) {
      if (any) Serial.print('|');
      Serial.print(name);
      any = true;
    }
  };
  emit("OCC", 2);
  emit("OCD2", 3);
  emit("OCD1", 4);
  emit("SCD", 5);
  emit("CUV", 6);
  emit("COV", 7);
  if (!any) {
    Serial.print("none");
  }
  Serial.println();
}

void printSafetyStatusA(uint8_t value) {
  Serial.print("Safety Status A (0x03): raw=0x");
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
  Serial.print(" bits=");
  bool any = false;
  auto emit = [&](const char *name, uint8_t bit) {
    if (value & (1u << bit)) {
      if (any) Serial.print('|');
      Serial.print(name);
      any = true;
    }
  };
  emit("REGOUT", 0);
  emit("CURLATCH", 1);
  emit("OCC", 2);
  emit("OCD2", 3);
  emit("OCD1", 4);
  emit("SCD", 5);
  emit("CUV", 6);
  emit("COV", 7);
  if (!any) {
    Serial.print("none");
  }
  Serial.println();
}

void printSafetyAlertB(uint8_t value) {
  Serial.print("Safety Alert B (0x04): raw=0x");
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
  Serial.print(" bits=");
  bool any = false;
  auto emit = [&](const char *name, uint8_t bit) {
    if (value & (1u << bit)) {
      if (any) Serial.print('|');
      Serial.print(name);
      any = true;
    }
  };
  emit("VSS", 0);
  emit("VREF", 1);
  emit("HWD", 2);
  emit("OTINT", 3);
  emit("UTC", 4);
  emit("UTD", 5);
  emit("OTC", 6);
  emit("OTD", 7);
  if (!any) {
    Serial.print("none");
  }
  Serial.println();
}

void printSafetyStatusB(uint8_t value) {
  Serial.print("Safety Status B (0x05): raw=0x");
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
  Serial.print(" bits=");
  bool any = false;
  auto emit = [&](const char *name, uint8_t bit) {
    if (value & (1u << bit)) {
      if (any) Serial.print('|');
      Serial.print(name);
      any = true;
    }
  };
  emit("VSS", 0);
  emit("VREF", 1);
  emit("HWD", 2);
  emit("OTINT", 3);
  emit("UTC", 4);
  emit("UTD", 5);
  emit("OTC", 6);
  emit("OTD", 7);
  if (!any) {
    Serial.print("none");
  }
  Serial.println();
}

void printCurrentVoltages(const uint16_t *cellMv, int16_t currentUserA) {
    Serial.print("Current: ");
    Serial.print(currentUserA);
    Serial.print("mA, S1: ");
    Serial.print(cellMv[0] / 1000.0);
    Serial.print("V, S2: ");
    Serial.print(cellMv[4] / 1000.0);
    Serial.println("V");
}

void printCurrent(int16_t currentUserA) {

}

bool readSnapshot(StatusSnapshot &snapshot) {
  uint8_t safetyAlertA = 0;
  uint8_t safetyStatusA = 0;
  uint8_t safetyAlertB = 0;
  uint8_t safetyStatusB = 0;
  uint16_t batteryStatus = 0;
  uint16_t cellVoltageMv[5] = {0, 0, 0, 0, 0};
  int16_t currentUserA = 0;
  uint16_t alarmStatus = 0;
  uint16_t alarmRawStatus = 0;

  if (!readByte(kRegSafetyAlertA, safetyAlertA)) return false;
  if (!readByte(kRegSafetyStatusA, safetyStatusA)) return false;
  if (!readByte(kRegSafetyAlertB, safetyAlertB)) return false;
  if (!readByte(kRegSafetyStatusB, safetyStatusB)) return false;
  if (!readWordLE(kRegBatteryStatus, batteryStatus)) return false;
  for (uint8_t i = 0; i < 5; ++i) {
    if (!readWordLE(kRegCellVoltage[i], cellVoltageMv[i])) return false;
  }
  if (!readInt16LE(kRegCurrent, currentUserA)) return false;
  if (!readWordLE(kRegAlarmStatus, alarmStatus)) return false;
  if (!readWordLE(kRegAlarmRawStatus, alarmRawStatus)) return false;

  snapshot.safetyAlertA = safetyAlertA;
  snapshot.safetyStatusA = safetyStatusA;
  snapshot.safetyAlertB = safetyAlertB;
  snapshot.safetyStatusB = safetyStatusB;
  snapshot.batteryStatus = batteryStatus;
  for (uint8_t i = 0; i < 5; ++i) {
    snapshot.cellVoltageMv[i] = cellVoltageMv[i];
  }
  snapshot.currentUserA = currentUserA;
  snapshot.alarmStatus = alarmStatus;
  snapshot.alarmRawStatus = alarmRawStatus;
  snapshot.valid = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin();
  Wire.setClock(400000);

  Serial.println("BQ76905 status monitor ready, waiting to start...");
}

unsigned int long lastStartupTime = 0;
unsigned int long lastErrorPrint = 0;
bool startupPrinted = false;
void loop() {
  static StatusSnapshot last;
  StatusSnapshot current;

  if (millis() < 5 * 1000) {
    if (millis() - lastStartupTime > 1000) {
      Serial.print(".");
      lastStartupTime = millis();
    }
    return;
  }

  if (!startupPrinted) {
    Serial.println("BQ76905 status monitor starting...");
    startupPrinted = true;
  }

  if (!readSnapshot(current)) {
    if (millis() - lastErrorPrint > 1000) {
      Serial.println("I2C read error; check wiring/address.");
      lastErrorPrint = millis();
    }
    delay(kPollIntervalMs);
    return;
  }

  bool changed = !last.valid ||
                 current.safetyAlertA != last.safetyAlertA ||
                 current.safetyStatusA != last.safetyStatusA ||
                 current.safetyAlertB != last.safetyAlertB ||
                 current.safetyStatusB != last.safetyStatusB ||
                 current.batteryStatus != last.batteryStatus ||
                 //current.currentUserA != last.currentUserA ||
                 current.alarmStatus != last.alarmStatus ||
                 current.alarmRawStatus != last.alarmRawStatus;
  if (!last.valid) {
    changed = true;
  }
//   else {
//     for (uint8_t i = 0; i < 5; ++i) {
//       if (current.cellVoltageMv[i] != last.cellVoltageMv[i]) {
//         changed = true;
//         break;
//       }
//     }
//   }

  if (millis() - lastStartupTime > 3000) {
    printCurrentVoltages(current.cellVoltageMv, current.currentUserA);
    lastStartupTime = millis();
  }
  
  if (changed) {
    printHeader("BQ76905 status changed");

    if (!last.valid || current.batteryStatus != last.batteryStatus) {
      printBatteryStatus(current.batteryStatus);
    }
    if (!last.valid || current.safetyAlertA != last.safetyAlertA) {
      printSafetyAlertA(current.safetyAlertA);
    }
    if (!last.valid || current.safetyStatusA != last.safetyStatusA) {
      printSafetyStatusA(current.safetyStatusA);
    }
    if (!last.valid || current.safetyAlertB != last.safetyAlertB) {
      printSafetyAlertB(current.safetyAlertB);
    }
    if (!last.valid || current.safetyStatusB != last.safetyStatusB) {
      printSafetyStatusB(current.safetyStatusB);
    }
    if (!last.valid || current.alarmStatus != last.alarmStatus) {
      printAlarmStatus(current.alarmStatus, "Alarm Status (0x62):", false);
    }
    if (!last.valid || current.alarmRawStatus != last.alarmRawStatus) {
      printAlarmStatus(current.alarmRawStatus, "Alarm Raw Status (0x64):", true);
    }
  }

  last = current;
  delay(kPollIntervalMs);
}

