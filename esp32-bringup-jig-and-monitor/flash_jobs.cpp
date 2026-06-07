#include "flash_jobs.h"
#include "config.h"
#include "charger_service.h"
#include "updi_programmer.h"
#include <FS.h>
#include <LittleFS.h>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** Shared TwoWire(1): normally on I2Ct; EEPROM job switches pins to I2Cc */
static TwoWire *SharedBus = nullptr;
static TwoWire *EW = nullptr;

static const char *PATH_EEPROM = "/stg_eeprom.bin";
static const char *PATH_ATTINY = "/stg_attiny.bin";

static int g_progress = 0;
static bool g_busy = false;
static char g_phase[40] = "idle";
static char g_err[128] = "";
static bool g_last_ok = false;

static void set_phase(const char *p) {
  strncpy(g_phase, p ? p : "?", sizeof(g_phase) - 1);
  g_phase[sizeof(g_phase) - 1] = 0;
}

static void set_err(const char *e) {
  strncpy(g_err, e ? e : "", sizeof(g_err) - 1);
  g_err[sizeof(g_err) - 1] = 0;
}

static bool wait_eeprom_ack(uint32_t timeoutMs = 20) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    EW->beginTransmission(EEPROM_AT24_ADDR);
    if (EW->endTransmission() == 0) {
      return true;
    }
    delay(1);
  }
  return false;
}

static bool eeprom_write_page(uint16_t memAddr, const uint8_t *data, size_t len) {
  EW->beginTransmission(EEPROM_AT24_ADDR);
  EW->write((uint8_t)(memAddr >> 8));
  EW->write((uint8_t)(memAddr & 0xFF));
  EW->write(data, len);
  return EW->endTransmission() == 0;
}

static bool eeprom_read(uint16_t memAddr, uint8_t *out, size_t len) {
  EW->beginTransmission(EEPROM_AT24_ADDR);
  EW->write((uint8_t)(memAddr >> 8));
  EW->write((uint8_t)(memAddr & 0xFF));
  if (EW->endTransmission(false) != 0) {
    return false;
  }
  size_t got = EW->requestFrom((int)EEPROM_AT24_ADDR, (int)len);
  if (got != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    out[i] = EW->read();
  }
  return true;
}

static bool write_file_to_eeprom(File &file) {
  uint8_t pageBuf[EEPROM_PAGE_BYTES];
  uint8_t verifyBuf[EEPROM_PAGE_BYTES];
  uint32_t totalWritten = 0;
  const uint32_t fileSize = file.size() ? file.size() : 1;

  while (file.available()) {
    size_t toRead = file.read(pageBuf, sizeof(pageBuf));
    if (toRead == 0) {
      break;
    }
    if (totalWritten + toRead > EEPROM_SIZE_BYTES) {
      set_err("File larger than EEPROM");
      return false;
    }

    uint16_t pageOffset = totalWritten % EEPROM_PAGE_BYTES;
    uint16_t spaceInPage = EEPROM_PAGE_BYTES - pageOffset;
    size_t chunkLen = (toRead > spaceInPage) ? spaceInPage : toRead;

    size_t consumed = 0;
    while (consumed < toRead) {
      uint16_t addr = (uint16_t)(totalWritten + consumed);
      size_t writeLen = min((size_t)spaceInPage, toRead - consumed);

      if (!eeprom_write_page(addr, pageBuf + consumed, writeLen)) {
        set_err("I2C write failed");
        return false;
      }
      if (!wait_eeprom_ack(20)) {
        set_err("EEPROM write timeout");
        return false;
      }
      if (!eeprom_read(addr, verifyBuf, writeLen)) {
        set_err("Verify read failed");
        return false;
      }
      if (memcmp(verifyBuf, pageBuf + consumed, writeLen) != 0) {
        set_err("Verify mismatch");
        return false;
      }

      consumed += writeLen;
      totalWritten += writeLen;
      g_progress = (int)(totalWritten * 100UL / fileSize);
      pageOffset = totalWritten % EEPROM_PAGE_BYTES;
      spaceInPage = EEPROM_PAGE_BYTES - pageOffset;
    }

    if (toRead > chunkLen) {
      memmove(pageBuf, pageBuf + chunkLen, toRead - chunkLen);
      size_t overflow = toRead - chunkLen;
      size_t offset = 0;
      while (offset < overflow) {
        uint16_t addr = (uint16_t)(totalWritten + offset);
        size_t writeLen = min((size_t)EEPROM_PAGE_BYTES, overflow - offset);

        if (!eeprom_write_page(addr, pageBuf + offset, writeLen)) {
          set_err("I2C write failed");
          return false;
        }
        if (!wait_eeprom_ack(20)) {
          set_err("EEPROM write timeout");
          return false;
        }
        if (!eeprom_read(addr, verifyBuf, writeLen)) {
          set_err("Verify read failed");
          return false;
        }
        if (memcmp(verifyBuf, pageBuf + offset, writeLen) != 0) {
          set_err("Verify mismatch");
          return false;
        }

        offset += writeLen;
        totalWritten += writeLen;
        g_progress = (int)(totalWritten * 100UL / fileSize);
      }
    }
  }
  return true;
}

static void restore_i2ct_bus() {
  if (!SharedBus) {
    return;
  }
  SharedBus->end();
  delay(10);
  SharedBus->begin(PIN_I2C_I2CT_SDA, PIN_I2C_I2CT_SCL, I2C_I2CT_HZ);
  EW = SharedBus;
  charger_reprobe();
  charger_set_monitoring_enabled(true);
}

static void task_eeprom(void *) {
  g_busy = true;
  g_progress = 0;
  set_err("");
  set_phase("eeprom_flash");

  if (!SharedBus) {
    set_err("No I2C bus");
    g_last_ok = false;
    g_busy = false;
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  charger_set_monitoring_enabled(false);
  SharedBus->end();
  delay(10);
  SharedBus->begin(PIN_I2C_I2CC_SDA, PIN_I2C_I2CC_SCL, I2C_I2CC_HZ);
  EW = SharedBus;
  set_phase("i2cc_eeprom");

  if (!LittleFS.exists(PATH_EEPROM)) {
    set_err("No staged file; upload first");
    g_last_ok = false;
    g_busy = false;
    restore_i2ct_bus();
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  File f = LittleFS.open(PATH_EEPROM, "r");
  if (!f) {
    set_err("Open staged file failed");
    g_last_ok = false;
    g_busy = false;
    restore_i2ct_bus();
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  uint32_t sz = f.size();
  if (sz > EEPROM_SIZE_BYTES) {
    set_err("Image too large");
    f.close();
    g_last_ok = false;
    g_busy = false;
    restore_i2ct_bus();
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  EW->beginTransmission(EEPROM_AT24_ADDR);
  bool present = (EW->endTransmission() == 0);
  if (!present) {
    set_err("EEPROM not detected on I2Cc — power TPS down per procedure");
    f.close();
    g_last_ok = false;
    g_busy = false;
    restore_i2ct_bus();
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  bool ok = write_file_to_eeprom(f);
  f.close();
  g_last_ok = ok;
  if (!ok && strlen(g_err) == 0) {
    set_err("Write failed");
  }
  g_progress = ok ? 100 : g_progress;
  restore_i2ct_bus();
  g_busy = false;
  set_phase(ok ? "idle" : "error");
  vTaskDelete(nullptr);
}

static void updi_progress_cb(int pct, const char *phase, void *) {
  g_progress = pct;
  set_phase(phase);
}

static void task_attiny(void *) {
  g_busy = true;
  g_progress = 0;
  set_err("");
  set_phase("attiny_flash");

  if (!LittleFS.exists(PATH_ATTINY)) {
    set_err("No staged file; upload first");
    g_last_ok = false;
    g_busy = false;
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  File f = LittleFS.open(PATH_ATTINY, "r");
  if (!f) {
    set_err("Open staged file failed");
    g_last_ok = false;
    g_busy = false;
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  size_t sz = f.size();
  if (sz == 0 || sz > ATTINY202_FLASH_SIZE) {
    set_err("Bad ATTiny image size");
    f.close();
    g_last_ok = false;
    g_busy = false;
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  uint8_t *buf = (uint8_t *)malloc(sz);
  if (!buf) {
    set_err("malloc failed");
    f.close();
    g_last_ok = false;
    g_busy = false;
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }

  if (f.read(buf, sz) != sz) {
    set_err("Read staged file failed");
    free(buf);
    f.close();
    g_last_ok = false;
    g_busy = false;
    set_phase("idle");
    vTaskDelete(nullptr);
    return;
  }
  f.close();

  UPDIResult r = updi_program_flash(buf, sz, updi_progress_cb, nullptr);
  free(buf);

  if (r != UPDI_RESULT_OK) {
    set_err(updi_result_string(r));
    g_last_ok = false;
  } else {
    g_last_ok = true;
    set_err("");
  }
  g_progress = g_last_ok ? 100 : g_progress;
  g_busy = false;
  set_phase("idle");
  vTaskDelete(nullptr);
}

void flash_jobs_begin(TwoWire &sharedSecondPeripheral) {
  SharedBus = &sharedSecondPeripheral;
  EW = SharedBus;
}

bool flash_jobs_start_eeprom_flash() {
  if (g_busy) {
    return false;
  }
  BaseType_t ok = xTaskCreatePinnedToCore(task_eeprom, "eep_flash", 8192, nullptr, 3, nullptr, 0);
  return ok == pdPASS;
}

bool flash_jobs_start_attiny_flash() {
  if (g_busy) {
    return false;
  }
  BaseType_t ok = xTaskCreatePinnedToCore(task_attiny, "updi_flash", 12288, nullptr, 3, nullptr, 0);
  return ok == pdPASS;
}

bool flash_jobs_status_json(char *buf, size_t bufLen) {
  char errEsc[160];
  size_t j = 0;
  for (size_t i = 0; g_err[i] && j + 1 < sizeof(errEsc); i++) {
    if (g_err[i] == '"' || g_err[i] == '\\') {
      errEsc[j++] = '\'';
    } else {
      errEsc[j++] = g_err[i];
    }
  }
  errEsc[j] = 0;
  int n = snprintf(buf, bufLen,
                     "{\"busy\":%s,\"progress\":%d,\"phase\":\"%s\",\"last_ok\":%s,\"error\":\"%s\","
                     "\"paths\":{\"eeprom\":\"%s\",\"attiny\":\"%s\"}}",
                     g_busy ? "true" : "false", g_progress, g_phase, g_last_ok ? "true" : "false",
                     errEsc, PATH_EEPROM, PATH_ATTINY);
  return n > 0 && (size_t)n < bufLen;
}
