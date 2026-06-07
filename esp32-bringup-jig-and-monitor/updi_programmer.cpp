/*
 * ESP32 port of jtag2updi UPDI UART + NVM flash write path for tinyAVR.
 * Reference: jtag2updi UPDI_lo_lvl.cpp, JTAG2.cpp NVM_buffered_write, NVM.h
 */

#include "updi_programmer.h"
#include "config.h"
#include <HardwareSerial.h>

static HardwareSerial UpdiSerial(2);

static constexpr uint8_t SYNCH = 0x55;
static constexpr uint16_t NVM_BASE_ADDR = 0x1000;

namespace NVM {
enum reg { CTRLA, CTRLB, STATUS };
enum cmnd { NOP, WP, ER, ERWP, PBC, CHER, EEER, WFU };
} // namespace NVM

// UPDI CS register indices (must match jtag2updi UPDI::reg order used by stcs/lcds)
enum UPDI_CSReg {
  UPDI_Status_A = 0,
  UPDI_Status_B = 1,
  UPDI_Control_A = 2,
  UPDI_Control_B = 3,
  UPDI_ASI_Reset_Request = 9,
  UPDI_ASI_System_Status = 11,
};

static constexpr uint8_t RESET_ON = 0x59;
static constexpr uint8_t RESET_OFF = 0x00;

static const uint8_t KEY_NVM_PROG[8] = {0x20, 0x67, 0x6F, 0x72, 0x50, 0x4D, 0x56, 0x4E};
static const uint8_t KEY_CHIP_ERASE[8] = {0x65, 0x73, 0x61, 0x72, 0x45, 0x4D, 0x56, 0x4E};

static void drain_rx() {
  while (UpdiSerial.available()) {
    (void)UpdiSerial.read();
  }
}

static uint8_t updi_put(uint8_t c) {
  UpdiSerial.write(c);
  UpdiSerial.flush();
  unsigned long t = millis();
  while (!UpdiSerial.available() && millis() - t < 100) {
    delayMicroseconds(50);
  }
  if (UpdiSerial.available()) {
    (void)UpdiSerial.read(); // echo (single-wire)
  }
  return c;
}

static uint8_t updi_get() {
  unsigned long t = millis();
  while (!UpdiSerial.available() && millis() - t < 500) {
    delayMicroseconds(100);
  }
  if (!UpdiSerial.available()) {
    return 0xFF;
  }
  return (uint8_t)UpdiSerial.read();
}

static void updi_double_break() {
  UpdiSerial.end();
  delay(5);
  UpdiSerial.begin(300, SERIAL_8N1, PIN_UPDI_RX, PIN_UPDI_TX);
  UpdiSerial.write((uint8_t)0x00);
  UpdiSerial.flush();
  UpdiSerial.write((uint8_t)0x00);
  UpdiSerial.flush();
  delay(15);
  drain_rx();
  UpdiSerial.begin(UPDI_UART_BAUD, SERIAL_8E2, PIN_UPDI_RX, PIN_UPDI_TX);
  delay(2);
  drain_rx();
}

static void stcs(uint8_t reg, uint8_t data) {
  updi_put(SYNCH);
  updi_put((uint8_t)(0xC0 + reg));
  updi_put(data);
}

static uint8_t lcds(uint8_t reg) {
  updi_put(SYNCH);
  updi_put((uint8_t)(0x80 + reg));
  return updi_get();
}

static uint8_t lds_b(uint16_t addr) {
  updi_put(SYNCH);
  updi_put(0x04);
  updi_put((uint8_t)(addr & 0xFF));
  updi_put((uint8_t)(addr >> 8));
  return updi_get();
}

static void sts_b(uint16_t addr, uint8_t data) {
  updi_put(SYNCH);
  updi_put(0x44);
  updi_put((uint8_t)(addr & 0xFF));
  updi_put((uint8_t)(addr >> 8));
  updi_get();
  updi_put(data);
  updi_get();
}

static void stptr_w(uint16_t addr) {
  updi_put(SYNCH);
  updi_put(0x69);
  updi_put((uint8_t)(addr & 0xFF));
  updi_put((uint8_t)(addr >> 8));
  updi_get();
}

static void stinc_b(uint8_t data) {
  updi_put(SYNCH);
  updi_put(0x64);
  updi_put(data);
  updi_get();
}

static void rep(uint8_t repeats) {
  updi_put(SYNCH);
  updi_put(0xA0);
  updi_put(repeats);
}

static void write_key(const uint8_t *key) {
  updi_put(SYNCH);
  updi_put(0xE0);
  for (uint8_t i = 0; i < 8; i++) {
    updi_put(key[i]);
  }
}

template <bool preserve_ptr> void nvm_cmd(uint8_t cmd) {
  uint16_t temp = 0;
  if (preserve_ptr) {
    updi_put(SYNCH);
    updi_put(0x29);
    temp = (uint16_t)updi_get() | ((uint16_t)updi_get() << 8);
  }
  sts_b(NVM_BASE_ADDR + NVM::CTRLA, cmd);
  if (preserve_ptr) {
    stptr_w(temp);
  }
}

template <bool preserve_ptr> void nvm_wait() {
  uint16_t temp = 0;
  if (preserve_ptr) {
    updi_put(SYNCH);
    updi_put(0x29);
    temp = (uint16_t)updi_get() | ((uint16_t)updi_get() << 8);
  }
  while (lds_b(NVM_BASE_ADDR + NVM::STATUS) & 0x03) {
    delayMicroseconds(10);
  }
  if (preserve_ptr) {
    stptr_w(temp);
  }
}

static uint8_t cpu_mode_masked(uint8_t mask) {
  return (uint8_t)(lcds(UPDI_ASI_System_Status) & mask);
}

static void cpu_reset() {
  stcs(UPDI_ASI_Reset_Request, RESET_ON);
  stcs(UPDI_ASI_Reset_Request, RESET_OFF);
  unsigned long t0 = millis();
  while (cpu_mode_masked(0x0E) == 0 && millis() - t0 < 2000) {
    delayMicroseconds(50);
  }
}

static bool enter_prog_mode() {
  cpu_reset();
  uint8_t system_status = cpu_mode_masked(0xEF);
  switch (system_status) {
  case 0x82:
    write_key(KEY_NVM_PROG);
    cpu_reset();
    /* fallthrough */
  case 0x08:
    nvm_cmd<false>(NVM::PBC);
    return true;
  default:
    return false;
  }
}

static bool leave_prog_mode() {
  uint8_t system_status = cpu_mode_masked(0xEF);
  switch (system_status) {
  case 0x08:
    cpu_reset();
    /* fallthrough */
  case 0x82:
    return true;
  default:
    return false;
  }
}

static bool chip_erase_device() {
  write_key(KEY_CHIP_ERASE);
  cpu_reset();
  delay(100);
  return enter_prog_mode();
}

/**
 * Flash page buffer write — mirrors JTAG2::NVM_buffered_write for MTYPE_FLASH (WP).
 */
static bool nvm_buffered_write_flash(uint16_t address, const uint8_t *src, uint16_t length,
                                     uint8_t page_size) {
  auto send_block = [&](uint8_t block_count, uint16_t &idx) -> bool {
    if (block_count == 0) {
      return true;
    }
    uint8_t n = (uint8_t)(block_count - 1);
    nvm_wait<true>();
    rep(n);
    stinc_b(src[idx]);
    for (uint8_t i = n; i; i--) {
      idx++;
      updi_put(src[idx]);
    }
    idx++;
    return true;
  };

  uint16_t byte_idx = 0;
  uint16_t bytes_remaining = length;
  stptr_w(address);

  uint8_t unaligned = (uint8_t)((-address) & (page_size - 1));
  if (unaligned > bytes_remaining) {
    unaligned = (uint8_t)bytes_remaining;
  }
  if (unaligned) {
    if (!send_block(unaligned, byte_idx)) {
      return false;
    }
    bytes_remaining -= unaligned;
    nvm_cmd<true>(NVM::WP);
  }
  while (bytes_remaining) {
    if (bytes_remaining >= page_size) {
      if (!send_block(page_size, byte_idx)) {
        return false;
      }
      bytes_remaining -= page_size;
    } else {
      if (!send_block((uint8_t)bytes_remaining, byte_idx)) {
        return false;
      }
      bytes_remaining = 0;
    }
    nvm_cmd<true>(NVM::WP);
  }
  return true;
}

void updi_open() {
  UpdiSerial.end();
  delay(10);
  UpdiSerial.begin(UPDI_UART_BAUD, SERIAL_8E2, PIN_UPDI_RX, PIN_UPDI_TX);
  delay(5);
  updi_double_break();
  // Match jtag2updi sign_on STCS for UART UPDI
  stcs(UPDI_Control_B, 8);
  stcs(UPDI_Control_A, 0x80);
}

void updi_close() {
  UpdiSerial.end();
}

UPDIResult updi_program_flash(const uint8_t *data, size_t length,
                              void (*progress)(int, const char *, void *), void *ctx) {
  if (!data || length == 0 || length > ATTINY202_FLASH_SIZE) {
    return UPDI_RESULT_WRITE_FAILED;
  }

  if (progress) {
    progress(0, "open", ctx);
  }
  updi_open();

  if (progress) {
    progress(5, "enter_prog", ctx);
  }
  if (!enter_prog_mode()) {
    updi_close();
    return UPDI_RESULT_ENTER_PROG_FAILED;
  }

  if (progress) {
    progress(10, "chip_erase", ctx);
  }
  if (!chip_erase_device()) {
    updi_close();
    return UPDI_RESULT_CHIP_ERASE_FAILED;
  }

  if (progress) {
    progress(15, "writing", ctx);
  }

  const uint16_t flash_base = ATTINY202_FLASH_BYTE_BASE;
  if (!nvm_buffered_write_flash(flash_base, data, (uint16_t)length, ATTINY202_FLASH_PAGE_SIZE)) {
    leave_prog_mode();
    updi_close();
    return UPDI_RESULT_WRITE_FAILED;
  }

  // Simple verify: read back via UPDI lds
  if (progress) {
    progress(85, "verify", ctx);
  }
  for (size_t i = 0; i < length; i++) {
    uint8_t v = lds_b((uint16_t)(flash_base + i));
    if (v != data[i]) {
      leave_prog_mode();
      updi_close();
      return UPDI_RESULT_VERIFY_FAILED;
    }
    if (progress && (i % 256 == 0)) {
      int p = 85 + (int)(10 * i / length);
      progress(p, "verify", ctx);
    }
  }

  if (progress) {
    progress(95, "leave_prog", ctx);
  }
  if (!leave_prog_mode()) {
    updi_close();
    return UPDI_RESULT_LEAVE_FAILED;
  }

  updi_close();
  if (progress) {
    progress(100, "done", ctx);
  }
  return UPDI_RESULT_OK;
}

const char *updi_result_string(UPDIResult r) {
  switch (r) {
  case UPDI_RESULT_OK:
    return "OK";
  case UPDI_RESULT_OPEN_FAILED:
    return "UPDI open failed";
  case UPDI_RESULT_ENTER_PROG_FAILED:
    return "Enter programming mode failed";
  case UPDI_RESULT_CHIP_ERASE_FAILED:
    return "Chip erase failed";
  case UPDI_RESULT_WRITE_FAILED:
    return "Flash write failed";
  case UPDI_RESULT_VERIFY_FAILED:
    return "Verify mismatch";
  case UPDI_RESULT_LEAVE_FAILED:
    return "Leave programming mode failed";
  default:
    return "Unknown error";
  }
}
