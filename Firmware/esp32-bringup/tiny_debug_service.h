#pragma once
#include <Arduino.h>

/**
 * ATtiny404 debug-serial listener.
 *
 * The BMS firmware (DEBUG=1) emits a terse 115200 8N1 TX-only stream on PB2
 * (the BATT_LVL_2 net). Wire that node to PIN_TINY_DEBUG_RX (common ground)
 * and this service decodes it into human-readable events plus a live status
 * snapshot for the web UI.
 *
 * Protocol (one record per newline):
 *   B          boot
 *   N<n> <lvl> BQ absent: I2C error n (decimal), lvl = PA1/PA2 bus pin levels
 *              (bit0=SDA, bit1=SCL; 3 = both idle high)
 *   O          BQ configured OK
 *   E          BQ configuration write failed
 *   D          power button pressed (down edge)
 *   L          power button long press (forces power off)
 *   R          power button short press released
 *   P1/P0      device power state change
 *   S<v> <c1>,<c2> <b><p> <err> <bstat>   1 Hz status: v=BQ reads valid,
 *              cell mV, b='B' balancing else '-', p='P' power on else 'p',
 *              last I2C error, BQ Battery Status 0x12 (decimal; bit15 SLEEP,
 *              bit14 DEEPSLEEP, bit8 FET_EN, bit7 POR, bit5 CFGUPDATE)
 */

void tiny_debug_begin();
void tiny_debug_poll();

// Status snapshot + events with sequence numbers greater than sinceSeq.
// Returns false if the JSON did not fit in buf.
bool tiny_debug_json(char *buf, size_t len, uint32_t sinceSeq);
