
/*
  ESP32 I2C bring-up replay sketch.

  This replays the I2C traffic captured from the working Waveshare board, but
  only when commanded over Serial.

  Serial commands:
    b, bringup  Replay the captured bring-up sequence
    scan        Print addresses that ACK on this bus
    status      Print raw SDA/SCL levels
    recover     Pulse SCL to try to release a stuck slave, then re-init I2C
    h, help     Show commands

  Wiring:
    ESP32 GND -> target GND
    ESP32 SDA -> target SDA
    ESP32 SCL -> target SCL

  ESP32 GPIOs are not 5 V tolerant. Level shift if the target bus is 5 V.
*/

#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;

// Change these to match your PCB wiring.
static constexpr gpio_num_t SDA_PIN = GPIO_NUM_21;
static constexpr gpio_num_t SCL_PIN = GPIO_NUM_22;

static constexpr uint32_t SERIAL_BAUD = 921600;
static constexpr uint32_t I2C_CLOCK_HZ = 100000;
static constexpr bool ENABLE_INTERNAL_PULLUPS = false;
static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(100);

struct I2CStep {
  uint8_t address;
  bool read;
  const uint8_t *data;
  size_t length;
  bool checkAck;
};

static const uint8_t data_2c_20[] = {0x20};
static const uint8_t data_60_82[] = {0x82};

static String inputLine;

static void printHelp();
static void printBusStatus();

static bool busIdle() {
  return gpio_get_level(SDA_PIN) != 0 && gpio_get_level(SCL_PIN) != 0;
}

static void printBusStatus() {
  const int sda = gpio_get_level(SDA_PIN);
  const int scl = gpio_get_level(SCL_PIN);

  Serial.print("Bus levels: SDA=");
  Serial.print(sda);
  Serial.print(", SCL=");
  Serial.print(scl);
  Serial.println(busIdle() ? " (idle/high)" : " (not idle)");

  if (sda == 0) {
    Serial.println("  SDA low: a device may be holding data low, pins may be swapped, or pullups/power may be wrong.");
  }
  if (scl == 0) {
    Serial.println("  SCL low: the bus cannot clock; check wiring, shorts, target reset state, and pullups.");
  }
}

static void initI2c() {
  i2c_config_t config = {};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = SDA_PIN;
  config.scl_io_num = SCL_PIN;
  config.sda_pullup_en = ENABLE_INTERNAL_PULLUPS ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  config.scl_pullup_en = ENABLE_INTERNAL_PULLUPS ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  config.master.clk_speed = I2C_CLOCK_HZ;
  config.clk_flags = 0;

  ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &config));

  const esp_err_t deleteResult = i2c_driver_delete(I2C_PORT);
  if (deleteResult != ESP_OK && deleteResult != ESP_ERR_INVALID_STATE) {
    Serial.print("WARN i2c_driver_delete: ");
    Serial.println(esp_err_to_name(deleteResult));
  }

  ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, config.mode, 0, 0, 0));
}

static esp_err_t runSteps(const char *label, const I2CStep *steps, size_t stepCount) {
  if (!busIdle()) {
    Serial.print(label);
    Serial.println(": bus is not idle before command");
    printBusStatus();
  }

  i2c_cmd_handle_t command = i2c_cmd_link_create();
  if (command == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  for (size_t i = 0; i < stepCount; i++) {
    const I2CStep &step = steps[i];
    i2c_master_start(command);

    const uint8_t addressByte = static_cast<uint8_t>((step.address << 1) | (step.read ? 1 : 0));
    i2c_master_write_byte(command, addressByte, step.checkAck);

    if (!step.read && step.length > 0) {
      i2c_master_write(command, const_cast<uint8_t *>(step.data), step.length, step.checkAck);
    }
  }

  i2c_master_stop(command);
  const esp_err_t result = i2c_master_cmd_begin(I2C_PORT, command, I2C_TIMEOUT_TICKS);
  i2c_cmd_link_delete(command);

  Serial.print(label);
  Serial.print(": ");
  Serial.println(esp_err_to_name(result));

  return result;
}

static esp_err_t runOne(const char *label, uint8_t address, bool read, const uint8_t *data, size_t length, bool checkAck) {
  const I2CStep step = {address, read, data, length, checkAck};
  return runSteps(label, &step, 1);
}

static void runBringup() {
  Serial.println("Running captured I2C bring-up sequence...");
  printBusStatus();

  // Captured:
  // 19273087 us W 0x2C ACK : 0x20+
  runOne("01 W 0x2C : 0x20", 0x2C, false, data_2c_20, sizeof(data_2c_20), true);

  // Address-only reads/writes are preserved because the capture showed them.
  runOne("02 R 0x45 address only", 0x45, true, nullptr, 0, true);
  runOne("03 W 0x60 expected NACK", 0x60, false, nullptr, 0, false);
  runOne("04 W 0x28 address only", 0x28, false, nullptr, 0, true);
  runOne("05 W 0x60 expected NACK", 0x60, false, nullptr, 0, false);
  runOne("06 W 0x60 expected NACK", 0x60, false, nullptr, 0, false);

  // Captured:
  // 19275775 us W 0x60 ACK : 0x82+
  runOne("07 W 0x60 : 0x82", 0x60, false, data_60_82, sizeof(data_60_82), false);

  runOne("08 R 0x2C address only", 0x2C, true, nullptr, 0, true);

  // Captured as a repeated-start pair:
  // 19276260 us W 0x61 NACK :
  // 19276366 us Sr W 0x54 ACK :
  {
    const I2CStep steps[] = {
      {0x61, false, nullptr, 0, false},
      {0x54, false, nullptr, 0, true},
    };
    runSteps("09 W 0x61 NACK, Sr W 0x54", steps, sizeof(steps) / sizeof(steps[0]));
  }

  // Captured as a repeated-start pair:
  // 19276477 us W 0x2C ACK :
  // 19276562 us Sr R 0x58 NACK :
  {
    const I2CStep steps[] = {
      {0x2C, false, nullptr, 0, true},
      {0x58, true, nullptr, 0, false},
    };
    runSteps("10 W 0x2C, Sr R 0x58 NACK", steps, sizeof(steps) / sizeof(steps[0]));
  }

  runOne("11 W 0x28 address only", 0x28, false, nullptr, 0, true);
  runOne("12 R 0x00 address only", 0x00, true, nullptr, 0, true);
  runOne("13 W 0x40 expected NACK", 0x40, false, nullptr, 0, false);
  runOne("14 W 0x08 address only", 0x08, false, nullptr, 0, true);

  Serial.println("Bring-up replay complete.");
}

static bool addressAcks(uint8_t address) {
  i2c_cmd_handle_t command = i2c_cmd_link_create();
  if (command == nullptr) {
    return false;
  }

  i2c_master_start(command);
  i2c_master_write_byte(command, static_cast<uint8_t>(address << 1), true);
  i2c_master_stop(command);
  const esp_err_t result = i2c_master_cmd_begin(I2C_PORT, command, I2C_TIMEOUT_TICKS);
  i2c_cmd_link_delete(command);

  return result == ESP_OK;
}

static void scanBus() {
  Serial.println("Scanning I2C bus...");
  printBusStatus();

  if (!busIdle()) {
    Serial.println("Scan will probably timeout until both lines idle high.");
  }

  bool foundAny = false;

  for (uint8_t address = 1; address < 0x7F; address++) {
    if (addressAcks(address)) {
      foundAny = true;
      Serial.print("ACK 0x");
      if (address < 0x10) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
    }
  }

  if (!foundAny) {
    Serial.println("No ACKing devices found.");
  }
}

static void recoverBus() {
  Serial.println("Trying I2C bus recovery...");
  printBusStatus();

  i2c_driver_delete(I2C_PORT);

  gpio_set_direction(SDA_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(SDA_PIN, ENABLE_INTERNAL_PULLUPS ? GPIO_PULLUP_ONLY : GPIO_FLOATING);

  gpio_set_direction(SCL_PIN, GPIO_MODE_OUTPUT_OD);
  gpio_set_pull_mode(SCL_PIN, ENABLE_INTERNAL_PULLUPS ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
  gpio_set_level(SCL_PIN, 1);
  delayMicroseconds(10);

  for (uint8_t i = 0; i < 9 && gpio_get_level(SDA_PIN) == 0; i++) {
    gpio_set_level(SCL_PIN, 0);
    delayMicroseconds(10);
    gpio_set_level(SCL_PIN, 1);
    delayMicroseconds(10);
  }

  // Generate a STOP condition: SDA low while SCL high, then release SDA high.
  gpio_set_direction(SDA_PIN, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(SDA_PIN, 0);
  delayMicroseconds(10);
  gpio_set_level(SCL_PIN, 1);
  delayMicroseconds(10);
  gpio_set_level(SDA_PIN, 1);
  delayMicroseconds(10);

  gpio_set_direction(SDA_PIN, GPIO_MODE_INPUT);
  gpio_set_direction(SCL_PIN, GPIO_MODE_INPUT);

  initI2c();
  printBusStatus();
}

static void handleCommand(String command) {
  command.trim();
  command.toLowerCase();

  if (command.length() == 0) {
    return;
  }

  if (command == "b" || command == "bringup") {
    runBringup();
  } else if (command == "scan") {
    scanBus();
  } else if (command == "status") {
    printBusStatus();
  } else if (command == "recover") {
    recoverBus();
  } else if (command == "h" || command == "help" || command == "?") {
    printHelp();
  } else {
    Serial.print("Unknown command: ");
    Serial.println(command);
    printHelp();
  }
}

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  b, bringup  Replay captured I2C bring-up");
  Serial.println("  scan        Scan for ACKing 7-bit I2C addresses");
  Serial.println("  status      Print raw SDA/SCL levels");
  Serial.println("  recover     Pulse SCL to try to release a stuck slave");
  Serial.println("  h, help     Show this help");
  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  initI2c();

  Serial.println();
  Serial.println("ESP32 ICN6211/Waveshare I2C bring-up replay");
  Serial.print("SDA GPIO ");
  Serial.print(static_cast<int>(SDA_PIN));
  Serial.print(", SCL GPIO ");
  Serial.print(static_cast<int>(SCL_PIN));
  Serial.print(", clock ");
  Serial.print(I2C_CLOCK_HZ);
  Serial.println(" Hz");
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      handleCommand(inputLine);
      inputLine = "";
    } else {
      inputLine += ch;
    }
  }
}

