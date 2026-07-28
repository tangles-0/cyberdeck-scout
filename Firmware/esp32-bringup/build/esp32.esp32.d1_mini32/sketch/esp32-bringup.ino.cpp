#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
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
#include "tiny_debug_service.h"
#include "html_ui.h"

static WebServer server(80);

static TwoWire BQI2C = TwoWire(0);
static TwoWire ChgI2C = TwoWire(1);

static File uploadFile;

enum BqBusRole : uint8_t {
  BQ_ROLE_DISCOVERY_TARGET = 0,
  BQ_ROLE_MASTER = 1,
};

static BqBusRole bqBusRole = BQ_ROLE_DISCOVERY_TARGET;
static volatile bool espMasterRequested = false;
static volatile uint32_t handoffRequestsSeen = 0;
static uint32_t lastTinyPingMs = 0;
static uint32_t lastTinyAckMs = 0;
static uint32_t bqMasterStartedMs = 0;
static bool tinyPingAckSeen = false;
static uint32_t tinyPingAttempts = 0;
static uint32_t tinyPingAcks = 0;
static uint32_t tinyPingFailures = 0;
static uint32_t bqRoleDemotions = 0;

#line 46 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static bool espOwnsBqBus();
#line 50 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleRoot();
#line 54 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleApiBq();
#line 86 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleApiCharger();
#line 95 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleApiTinyDebug();
#line 108 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleApiFlashStatus();
#line 117 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleBqAction();
#line 145 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleUploadEeprom();
#line 165 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleUploadAttiny();
#line 185 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void connect_wifi();
#line 203 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void handleTinyHandoff(int len);
#line 221 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void startBqDiscoveryTarget();
#line 235 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void becomeBqMaster();
#line 251 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static bool pingTinyTarget();
#line 265 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static void bqRoleTask();
#line 298 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
void setup();
#line 365 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
void loop();
#line 46 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/esp32-bringup.ino"
static bool espOwnsBqBus() {
  return bqBusRole == BQ_ROLE_MASTER;
}

static void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

static void handleApiBq() {
  const BqSnapshot &s = bq_snapshot();
  const uint32_t age =
      (s.lastReadMs == 0) ? 0xFFFFFFFFUL : (uint32_t)(millis() - s.lastReadMs);
  char body[1152];
  snprintf(
      body, sizeof(body),
      "{\"frame\":%u,\"c1_mV\":%u,\"c2_mV\":%u,\"current_raw\":%d,\"int_temp_c\":%d,\"ts_raw\":%u,"
      "\"soc_pct\":%u,"
      "\"battery_status\":%u,\"alarm_status\":%u,\"alarm_raw_status\":%u,"
      "\"safety_alert_a\":%u,\"safety_status_a\":%u,\"safety_alert_b\":%u,\"safety_status_b\":%u,"
      "\"alarm_seen\":%u,\"safety_a_seen\":%u,\"safety_b_seen\":%u,\"battery_seen\":%u,"
      "\"flags\":%u,\"deep_sleep\":%s,\"balance_gate\":%s,"
      "\"bq_balancing\":%s,\"alarm\":%s,\"safety_a\":%s,\"safety_b\":%s,"
      "\"good_packets\":%lu,\"bad_packets\":%lu,\"last_packet_age_ms\":%lu,\"i2c_ok\":%s,"
      "\"esp_bq_role\":\"%s\",\"esp_bq_target_mode\":%s,\"actions_enabled\":%s,"
      "\"handoff_requests_seen\":%lu,\"tiny_ping_attempts\":%lu,"
      "\"tiny_ping_acks\":%lu,\"tiny_ping_failures\":%lu,\"bq_role_demotions\":%lu}",
      s.frame, s.c1_mV, s.c2_mV, s.currentRaw, s.intTempC, s.tsRaw, s.socPct, s.batteryStatus,
      s.alarmStatus, s.alarmRawStatus, s.safetyAlertA, s.safetyStatusA, s.safetyAlertB,
      s.safetyStatusB, s.alarmSeen, s.safetyASeen, s.safetyBSeen, s.batterySeen, s.flags,
      (s.flags & 0x01) ? "true" : "false", (s.flags & 0x02) ? "true" : "false",
      (s.flags & 0x04) ? "true" : "false", (s.flags & 0x08) ? "true" : "false",
      (s.flags & 0x10) ? "true" : "false", (s.flags & 0x20) ? "true" : "false",
      (unsigned long)s.goodReads, (unsigned long)s.badReads, (unsigned long)age,
      s.i2cOk ? "true" : "false", espOwnsBqBus() ? "master" : "discovery_target",
      espOwnsBqBus() ? "false" : "true", espOwnsBqBus() ? "true" : "false",
      (unsigned long)handoffRequestsSeen, (unsigned long)tinyPingAttempts,
      (unsigned long)tinyPingAcks, (unsigned long)tinyPingFailures, (unsigned long)bqRoleDemotions);
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

static void handleApiTinyDebug() {
  uint32_t sinceSeq = 0;
  if (server.hasArg("since")) {
    sinceSeq = (uint32_t)server.arg("since").toInt();
  }
  char buf[2048];
  if (!tiny_debug_json(buf, sizeof(buf), sinceSeq)) {
    server.send(500, "application/json", "{\"error\":\"json_overflow\"}");
    return;
  }
  server.send(200, "application/json", buf);
}

static void handleApiFlashStatus() {
  char buf[640];
  if (!flash_jobs_status_json(buf, sizeof(buf))) {
    server.send(500, "application/json", "{\"error\":\"overflow\"}");
    return;
  }
  server.send(200, "application/json", buf);
}

static void handleBqAction() {
  if (!espOwnsBqBus()) {
    server.send(409, "application/json", "{\"error\":\"esp_not_bq_master\"}");
    return;
  }
  if (!server.hasArg("cmd")) {
    server.send(400, "application/json", "{\"error\":\"missing cmd\"}");
    return;
  }
  String c = server.arg("cmd");
  if (c == "toggle_sleep") {
    bq_toggle_deepsleep();
  } else if (c == "toggle_balance") {
    bool enabled = bq_toggle_balancing();
    server.send(200, "application/json",
                enabled ? "{\"ok\":true,\"balancing\":true}" : "{\"ok\":true,\"balancing\":false}");
    return;
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

static void handleTinyHandoff(int len) {
  uint8_t first = 0;
  uint8_t second = 0;
  if (len >= 1 && BQI2C.available()) {
    first = (uint8_t)BQI2C.read();
  }
  if (len >= 2 && BQI2C.available()) {
    second = (uint8_t)BQI2C.read();
  }
  while (BQI2C.available()) {
    BQI2C.read();
  }
  if (first == BQ_ROLE_MAGIC && second == BQ_ROLE_CMD_ESP_MASTER) {
    handoffRequestsSeen++;
    espMasterRequested = true;
  }
}

static void startBqDiscoveryTarget() {
  BQI2C.end();
  delay(10);
  BQI2C.onReceive(handleTinyHandoff);
  BQI2C.begin(ESP_BQ_DISCOVERY_ADDR, PIN_I2C_BQ_SDA, PIN_I2C_BQ_SCL, I2C_BQ_HZ);
  bqBusRole = BQ_ROLE_DISCOVERY_TARGET;
  espMasterRequested = false;
  lastTinyPingMs = 0;
  lastTinyAckMs = 0;
  bqMasterStartedMs = 0;
  tinyPingAckSeen = false;
  Serial.println("BQ bus: ESP discovery target; waiting for ATtiny handoff");
}

static void becomeBqMaster() {
  BQI2C.end();
  delay(10);
  BQI2C.begin(PIN_I2C_BQ_SDA, PIN_I2C_BQ_SCL, I2C_BQ_HZ);
  bq_service_begin(BQI2C);
  bqBusRole = BQ_ROLE_MASTER;
  espMasterRequested = false;
  lastTinyPingMs = 0;
  tinyPingAckSeen = false;
  delay(BQ_ROLE_HANDOFF_SETTLE_MS);
  bq_configure();
  lastTinyAckMs = millis();
  bqMasterStartedMs = lastTinyAckMs;
  Serial.println("BQ bus: ESP master; pinging ATtiny target watchdog");
}

static bool pingTinyTarget() {
  tinyPingAttempts++;
  BQI2C.beginTransmission(TINY_BQ_COORDINATOR_ADDR);
  BQI2C.write(BQ_ROLE_MAGIC);
  BQI2C.write(BQ_ROLE_CMD_TINY_PING);
  const bool acked = BQI2C.endTransmission() == 0;
  if (acked) {
    tinyPingAcks++;
  } else {
    tinyPingFailures++;
  }
  return acked;
}

static void bqRoleTask() {
  if (!ESP_BQ_ROLE_HANDOFF_ENABLED) {
    return;
  }

  const uint32_t now = millis();

  if (bqBusRole == BQ_ROLE_DISCOVERY_TARGET) {
    if (espMasterRequested) {
      becomeBqMaster();
    }
    return;
  }

  if ((uint32_t)(now - lastTinyPingMs) < BQ_ROLE_PING_PERIOD_MS) {
    return;
  }
  lastTinyPingMs = now;

  if (pingTinyTarget()) {
    lastTinyAckMs = now;
    tinyPingAckSeen = true;
  } else {
    const uint32_t timeoutMs = tinyPingAckSeen ? BQ_ROLE_PING_TIMEOUT_MS : BQ_ROLE_FIRST_PING_GRACE_MS;
    const uint32_t sinceMs = tinyPingAckSeen ? (uint32_t)(now - lastTinyAckMs)
                                             : (uint32_t)(now - bqMasterStartedMs);
    if (sinceMs > timeoutMs) {
      bqRoleDemotions++;
      startBqDiscoveryTarget();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("unified-esp32-service");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  if (ESP_BQ_ROLE_HANDOFF_ENABLED) {
    startBqDiscoveryTarget();
  } else {
    BQI2C.begin(PIN_I2C_BQ_SDA, PIN_I2C_BQ_SCL, I2C_BQ_HZ);
    bq_service_begin(BQI2C);
    bqBusRole = BQ_ROLE_MASTER;
    bq_configure();
    Serial.println("BQ bus: ESP direct master mode; role handoff disabled");
  }
  ChgI2C.begin(PIN_I2C_I2CT_SDA, PIN_I2C_I2CT_SCL, I2C_I2CT_HZ);

  charger_begin(ChgI2C);
  flash_jobs_begin(ChgI2C);
  tiny_debug_begin();

  connect_wifi();

  server.on("/", handleRoot);
  server.on("/api/bq", handleApiBq);
  server.on("/api/charger", handleApiCharger);
  server.on("/api/flash/status", handleApiFlashStatus);
  server.on("/api/tinydbg", handleApiTinyDebug);
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
  bqRoleTask();
  if (espOwnsBqBus()) {
    bq_service_poll();
  }
  charger_poll();
  tiny_debug_poll();
  delay(1);
}

