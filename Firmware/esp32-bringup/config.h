#pragma once

/**
 * Pin and bus configuration — adjust for your PCB before flashing.
 *
 * ESP32 has two hardware I2C peripherals (Wire instance 0 and 1).
 *
 * Bus roles (per Cyberdeck mainboard):
 * - BQ bus: BQ76905 (with ATtiny404) — pack monitor / protect IC.
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

// --- I2C: BQ76905 + ATtiny404 host side ---
static const int PIN_I2C_BQ_SDA = 21;
static const int PIN_I2C_BQ_SCL = 22;
static const uint32_t I2C_BQ_HZ = 100000;
// Role handoff on the shared BQ76905 / ATtiny404 bus:
// - ESP starts as an I2C target at ESP_BQ_DISCOVERY_ADDR.
// - Tiny polls that address every few seconds while it owns the BQ.
// - Tiny sends BQ_ROLE_CMD_ESP_MASTER, switches to target mode at
//   TINY_BQ_COORDINATOR_ADDR, and the ESP becomes the BQ bus controller.
// - ESP pings the Tiny target; if either side loses the other, both fall back
//   to the safe state where the Tiny owns and reconfigures the BQ.
static const uint8_t ESP_BQ_DISCOVERY_ADDR = 0x42;
static const uint8_t TINY_BQ_COORDINATOR_ADDR = 0x43;
static const uint8_t BQ_ROLE_MAGIC = 0xC5;
static const uint8_t BQ_ROLE_CMD_ESP_MASTER = 0xA1;
static const uint8_t BQ_ROLE_CMD_TINY_PING = 0x5A;
static const bool ESP_BQ_ROLE_HANDOFF_ENABLED = false;
static const uint32_t BQ_ROLE_HANDOFF_SETTLE_MS = 500;
static const uint32_t BQ_ROLE_PING_PERIOD_MS = 500;
static const uint32_t BQ_ROLE_PING_TIMEOUT_MS = 1000;
static const uint32_t BQ_ROLE_FIRST_PING_GRACE_MS = 8000;

// --- I2Ct: TPS25751 (target) + BQ25792 — charger / PD monitoring ONLY ---
static const int PIN_I2C_I2CT_SDA = 18;
static const int PIN_I2C_I2CT_SCL = 19;
static const int PIN_I2C_I2CC_SDA = 25;
static const int PIN_I2C_I2CC_SCL = 26;
// static const int PIN_I2C_I2CT_SDA = 25;
// static const int PIN_I2C_I2CT_SCL = 26;
// static const int PIN_I2C_I2CC_SDA = 18;
// static const int PIN_I2C_I2CC_SCL = 19;
static const uint32_t I2C_I2CT_HZ = 400000;

// --- I2Cc: AT24 EEPROM — programming ONLY (power TPS down first) ---
static const uint32_t I2C_I2CC_HZ = 400000;

// --- ATtiny404 debug serial (BMS firmware DEBUG=1, TX-only on its PB2) ---
// Wire Tiny PB2 (BATT_LVL_2 net) -> this pin, plus common ground.
// RX-only; uses UART1 (UPDI owns UART2). Input-only pins 34/35 also work.
static const int PIN_TINY_DEBUG_RX = 27;
static const uint32_t TINY_DEBUG_BAUD = 115200;

// --- UPDI (ATtiny404 program flash) ---
// Match jtag2updi/SerialUPDI wiring:
//   ESP32 RX -> target UPDI directly
//   ESP32 TX -> target UPDI through a series resistor (typically 4.7k)
static const int PIN_UPDI_RX = 16;
static const int PIN_UPDI_TX = 17;
static const uint32_t UPDI_UART_BAUD = 115200;

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

// ATtiny404: 4 KB flash, 64-byte pages (tinyAVR 0-series; same UPDI layout as 202/402)
static const uint32_t ATTINY_FLASH_SIZE = 4096;
static const uint8_t ATTINY_FLASH_PAGE_SIZE = 64;
static const uint16_t ATTINY_FLASH_BYTE_BASE = 0x8000;

// megaTinyCore defaults for ATtiny404, 20 MHz internal, 8 ms startup, EEPROM retained.
// These mirror Upload Using Programmer / SerialUPDI fuse settings for the staged sketch.
static const bool ATTINY_WRITE_MEGA_TINY_CORE_FUSES = true;
static const uint8_t ATTINY_FUSE_WDTCFG = 0x00;
static const uint8_t ATTINY_FUSE_OSCCFG = 0x02;
static const uint8_t ATTINY_FUSE_SYSCFG0 = 0xC5;
static const uint8_t ATTINY_FUSE_SYSCFG1 = 0x04;
static const uint8_t ATTINY_FUSE_APPEND = 0x00;
static const uint8_t ATTINY_FUSE_BOOTEND = 0x00;
