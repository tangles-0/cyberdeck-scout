#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

struct BqSnapshot {
  uint16_t frame;
  uint16_t c1_mV;
  uint16_t c2_mV;
  int16_t currentRaw;
  int16_t intTempC;
  uint16_t tsRaw;
  uint8_t socPct;
  uint16_t batteryStatus;
  uint16_t alarmStatus;
  uint16_t alarmRawStatus;
  uint8_t safetyAlertA;
  uint8_t safetyStatusA;
  uint8_t safetyAlertB;
  uint8_t safetyStatusB;
  uint8_t flags;
  uint16_t alarmSeen;
  uint8_t safetyASeen;
  uint8_t safetyBSeen;
  uint16_t batterySeen;
  uint32_t goodReads;
  uint32_t badReads;
  uint32_t lastReadMs;
  bool i2cOk;
};

void bq_service_begin(TwoWire &wire);
void bq_service_poll();
const BqSnapshot &bq_snapshot();

bool bq_configure();
bool bq_send_subcommand(uint16_t cmd);
bool bq_toggle_deepsleep();
bool bq_toggle_balancing(); // returns new enabled state
bool bq_enter_ship_mode(); // blocking forever on ATTiny — here only sends cmds once
