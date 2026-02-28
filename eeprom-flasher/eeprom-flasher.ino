// ESP32 sketch: copy a binary file from LittleFS to AT24C EEPROM over I2C.
// Place the .bin file in the sketch's data/ folder and upload using
// the arduino-littlefs-upload plugin.

#include <Arduino.h>
#include <Wire.h>
#include <FS.h>
#include <LittleFS.h>

// ---------------- User configuration ----------------
// I2C pins (ESP32 default is SDA=21, SCL=22). Override if needed.
static const int I2C_SDA_PIN = 21;
static const int I2C_SCL_PIN = 22;

// I2C clock; AT24C typically supports 100k or 400k.
static const uint32_t I2C_CLOCK_HZ = 400000;
// Fallback clock for stubborn buses (e.g. strong pull-ups or long wires).
static const uint32_t I2C_FALLBACK_CLOCK_HZ = 100000;

// 7-bit I2C base address of AT24C (often 0x50, depends on A0/A1/A2 pins).
static const uint8_t AT24C_ADDR = 0x50;

// Total EEPROM size in bytes (adjust to your part).
// Example: 24C256 = 32768 bytes (~32 KB).
static const uint32_t EEPROM_SIZE_BYTES = 32768;

// Page size in bytes (adjust to your part).
// Example: 24C256 uses 64-byte pages.
static const uint16_t EEPROM_PAGE_BYTES = 64;

// Input file stored in LittleFS.
static const char *BIN_PATH = "/full-flash.bin";

// Optional: write verification by reading back and comparing.
static const bool VERIFY_AFTER_WRITE = true;
static const bool ENABLE_I2C_SCAN = true;
// ----------------------------------------------------

static bool i2cWritePage(uint16_t memAddr, const uint8_t *data, size_t len) {
  Wire.beginTransmission(AT24C_ADDR);
  Wire.write((uint8_t)(memAddr >> 8));
  Wire.write((uint8_t)(memAddr & 0xFF));
  Wire.write(data, len);
  uint8_t status = Wire.endTransmission();
  return status == 0;
}

static bool waitForWriteComplete(uint32_t timeoutMs = 10) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    Wire.beginTransmission(AT24C_ADDR);
    uint8_t status = Wire.endTransmission();
    if (status == 0) {
      return true;
    }
    delay(1);
  }
  return false;
}

static bool i2cReadBytes(uint16_t memAddr, uint8_t *out, size_t len) {
  Wire.beginTransmission(AT24C_ADDR);
  Wire.write((uint8_t)(memAddr >> 8));
  Wire.write((uint8_t)(memAddr & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  size_t got = Wire.requestFrom((int)AT24C_ADDR, (int)len);
  if (got != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    out[i] = (uint8_t)Wire.read();
  }
  return true;
}

static bool probeAddress(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void scanI2CBus() {
  Serial.println("I2C scan:");
  bool any = false;
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    if (probeAddress(addr)) {
      Serial.printf("  - 0x%02X\n", addr);
      any = true;
    }
  }
  if (!any) {
    Serial.println("  (no devices found)");
  }
}

static bool ensureEepromPresent(uint32_t clockHz) {
  Wire.end();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, clockHz);
  delay(10);
  if (ENABLE_I2C_SCAN) {
    scanI2CBus();
  }
  return probeAddress(AT24C_ADDR);
}

static bool writeFileToEeprom(File &file) {
  uint8_t pageBuf[EEPROM_PAGE_BYTES];
  uint8_t verifyBuf[EEPROM_PAGE_BYTES];

  uint32_t totalWritten = 0;
  while (file.available()) {
    size_t toRead = file.read(pageBuf, sizeof(pageBuf));
    if (toRead == 0) {
      break;
    }
    if (totalWritten + toRead > EEPROM_SIZE_BYTES) {
      Serial.println("ERROR: File too large for EEPROM.");
      return false;
    }

    // Ensure we don't cross a page boundary.
    uint16_t pageOffset = totalWritten % EEPROM_PAGE_BYTES;
    uint16_t spaceInPage = EEPROM_PAGE_BYTES - pageOffset;
    size_t chunkLen = (toRead > spaceInPage) ? spaceInPage : toRead;

    size_t consumed = 0;
    while (consumed < toRead) {
      uint16_t addr = (uint16_t)(totalWritten + consumed);
      size_t writeLen = min((size_t)spaceInPage, toRead - consumed);

      if (!i2cWritePage(addr, pageBuf + consumed, writeLen)) {
        Serial.println("ERROR: I2C write failed.");
        return false;
      }
      if (!waitForWriteComplete(20)) {
        Serial.println("ERROR: EEPROM write timeout.");
        return false;
      }

      if (VERIFY_AFTER_WRITE) {
        if (!i2cReadBytes(addr, verifyBuf, writeLen)) {
          Serial.println("ERROR: I2C read failed.");
          return false;
        }
        if (memcmp(verifyBuf, pageBuf + consumed, writeLen) != 0) {
          Serial.println("ERROR: Verify mismatch.");
          return false;
        }
      }

      consumed += writeLen;
      totalWritten += writeLen;
      pageOffset = totalWritten % EEPROM_PAGE_BYTES;
      spaceInPage = EEPROM_PAGE_BYTES - pageOffset;
    }

    // If we read more than the remaining page, stash the extra bytes.
    if (toRead > chunkLen) {
      // Move the overflow bytes to the start of the buffer
      memmove(pageBuf, pageBuf + chunkLen, toRead - chunkLen);
      size_t overflow = toRead - chunkLen;

      // Continue writing overflow bytes aligned to new pages
      size_t offset = 0;
      while (offset < overflow) {
        uint16_t addr = (uint16_t)(totalWritten + offset);
        size_t writeLen = min((size_t)EEPROM_PAGE_BYTES, overflow - offset);

        if (!i2cWritePage(addr, pageBuf + offset, writeLen)) {
          Serial.println("ERROR: I2C write failed.");
          return false;
        }
        if (!waitForWriteComplete(20)) {
          Serial.println("ERROR: EEPROM write timeout.");
          return false;
        }
        if (VERIFY_AFTER_WRITE) {
          if (!i2cReadBytes(addr, verifyBuf, writeLen)) {
            Serial.println("ERROR: I2C read failed.");
            return false;
          }
          if (memcmp(verifyBuf, pageBuf + offset, writeLen) != 0) {
            Serial.println("ERROR: Verify mismatch.");
            return false;
          }
        }

        offset += writeLen;
        totalWritten += writeLen;
      }
    }

    if ((totalWritten % 1024) == 0) {
      Serial.printf("Progress: %lu bytes written\n",
                    (unsigned long)totalWritten);
    }
  }

  Serial.printf("Done. %lu bytes written to EEPROM.\n",
                (unsigned long)totalWritten);
  return true;
}

bool writeSuccessful = false;
void writeEEPROM () {
  Serial.println("EEPROM flasher starting...");
  if (!ensureEepromPresent(I2C_CLOCK_HZ)) {
    Serial.printf("WARN: EEPROM 0x%02X not seen at %lu Hz, trying %lu Hz...\n",
                  AT24C_ADDR, (unsigned long)I2C_CLOCK_HZ, (unsigned long)I2C_FALLBACK_CLOCK_HZ);
    if (!ensureEepromPresent(I2C_FALLBACK_CLOCK_HZ)) {
      Serial.printf("ERROR: EEPROM 0x%02X not detected on I2C.\n", AT24C_ADDR);
      return;
    }
  }

  if (!LittleFS.begin()) {
    Serial.println("ERROR: LittleFS mount failed.");
    return;
  }

  if (!LittleFS.exists(BIN_PATH)) {
    Serial.print("ERROR: File not found: ");
    Serial.println(BIN_PATH);
    return;
  }

  File binFile = LittleFS.open(BIN_PATH, "r");
  if (!binFile) {
    Serial.println("ERROR: Failed to open file.");
    return;
  }

  uint32_t fileSize = binFile.size();
  Serial.printf("File size: %lu bytes\n", (unsigned long)fileSize);

  if (fileSize > EEPROM_SIZE_BYTES) {
    Serial.println("ERROR: File is larger than EEPROM size.");
    binFile.close();
    return;
  }

  bool ok = writeFileToEeprom(binFile);
  binFile.close();

  if (ok) {
    Serial.println("EEPROM flashing complete.");
    writeSuccessful = true;
  } else {
    Serial.println("EEPROM flashing failed.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
}

unsigned int long lastFlashTry = 0;
void loop() {
  
  if (millis() - lastFlashTry > 5000) {
    if (writeSuccessful) {
      Serial.println("EEPROM was flashed successfully!");
    } else {
      writeEEPROM();
    }
    lastFlashTry = millis();
  }

}

