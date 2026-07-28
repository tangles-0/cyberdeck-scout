#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/updi_programmer.cpp"
/*
 * ESP32 port of jtag2updi UPDI UART + NVM flash write path for tinyAVR.
 * Reference: jtag2updi UPDI_lo_lvl.cpp, JTAG2.cpp NVM_buffered_write, NVM.h
 */

#include "updi_programmer.h"
#include "config.h"
#include "driver/gpio.h"
#include "driver/uart.h"

static constexpr uart_port_t UPDI_UART_NUM = UART_NUM_2;
static constexpr uint32_t UPDI_ECHO_TIMEOUT_MS = 20;
static constexpr uint32_t UPDI_RX_TIMEOUT_MS = 500;
static constexpr uint8_t UPDI_ACK = 0x40;
static bool g_uart_installed = false;
static bool g_echo_seen = false;
static uint32_t g_echo_ok = 0;
static uint32_t g_echo_timeout = 0;
static uint32_t g_echo_mismatch = 0;
static uint32_t g_rx_timeout = 0;
static uint32_t g_ack_fail = 0;
static uint8_t g_last_rx = 0xFF;
static uint8_t g_last_ack = 0xFF;
static uint8_t g_last_nvm_status = 0xFF;
static const char *g_last_fail = "none";
static uint16_t g_last_addr = 0;
static uint16_t g_fail_addr = 0;
static size_t g_fail_offset = 0;
static uint16_t g_pages_loaded = 0;
static uint16_t g_pages_committed = 0;
static uint16_t g_bytes_loaded = 0;
static uint16_t g_bytes_verified = 0;

static constexpr uint8_t SYNCH = 0x55;
static constexpr uint16_t NVM_BASE_ADDR = 0x1000;
static constexpr uint16_t FUSE_BASE_ADDR = 0x1280;
static constexpr uint8_t UPDI_DISABLE = 0x04;

namespace NVM {
enum reg { CTRLA, CTRLB, STATUS, INTCTRL, INTFLAGS, RESERVED, DATA, DATAH, ADDR, ADDRH };
enum cmnd { NOP, WP, ER, ERWP, PBC, CHER, EEER, WFU };
} // namespace NVM

enum FuseOffset {
  FUSE_WDTCFG = 0x00,
  FUSE_OSCCFG = 0x02,
  FUSE_SYSCFG0 = 0x05,
  FUSE_SYSCFG1 = 0x06,
  FUSE_APPEND = 0x07,
  FUSE_BOOTEND = 0x08,
};

// UPDI CS register indices (must match jtag2updi UPDI::reg order used by stcs/lcds)
enum UPDI_CSReg {
  UPDI_Status_A = 0,
  UPDI_Status_B = 1,
  UPDI_Control_A = 2,
  UPDI_Control_B = 3,
  UPDI_ASI_Key_Status = 7,
  UPDI_ASI_Reset_Request = 8,
  UPDI_ASI_System_Status = 11,
};

static constexpr uint8_t RESET_ON = 0x59;
static constexpr uint8_t RESET_OFF = 0x00;

static const uint8_t KEY_NVM_PROG[8] = {0x20, 0x67, 0x6F, 0x72, 0x50, 0x4D, 0x56, 0x4E};
static const uint8_t KEY_CHIP_ERASE[8] = {0x65, 0x73, 0x61, 0x72, 0x45, 0x4D, 0x56, 0x4E};

static uint8_t g_diag_asi_raw = 0xFF;
static uint8_t g_diag_asi_masked = 0xFF;

void updi_clear_diag() {
  g_diag_asi_raw = 0xFF;
  g_diag_asi_masked = 0xFF;
  g_echo_seen = false;
  g_echo_ok = 0;
  g_echo_timeout = 0;
  g_echo_mismatch = 0;
  g_rx_timeout = 0;
  g_ack_fail = 0;
  g_last_rx = 0xFF;
  g_last_ack = 0xFF;
  g_last_nvm_status = 0xFF;
  g_last_fail = "none";
  g_last_addr = 0;
  g_fail_addr = 0;
  g_fail_offset = 0;
  g_pages_loaded = 0;
  g_pages_committed = 0;
  g_bytes_loaded = 0;
  g_bytes_verified = 0;
}

uint8_t updi_last_asi_system_status() {
  return g_diag_asi_raw;
}

uint8_t updi_last_asi_system_status_masked() {
  return g_diag_asi_masked;
}

bool updi_last_echo_seen() {
  return g_echo_seen;
}

static void log_diag(const char *where) {
  Serial.printf("[UPDI] %s echo_seen=%s echo_ok=%lu echo_timeout=%lu echo_mismatch=%lu "
                "rx_timeout=%lu ack_fail=%lu last_rx=0x%02X last_ack=0x%02X "
                "asi=0x%02X masked=0x%02X nvm_status=0x%02X last_addr=0x%04X "
                "fail_addr=0x%04X fail_offset=%u pages=%u/%u bytes_loaded=%u "
                "bytes_verified=%u last_fail=%s\n",
                where, g_echo_seen ? "true" : "false", (unsigned long)g_echo_ok,
                (unsigned long)g_echo_timeout, (unsigned long)g_echo_mismatch,
                (unsigned long)g_rx_timeout, (unsigned long)g_ack_fail, g_last_rx,
                g_last_ack, g_diag_asi_raw, g_diag_asi_masked, g_last_nvm_status,
                g_last_addr, g_fail_addr, (unsigned)g_fail_offset, g_pages_committed,
                g_pages_loaded, g_bytes_loaded, g_bytes_verified, g_last_fail);
}

static bool fail_at(const char *where) {
  g_last_fail = where;
  log_diag(where);
  return false;
}

static void drain_rx() {
  if (!g_uart_installed) {
    return;
  }
  uint8_t discard[32];
  size_t buffered = 0;
  while (uart_get_buffered_data_len(UPDI_UART_NUM, &buffered) == ESP_OK && buffered > 0) {
    const size_t to_read = buffered > sizeof(discard) ? sizeof(discard) : buffered;
    (void)uart_read_bytes(UPDI_UART_NUM, discard, to_read, 0);
  }
}

static bool updi_wait_tx(uint32_t timeout_ms) {
  if (!g_uart_installed) {
    return false;
  }
  return uart_wait_tx_done(UPDI_UART_NUM, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
}

static bool updi_serial_begin(uint32_t baud, bool even_parity, uart_stop_bits_t stop_bits) {
  Serial.printf("[UPDI] uart_begin baud=%lu parity=%s stop_bits=%d rx=%d tx=%d\n",
                (unsigned long)baud, even_parity ? "even" : "none", (int)stop_bits,
                PIN_UPDI_RX, PIN_UPDI_TX);
  uart_config_t uart_config = {};
  uart_config.baud_rate = baud;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = even_parity ? UART_PARITY_EVEN : UART_PARITY_DISABLE;
  uart_config.stop_bits = stop_bits;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.rx_flow_ctrl_thresh = 0;
  uart_config.source_clk = UART_SCLK_DEFAULT;

  if (uart_param_config(UPDI_UART_NUM, &uart_config) != ESP_OK) {
    return fail_at("uart_param_config");
  }
  if (!g_uart_installed) {
    if (uart_driver_install(UPDI_UART_NUM, 256, 256, 0, nullptr, 0) != ESP_OK) {
      return fail_at("uart_driver_install");
    }
    g_uart_installed = true;
  }
  if (uart_set_pin(UPDI_UART_NUM, PIN_UPDI_TX, PIN_UPDI_RX, UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK) {
    (void)uart_driver_delete(UPDI_UART_NUM);
    g_uart_installed = false;
    return fail_at("uart_set_pin");
  }
  // Keep RX attached to the UART; pinMode() can replace the matrix function on ESP32.
  (void)gpio_set_pull_mode((gpio_num_t)PIN_UPDI_RX, GPIO_PULLUP_ONLY);
  delay(2);
  drain_rx();
  return true;
}

static bool discard_echo(uint8_t expected) {
  uint8_t echo = 0xFF;
  if (uart_read_bytes(UPDI_UART_NUM, &echo, 1, pdMS_TO_TICKS(UPDI_ECHO_TIMEOUT_MS)) == 1) {
    g_last_rx = echo;
    if (echo == expected) {
      g_echo_seen = true;
      g_echo_ok++;
      return true;
    }
    g_echo_mismatch++;
    return false;
  }
  g_echo_timeout++;
  return false;
}

static uint8_t updi_put(uint8_t c) {
  if (!g_uart_installed) {
    return c;
  }
  (void)uart_write_bytes(UPDI_UART_NUM, &c, 1);
  updi_wait_tx(50);
  (void)discard_echo(c);
  return c;
}

static bool updi_get_byte(uint8_t &out, uint32_t timeout_ms = UPDI_RX_TIMEOUT_MS) {
  if (!g_uart_installed) {
    out = 0xFF;
    return false;
  }
  const int got = uart_read_bytes(UPDI_UART_NUM, &out, 1, pdMS_TO_TICKS(timeout_ms));
  if (got == 1) {
    g_last_rx = out;
    return true;
  }
  out = 0xFF;
  g_last_rx = out;
  g_rx_timeout++;
  return false;
}

static uint8_t updi_get() {
  uint8_t v = 0xFF;
  (void)updi_get_byte(v);
  return v;
}

static bool updi_expect_ack(const char *where = "ack") {
  uint8_t v = 0xFF;
  const bool got = updi_get_byte(v);
  g_last_ack = v;
  if (got && v == UPDI_ACK) {
    return true;
  }
  g_ack_fail++;
  Serial.printf("[UPDI] ACK fail at %s got=%s0x%02X\n", where, got ? "" : "timeout/", v);
  return false;
}

static bool updi_double_break() {
  Serial.println("[UPDI] double_break start");
  if (!updi_serial_begin(300, false, UART_STOP_BITS_1)) {
    return fail_at("double_break_slow_uart");
  }
  updi_put(0x00);
  updi_put(0x00);
  delay(2);
  drain_rx();
  const bool ok = updi_serial_begin(UPDI_UART_BAUD, true, UART_STOP_BITS_2);
  if (!ok) {
    return fail_at("double_break_fast_uart");
  }
  log_diag("double_break_done");
  return true;
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

static void capture_asi_status() {
  g_diag_asi_raw = lcds(UPDI_ASI_System_Status);
  g_diag_asi_masked = (uint8_t)(g_diag_asi_raw & 0xEF);
}

static uint8_t lds_b(uint16_t addr) {
  updi_put(SYNCH);
  updi_put(0x04);
  updi_put((uint8_t)(addr & 0xFF));
  updi_put((uint8_t)(addr >> 8));
  return updi_get();
}

static bool sts_b(uint16_t addr, uint8_t data) {
  updi_put(SYNCH);
  updi_put(0x44);
  updi_put((uint8_t)(addr & 0xFF));
  updi_put((uint8_t)(addr >> 8));
  if (!updi_expect_ack("sts_addr")) {
    return false;
  }
  updi_put(data);
  return updi_expect_ack("sts_data");
}

static bool stptr_w(uint16_t addr) {
  updi_put(SYNCH);
  updi_put(0x69);
  updi_put((uint8_t)(addr & 0xFF));
  updi_put((uint8_t)(addr >> 8));
  return updi_expect_ack("stptr_w");
}

static bool stinc_b(uint8_t data) {
  updi_put(SYNCH);
  updi_put(0x64);
  updi_put(data);
  return updi_expect_ack("stinc_b");
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

template <bool preserve_ptr> bool nvm_cmd(uint8_t cmd) {
  uint16_t temp = 0;
  if (preserve_ptr) {
    updi_put(SYNCH);
    updi_put(0x29);
    uint8_t lo = 0xFF;
    uint8_t hi = 0xFF;
    if (!updi_get_byte(lo) || !updi_get_byte(hi)) {
      return fail_at("nvm_cmd_read_ptr");
    }
    temp = (uint16_t)lo | ((uint16_t)hi << 8);
  }
  if (!sts_b(NVM_BASE_ADDR + NVM::CTRLA, cmd)) {
    Serial.printf("[UPDI] NVM cmd 0x%02X failed\n", cmd);
    return fail_at("nvm_cmd_sts");
  }
  if (preserve_ptr) {
    if (!stptr_w(temp)) {
      return fail_at("nvm_cmd_restore_ptr");
    }
  }
  return true;
}

template <bool preserve_ptr> bool nvm_wait(uint32_t timeout_ms = 1000) {
  uint16_t temp = 0;
  if (preserve_ptr) {
    updi_put(SYNCH);
    updi_put(0x29);
    uint8_t lo = 0xFF;
    uint8_t hi = 0xFF;
    if (!updi_get_byte(lo) || !updi_get_byte(hi)) {
      return fail_at("nvm_wait_read_ptr");
    }
    temp = (uint16_t)lo | ((uint16_t)hi << 8);
  }
  const unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    const uint8_t status = lds_b(NVM_BASE_ADDR + NVM::STATUS);
    g_last_nvm_status = status;
    if (status == 0xFF) {
      return fail_at("nvm_wait_status_ff");
    }
    if ((status & 0x03) == 0) {
      if (preserve_ptr) {
        if (!stptr_w(temp)) {
          return fail_at("nvm_wait_restore_ptr");
        }
      }
      return true;
    }
    delayMicroseconds(10);
  }
  return fail_at("nvm_wait_timeout");
}

static void cpu_reset();

static bool nvm_set_addr(uint16_t addr) {
  return sts_b(NVM_BASE_ADDR + NVM::ADDR, (uint8_t)(addr & 0xFF)) &&
         sts_b(NVM_BASE_ADDR + NVM::ADDRH, (uint8_t)(addr >> 8));
}

static bool write_fuse(uint8_t offset, uint8_t value) {
  const uint16_t addr = FUSE_BASE_ADDR + offset;
  const uint8_t before = lds_b(addr);
  Serial.printf("[UPDI] fuse 0x%04X before=0x%02X target=0x%02X\n", addr, before, value);

  if (before == value) {
    return true;
  }
  if (!nvm_wait<false>()) {
    return fail_at("fuse_wait_before");
  }
  if (!nvm_set_addr(addr)) {
    g_fail_addr = addr;
    return fail_at("fuse_set_addr");
  }
  if (!sts_b(NVM_BASE_ADDR + NVM::DATA, value)) {
    g_fail_addr = addr;
    return fail_at("fuse_set_data");
  }
  if (!nvm_cmd<false>(NVM::WFU)) {
    g_fail_addr = addr;
    return fail_at("fuse_write_cmd");
  }
  if (!nvm_wait<false>()) {
    g_fail_addr = addr;
    return fail_at("fuse_wait_after");
  }

  const uint8_t after = lds_b(addr);
  Serial.printf("[UPDI] fuse 0x%04X after=0x%02X\n", addr, after);
  if (after != value) {
    g_fail_addr = addr;
    g_last_rx = after;
    return fail_at("fuse_verify");
  }
  return true;
}

static bool write_megatinycore_fuses() {
  if (!ATTINY_WRITE_MEGA_TINY_CORE_FUSES) {
    Serial.println("[UPDI] fuse write disabled");
    return true;
  }
  Serial.println("[UPDI] writing megaTinyCore fuses");
  bool ok = true;
  ok = ok && write_fuse(FUSE_WDTCFG, ATTINY_FUSE_WDTCFG);
  ok = ok && write_fuse(FUSE_OSCCFG, ATTINY_FUSE_OSCCFG);
  ok = ok && write_fuse(FUSE_SYSCFG0, ATTINY_FUSE_SYSCFG0);
  ok = ok && write_fuse(FUSE_SYSCFG1, ATTINY_FUSE_SYSCFG1);
  ok = ok && write_fuse(FUSE_APPEND, ATTINY_FUSE_APPEND);
  ok = ok && write_fuse(FUSE_BOOTEND, ATTINY_FUSE_BOOTEND);
  if (!ok) {
    return false;
  }
  Serial.println("[UPDI] fuse write complete; reset required");
  cpu_reset();
  return true;
}

static uint8_t cpu_mode_masked(uint8_t mask) {
  return (uint8_t)(lcds(UPDI_ASI_System_Status) & mask);
}

static void cpu_reset() {
  Serial.println("[UPDI] system reset pulse");
  stcs(UPDI_ASI_Reset_Request, RESET_ON);
  stcs(UPDI_ASI_Reset_Request, RESET_OFF);
  unsigned long t0 = millis();
  while (cpu_mode_masked(0x0E) == 0 && millis() - t0 < 2000) {
    delayMicroseconds(50);
  }
  Serial.printf("[UPDI] system reset wait done after %lu ms\n", (unsigned long)(millis() - t0));
}

static bool enter_prog_mode() {
  Serial.println("[UPDI] enter_prog start");
  cpu_reset();
  capture_asi_status();
  uint8_t system_status = g_diag_asi_masked;
  Serial.printf("[UPDI] ASI system status raw=0x%02X masked=0x%02X\n", g_diag_asi_raw,
                g_diag_asi_masked);
  switch (system_status) {
  case 0x82:
    Serial.println("[UPDI] target running; sending NVMPROG key");
    write_key(KEY_NVM_PROG);
    delay(2);
    {
      const unsigned long key_start = millis();
      uint8_t key_status = 0;
      while (millis() - key_start < 100) {
        key_status = lcds(UPDI_ASI_Key_Status);
        if (key_status & 0x10) {
          break;
        }
        delay(1);
      }
      Serial.printf("[UPDI] key status after NVMPROG=0x%02X\n", key_status);
      if ((key_status & 0x10) == 0) {
        return fail_at("enter_prog_key_not_latched");
      }
    }
    cpu_reset();
    {
      const unsigned long prog_start = millis();
      while (millis() - prog_start < 500) {
        system_status = (uint8_t)(lcds(UPDI_ASI_System_Status) & 0xEF);
        if (system_status == 0x08) {
          break;
        }
        delay(1);
      }
      Serial.printf("[UPDI] ASI after NVMPROG reset masked=0x%02X\n", system_status);
      if (system_status != 0x08) {
        return fail_at("enter_prog_not_started");
      }
    }
    /* fallthrough */
  case 0x08:
    Serial.println("[UPDI] target in NVM prog mode; clearing page buffer");
    return nvm_cmd<false>(NVM::PBC);
  default:
    return fail_at("enter_prog_bad_asi_status");
  }
}

static bool leave_prog_mode() {
  Serial.println("[UPDI] leave_prog start");
  if (!nvm_wait<false>()) {
    return fail_at("leave_nvm_wait");
  }

  // Datasheet NVM programming exit: issue and release System Reset, then disable UPDI.
  cpu_reset();
  uint8_t system_status = lcds(UPDI_ASI_System_Status);
  Serial.printf("[UPDI] leave ASI status=0x%02X\n", system_status);
  if (system_status == 0xFF) {
    return fail_at("leave_asi_ff");
  }
  Serial.println("[UPDI] disabling UPDI");
  stcs(UPDI_Control_B, UPDI_DISABLE);
  delay(2);
  log_diag("leave_prog_done");
  return true;
}

static bool chip_erase_device() {
  Serial.println("[UPDI] chip_erase key");
  write_key(KEY_CHIP_ERASE);
  cpu_reset();
  delay(100);
  const bool ok = enter_prog_mode();
  Serial.printf("[UPDI] chip_erase %s\n", ok ? "ok" : "failed");
  return ok;
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
    if (!nvm_wait<true>()) {
      return fail_at("send_block_nvm_wait");
    }
    rep(n);
    if (!stinc_b(src[idx])) {
      g_fail_offset = idx;
      return fail_at("send_block_first_byte");
    }
    for (uint8_t i = n; i; i--) {
      idx++;
      updi_put(src[idx]);
      if (!updi_expect_ack("send_block_data")) {
        g_fail_offset = idx;
        return fail_at("send_block_data_ack");
      }
    }
    idx++;
    return true;
  };

  uint16_t byte_idx = 0;
  uint16_t bytes_remaining = length;
  Serial.printf("[UPDI] write_flash start addr=0x%04X length=%u page_size=%u\n", address, length,
                page_size);
  if (!stptr_w(address)) {
    return fail_at("write_flash_set_ptr");
  }

  uint8_t unaligned = (uint8_t)((-address) & (page_size - 1));
  if (unaligned > bytes_remaining) {
    unaligned = (uint8_t)bytes_remaining;
  }
  if (unaligned) {
    if (!send_block(unaligned, byte_idx)) {
      return false;
    }
    bytes_remaining -= unaligned;
    if (!nvm_cmd<true>(NVM::WP)) {
      return false;
    }
  }
  while (bytes_remaining) {
    const uint16_t page_addr = (uint16_t)(address + byte_idx);
    const uint16_t before_idx = byte_idx;
    g_last_addr = page_addr;
    if (bytes_remaining >= page_size) {
      if (!send_block(page_size, byte_idx)) {
        g_fail_addr = page_addr;
        g_fail_offset = before_idx;
        return fail_at("write_flash_send_page");
      }
      bytes_remaining -= page_size;
    } else {
      if (!send_block((uint8_t)bytes_remaining, byte_idx)) {
        g_fail_addr = page_addr;
        g_fail_offset = before_idx;
        return fail_at("write_flash_send_final");
      }
      bytes_remaining = 0;
    }
    g_pages_loaded++;
    g_bytes_loaded = byte_idx;
    if (!nvm_cmd<true>(NVM::WP)) {
      g_fail_addr = page_addr;
      g_fail_offset = before_idx;
      return fail_at("write_flash_page_cmd");
    }
    g_pages_committed++;
  }
  Serial.printf("[UPDI] write_flash done pages=%u bytes=%u last_addr=0x%04X\n", g_pages_committed,
                g_bytes_loaded, g_last_addr);
  return true;
}

bool updi_open() {
  Serial.println("[UPDI] open start");
  for (uint8_t attempt = 1; attempt <= 2; attempt++) {
    Serial.printf("[UPDI] open attempt %u\n", attempt);
    updi_close();
    delay(attempt == 1 ? 10 : 50);
    if (!updi_double_break()) {
      continue;
    }
    // Match jtag2updi sign_on STCS for UART UPDI.
    stcs(UPDI_Control_B, 8);
    stcs(UPDI_Control_A, 0x80);
    uint8_t status_a = lcds(UPDI_Status_A);
    uint8_t status_b = lcds(UPDI_Status_B);
    Serial.printf("[UPDI] open status_a=0x%02X status_b=0x%02X\n", status_a, status_b);
    if (status_a != 0xFF) {
      log_diag("open_done");
      return true;
    }
    log_diag("open_no_reply_retry");
  }
  return fail_at("open_no_reply");
}

void updi_close() {
  Serial.println("[UPDI] close");
  if (g_uart_installed) {
    (void)uart_driver_delete(UPDI_UART_NUM);
    g_uart_installed = false;
  }
  pinMode(PIN_UPDI_RX, INPUT_PULLUP);
  pinMode(PIN_UPDI_TX, INPUT);
}

UPDIResult updi_program_flash(const uint8_t *data, size_t length,
                              void (*progress)(int, const char *, void *), void *ctx) {
  updi_clear_diag();
  Serial.printf("[UPDI] program_flash start length=%u flash_base=0x%04X flash_size=%lu page=%u\n",
                (unsigned)length, ATTINY_FLASH_BYTE_BASE, (unsigned long)ATTINY_FLASH_SIZE,
                ATTINY_FLASH_PAGE_SIZE);

  if (!data || length == 0 || length > ATTINY_FLASH_SIZE) {
    Serial.println("[UPDI] bad image size");
    return UPDI_RESULT_WRITE_FAILED;
  }

  if (progress) {
    progress(0, "open", ctx);
  }
  if (!updi_open()) {
    updi_close();
    log_diag("program_open_failed");
    return UPDI_RESULT_OPEN_FAILED;
  }

  if (progress) {
    progress(10, "chip_erase", ctx);
  }
  if (!chip_erase_device()) {
    updi_close();
    log_diag("program_chip_erase_failed");
    return UPDI_RESULT_CHIP_ERASE_FAILED;
  }

  if (progress) {
    progress(12, "fuses", ctx);
  }
  if (!write_megatinycore_fuses()) {
    leave_prog_mode();
    updi_close();
    log_diag("program_fuse_failed");
    return UPDI_RESULT_WRITE_FAILED;
  }
  if (!enter_prog_mode()) {
    updi_close();
    log_diag("program_reenter_after_fuses_failed");
    return UPDI_RESULT_ENTER_PROG_FAILED;
  }

  if (progress) {
    progress(15, "writing", ctx);
  }

  const uint16_t flash_base = ATTINY_FLASH_BYTE_BASE;
  if (!nvm_buffered_write_flash(flash_base, data, (uint16_t)length, ATTINY_FLASH_PAGE_SIZE)) {
    leave_prog_mode();
    updi_close();
    log_diag("program_write_failed");
    return UPDI_RESULT_WRITE_FAILED;
  }

  // Simple verify: read back via UPDI lds
  if (progress) {
    progress(85, "verify", ctx);
  }
  for (size_t i = 0; i < length; i++) {
    uint8_t v = lds_b((uint16_t)(flash_base + i));
    if (v != data[i]) {
      g_fail_addr = (uint16_t)(flash_base + i);
      g_fail_offset = i;
      Serial.printf("[UPDI] verify mismatch offset=%u addr=0x%04X want=0x%02X got=0x%02X\n",
                    (unsigned)i, (unsigned)(flash_base + i), data[i], v);
      leave_prog_mode();
      updi_close();
      log_diag("program_verify_failed");
      return UPDI_RESULT_VERIFY_FAILED;
    }
    g_bytes_verified = (uint16_t)(i + 1);
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
    log_diag("program_leave_failed");
    return UPDI_RESULT_LEAVE_FAILED;
  }

  updi_close();
  log_diag("program_done");
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
