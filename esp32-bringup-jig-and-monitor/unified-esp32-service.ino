/**
 * Unified Cyberdeck factory/diagnostic ESP32 service.
 *
 * Tabs: BQ76905 monitor (direct I2C), TPS25751/BQ25792 charger monitor, firmware flashing.
 *
 * Set Wi-Fi and I2C/UPDI pins in config.h before building.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <LittleFS.h>

#include "config.h"
#include "bq76905_service.h"
#include "charger_service.h"
#include "flash_jobs.h"
#include "html_ui.h"

static WebServer server(80);

static TwoWire BQI2C = TwoWire(0);
static TwoWire ChgI2C = TwoWire(1);

static File uploadFile;

static void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

static void handleApiBq() {
  const BqSnapshot &s = bq_snapshot();
  const uint32_t age =
      (s.lastReadMs == 0) ? 0xFFFFFFFFUL : (uint32_t)(millis() - s.lastReadMs);
  char body[640];
  snprintf(
      body, sizeof(body),
      "{\"frame\":%u,\"c1_mV\":%u,\"c2_mV\":%u,\"current_raw\":%d,\"int_temp_c\":%d,\"ts_raw\":%u,"
      "\"soc_pct\":%u,"
      "\"battery_status\":%u,\"alarm_status\":%u,\"alarm_raw_status\":%u,"
      "\"safety_alert_a\":%u,\"safety_status_a\":%u,\"safety_alert_b\":%u,\"safety_status_b\":%u,"
      "\"alarm_seen\":%u,\"safety_a_seen\":%u,\"safety_b_seen\":%u,\"battery_seen\":%u,"
      "\"flags\":%u,\"deep_sleep\":%s,\"balance_gate\":%s,"
      "\"bq_balancing\":%s,\"alarm\":%s,\"safety_a\":%s,\"safety_b\":%s,"
      "\"good_packets\":%lu,\"bad_packets\":%lu,\"last_packet_age_ms\":%lu,\"i2c_ok\":%s}",
      s.frame, s.c1_mV, s.c2_mV, s.currentRaw, s.intTempC, s.tsRaw, s.socPct, s.batteryStatus,
      s.alarmStatus, s.alarmRawStatus, s.safetyAlertA, s.safetyStatusA, s.safetyAlertB,
      s.safetyStatusB, s.alarmSeen, s.safetyASeen, s.safetyBSeen, s.batterySeen, s.flags,
      (s.flags & 0x01) ? "true" : "false", (s.flags & 0x02) ? "true" : "false",
      (s.flags & 0x04) ? "true" : "false", (s.flags & 0x08) ? "true" : "false",
      (s.flags & 0x10) ? "true" : "false", (s.flags & 0x20) ? "true" : "false",
      (unsigned long)s.goodReads, (unsigned long)s.badReads, (unsigned long)age,
      s.i2cOk ? "true" : "false");
  server.send(200, "application/json", body);
}

static void handleApiCharger() {
  char buf[3072];
  if (!charger_fill_json(buf, sizeof(buf))) {
    server.send(500, "application/json", "{\"error\":\"json_overflow\"}");
    return;
  }
  server.send(200, "application/json", buf);
}

static void handleApiFlashStatus() {
  char buf[512];
  if (!flash_jobs_status_json(buf, sizeof(buf))) {
    server.send(500, "application/json", "{\"error\":\"overflow\"}");
    return;
  }
  server.send(200, "application/json", buf);
}

static void handleBqAction() {
  if (!server.hasArg("cmd")) {
    server.send(400, "application/json", "{\"error\":\"missing cmd\"}");
    return;
  }
  String c = server.arg("cmd");
  if (c == "toggle_sleep") {
    bq_toggle_deepsleep();
  } else if (c == "ship" || c == "shutdown") {
    bq_enter_ship_mode();
  } else if (c == "reconfig") {
    bq_configure();
  } else {
    server.send(400, "application/json", "{\"error\":\"bad cmd\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleUploadEeprom() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    LittleFS.remove("/stg_eeprom.bin");
    uploadFile = LittleFS.open("/stg_eeprom.bin", "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(up.buf, up.currentSize);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
    }
  }
}

static void handleUploadAttiny() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    LittleFS.remove("/stg_attiny.bin");
    uploadFile = LittleFS.open("/stg_attiny.bin", "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(up.buf, up.currentSize);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
    }
  }
}

static void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed — AP fallback not enabled; fix credentials in config.h");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("unified-esp32-service");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  BQI2C.begin(PIN_I2C_BQ_SDA, PIN_I2C_BQ_SCL, I2C_BQ_HZ);
  ChgI2C.begin(PIN_I2C_I2CT_SDA, PIN_I2C_I2CT_SCL, I2C_I2CT_HZ);

  bq_service_begin(BQI2C);
  charger_begin(ChgI2C);
  flash_jobs_begin(ChgI2C);

  bq_configure();

  connect_wifi();

  server.on("/", handleRoot);
  server.on("/api/bq", handleApiBq);
  server.on("/api/charger", handleApiCharger);
  server.on("/api/flash/status", handleApiFlashStatus);
  server.on("/api/bq/action", HTTP_GET, handleBqAction);

  server.on(
      "/upload/eeprom", HTTP_POST,
      []() {
        server.send(200, "application/json", "{\"ok\":true}");
      },
      handleUploadEeprom);

  server.on(
      "/upload/attiny", HTTP_POST,
      []() {
        server.send(200, "application/json", "{\"ok\":true}");
      },
      handleUploadAttiny);

  server.on("/flash/eeprom/start", HTTP_POST, []() {
    if (flash_jobs_start_eeprom_flash()) {
      server.send(200, "application/json", "{\"started\":true}");
    } else {
      server.send(409, "application/json", "{\"started\":false,\"reason\":\"busy\"}");
    }
  });

  server.on("/flash/attiny/start", HTTP_POST, []() {
    if (flash_jobs_start_attiny_flash()) {
      server.send(200, "application/json", "{\"started\":true}");
    } else {
      server.send(409, "application/json", "{\"started\":false,\"reason\":\"busy\"}");
    }
  });

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  bq_service_poll();
  charger_poll();
  delay(1);
}
