#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/charger_service.h"
#pragma once
#include <Arduino.h>
#include <Wire.h>

/**
 * I2Ct bus only: TPS25751 (target) + BQ25792. Never use I2Cc here — that bus is for
 * EEPROM programming while TPS is off (see flash_jobs + config.h).
 */
void charger_begin(TwoWire &wireI2Ct);
void charger_poll();

/** Suspend monitor reads while EEPROM job owns the shared peripheral on I2Cc pins */
void charger_set_monitoring_enabled(bool enabled);

/** After EEPROM flash restores I2Ct wiring, re-probe TPS/BQ addresses */
void charger_reprobe();

/** Writes compact JSON into buf (null-terminated), returns false if truncated error */
bool charger_fill_json(char *buf, size_t bufLen);
