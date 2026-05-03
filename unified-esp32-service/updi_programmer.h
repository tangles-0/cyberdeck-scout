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
void updi_open();

void updi_close();

/**
 * Full workflow: enter prog, chip erase, write image to flash (byte addresses from
 * config ATTINY202_FLASH_BYTE_BASE), leave prog (reset to app).
 * @param data raw .bin (e.g. Export compiled binary from IDE), length <= flash size
 * @param progress 0-100 callback, optional
 */
UPDIResult updi_program_flash(
    const uint8_t *data, size_t length,
    void (*progress)(int percent, const char *phase, void *ctx), void *ctx);

const char *updi_result_string(UPDIResult r);
