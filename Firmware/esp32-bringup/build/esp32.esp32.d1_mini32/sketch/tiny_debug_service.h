#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/tiny_debug_service.h"
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
 *   N<n> <lvl> BQ absent: I2C error n (decimal), lvl = bus pin levels as seen
 *              by the TWI (bit0=SDA, bit1=SCL; 3 = both idle high)
 *   O          BQ configured OK
 *   E          BQ configuration write failed
 *   P1/P0      device power state change (also marks button-driven power events)
 *   S<v> <c1>,<c2> <b><p> <err>   1 Hz status: v=BQ reads valid, cell mV,
 *              b='B' balancing else '-', p='P' power on else 'p', last I2C error
 */

void tiny_debug_begin();
void tiny_debug_poll();

// Status snapshot + events with sequence numbers greater than sinceSeq.
// Returns false if the JSON did not fit in buf.
bool tiny_debug_json(char *buf, size_t len, uint32_t sinceSeq);
