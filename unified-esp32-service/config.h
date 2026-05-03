#pragma once

/**
 * Pin and bus configuration — adjust for your PCB before flashing.
 *
 * ESP32 has two hardware I2C peripherals (Wire instance 0 and 1).
 *
 * Bus roles (per Cyberdeck mainboard):
 * - BQ bus: BQ76905 (with ATTiny202) — pack monitor / protect IC.
 * - I2Ct: TPS25751 as I2C *target* — this is where the ESP talks to the PD controller
 *   for status (MODE, STATUS, PD, ADC, …). BQ25792 is monitored here as well (same
 *   net as in charger-monitor); you cannot use I2Cc for TPS access — that side is
 *   controller-only from the ESP.
 * - I2Cc: EEPROM (AT24) lives here *only* for factory flashing. The TPS must be
 *   powered down for this bus to be used safely / without clashing. Firmware
 *   temporarily remaps the second I2C peripheral from I2Ct pins to I2Cc pins only
 *   while an EEPROM job runs, then restores I2Ct for monitoring.
 */

#include <Arduino.h>

// Wi-Fi (replace or use WiFiManager later)
static const char *const WIFI_SSID = "Tangles";
static const char *const WIFI_PASS = "Jiblet!1337";

// --- I2C: BQ76905 + ATTiny202 host side ---
static const int PIN_I2C_BQ_SDA = 21;
static const int PIN_I2C_BQ_SCL = 22;
static const uint32_t I2C_BQ_HZ = 100000;

// --- I2Ct: TPS25751 (target) + BQ25792 — charger / PD monitoring ONLY ---
static const int PIN_I2C_I2CT_SDA = 18;
static const int PIN_I2C_I2CT_SCL = 19;
static const uint32_t I2C_I2CT_HZ = 400000;

// --- I2Cc: AT24 EEPROM — programming ONLY (power TPS down first) ---
static const int PIN_I2C_I2CC_SDA = 25;
static const int PIN_I2C_I2CC_SCL = 26;
static const uint32_t I2C_I2CC_HZ = 400000;

// --- UPDI (ATtiny202 program flash) ---
// Typical wiring: ESP32 TX -> series resistor -> UPDI pin; RX tied to UPDI pin.
static const int PIN_UPDI_TX = 17;
static const int PIN_UPDI_RX = 16;
static const uint32_t UPDI_UART_BAUD = 230400;

// --- Device addresses ---
static const uint8_t BQ76905_ADDR = 0x08;
static const uint8_t BQ25792_ADDR = 0x6B;
static const uint8_t EEPROM_AT24_ADDR = 0x50;
static const uint8_t TPS25751_DEFAULT_ADDR = 0x22;
static const bool AUTO_DETECT_TPS25751 = true;

// TPS strap search order
static const uint8_t TPS25751_ADDR_CANDIDATES[] = {0x20, 0x21, 0x22, 0x23};

// EEPROM geometry (from eeprom-flasher sketch)
static const uint32_t EEPROM_SIZE_BYTES = 32768;
static const uint16_t EEPROM_PAGE_BYTES = 64;

// ATtiny202: 8 KB flash, 64-byte pages (megaTinyCore / datasheet)
static const uint32_t ATTINY202_FLASH_SIZE = 8192;
static const uint8_t ATTINY202_FLASH_PAGE_SIZE = 64;
// UPDI linear flash base (tinyAVR 0/1/2 class devices)
static const uint16_t ATTINY202_FLASH_BYTE_BASE = 0x8000;
