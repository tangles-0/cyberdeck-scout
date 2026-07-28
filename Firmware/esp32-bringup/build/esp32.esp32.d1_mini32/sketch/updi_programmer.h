#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/updi_programmer.h"
#pragma once

#include <Arduino.h>

/** RESULT_OK or RESULT_* error tag */
typedef int UPDIResult;

#define UPDI_RESULT_OK 0
#define UPDI_RESULT_OPEN_FAILED -1
#define UPDI_RESULT_ENTER_PROG_FAILED -2
#define UPDI_RESULT_CHIP_ERASE_FAILED -3
#define UPDI_RESULT_WRITE_FAILED -4
#define UPDI_RESULT_VERIFY_FAILED -5
#define UPDI_RESULT_LEAVE_FAILED -6

/**
 * Open UPDI UART (double-break wake), configure UPDI CS regs like jtag2updi (UART mode).
 */
bool updi_open();

void updi_close();

/**
 * Full workflow: enter prog, chip erase, write image to flash (byte addresses from
 * config ATTINY_FLASH_BYTE_BASE), leave prog (reset to app).
 * @param data raw .bin (e.g. Export compiled binary from IDE), length <= flash size
 * @param progress 0-100 callback, optional
 */
UPDIResult updi_program_flash(
    const uint8_t *data, size_t length,
    void (*progress)(int percent, const char *phase, void *ctx), void *ctx);

const char *updi_result_string(UPDIResult r);

/** Last ASI_System_Status from enter_prog_mode (raw byte from lcds). 0xFF if never read. */
uint8_t updi_last_asi_system_status();

/** Same register after mask 0xEF (values 0x82=run, 0x08=prog are expected). */
uint8_t updi_last_asi_system_status_masked();

/** True if RX saw at least one byte sent by TX on the one-wire UPDI node. */
bool updi_last_echo_seen();

void updi_clear_diag();
