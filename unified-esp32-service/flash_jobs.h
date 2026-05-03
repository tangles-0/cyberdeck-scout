#pragma once
#include <Arduino.h>
#include <Wire.h>

/**
 * Second I2C peripheral (same TwoWire): normally wired as I2Ct for charger monitor.
 * EEPROM flash temporarily switches these pins to I2Cc (see config.h).
 */
void flash_jobs_begin(TwoWire &sharedSecondPeripheral);

/** Browser uploads write to LittleFS staging paths first */
bool flash_jobs_start_eeprom_flash();
bool flash_jobs_start_attiny_flash();

/** Status for /api/flash/status */
bool flash_jobs_status_json(char *buf, size_t bufLen);
