
/*
  Passive I2C sniffer for ESP32.

  Connect ESP32 GND to the target GND, then connect SCL/SDA to the bus you want
  to observe. Do not add pullups or drive the lines from this sketch. The ESP32
  pins are inputs only, so the target board must already have valid pullups.

  IMPORTANT: ESP32 GPIOs are not 5 V tolerant. Level shift the bus if needed.

  The output format is:
    W 0x2C ACK : 0x20+ 0x01+ 0x23+
    R 0x2C ACK : 0xA5+

  The first byte after ':' is usually the register address for register writes,
  followed by one or more data bytes. A '+' after a byte means the byte was ACKed
  by the receiver; '-' means NACK.
*/

#include <Arduino.h>
#include "driver/gpio.h"

// Change these to match where you clip onto the Waveshare board.
static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;

static constexpr uint32_t SERIAL_BAUD = 921600;
static constexpr size_t MAX_BYTES_PER_TRANSACTION = 128;
static constexpr size_t TRANSACTION_QUEUE_DEPTH = 16;

struct I2CTransaction {
  uint32_t startedAtUs;
  uint32_t stoppedAtUs;
  uint8_t bytes[MAX_BYTES_PER_TRANSACTION];
  bool ack[MAX_BYTES_PER_TRANSACTION];
  uint16_t length;
  bool overflow;
  bool repeatedStart;
};

static portMUX_TYPE i2cMux = portMUX_INITIALIZER_UNLOCKED;

static I2CTransaction transactionQueue[TRANSACTION_QUEUE_DEPTH];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;
static volatile uint32_t droppedTransactions = 0;

static I2CTransaction currentTransaction;
static volatile bool inTransaction = false;
static volatile uint8_t currentByte = 0;
static volatile uint8_t currentBit = 0;

static inline bool IRAM_ATTR readScl() {
  return gpio_get_level(static_cast<gpio_num_t>(SCL_PIN)) != 0;
}

static inline bool IRAM_ATTR readSda() {
  return gpio_get_level(static_cast<gpio_num_t>(SDA_PIN)) != 0;
}

static void IRAM_ATTR resetCurrentTransaction(bool repeatedStart) {
  currentTransaction.startedAtUs = micros();
  currentTransaction.stoppedAtUs = 0;
  currentTransaction.length = 0;
  currentTransaction.overflow = false;
  currentTransaction.repeatedStart = repeatedStart;
  currentByte = 0;
  currentBit = 0;
  inTransaction = true;
}

static void IRAM_ATTR queueCurrentTransaction(uint32_t stoppedAtUs) {
  if (!inTransaction || currentTransaction.length == 0) {
    inTransaction = false;
    return;
  }

  currentTransaction.stoppedAtUs = stoppedAtUs;

  const uint8_t nextHead = (queueHead + 1) % TRANSACTION_QUEUE_DEPTH;
  if (nextHead == queueTail) {
    droppedTransactions++;
  } else {
    transactionQueue[queueHead] = currentTransaction;
    queueHead = nextHead;
  }

  inTransaction = false;
}

static void IRAM_ATTR onSdaChange() {
  const bool scl = readScl();
  if (!scl) {
    return;
  }

  const bool sda = readSda();
  const uint32_t now = micros();

  portENTER_CRITICAL_ISR(&i2cMux);
  if (!sda) {
    // START or repeated START: SDA falls while SCL is high.
    if (inTransaction && currentTransaction.length > 0) {
      queueCurrentTransaction(now);
      resetCurrentTransaction(true);
    } else {
      resetCurrentTransaction(false);
    }
  } else {
    // STOP: SDA rises while SCL is high.
    queueCurrentTransaction(now);
  }
  portEXIT_CRITICAL_ISR(&i2cMux);
}

static void IRAM_ATTR onSclRise() {
  if (!readScl()) {
    return;
  }

  const bool sda = readSda();

  portENTER_CRITICAL_ISR(&i2cMux);
  if (!inTransaction) {
    portEXIT_CRITICAL_ISR(&i2cMux);
    return;
  }

  if (currentBit < 8) {
    currentByte = static_cast<uint8_t>((currentByte << 1) | (sda ? 1 : 0));
    currentBit++;
  } else {
    // Ninth clock is ACK/NACK. ACK is an active-low bit.
    if (currentTransaction.length < MAX_BYTES_PER_TRANSACTION) {
      const uint16_t index = currentTransaction.length;
      currentTransaction.bytes[index] = currentByte;
      currentTransaction.ack[index] = !sda;
      currentTransaction.length++;
    } else {
      currentTransaction.overflow = true;
    }

    currentByte = 0;
    currentBit = 0;
  }
  portEXIT_CRITICAL_ISR(&i2cMux);
}

static bool popTransaction(I2CTransaction &out) {
  bool available = false;

  portENTER_CRITICAL(&i2cMux);
  if (queueTail != queueHead) {
    out = transactionQueue[queueTail];
    queueTail = (queueTail + 1) % TRANSACTION_QUEUE_DEPTH;
    available = true;
  }
  portEXIT_CRITICAL(&i2cMux);

  return available;
}

static void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void printTransaction(const I2CTransaction &transaction) {
  if (transaction.length == 0) {
    return;
  }

  const uint8_t addressByte = transaction.bytes[0];
  const uint8_t address = addressByte >> 1;
  const bool read = (addressByte & 0x01) != 0;

  Serial.print(transaction.startedAtUs);
  Serial.print(" us ");
  if (transaction.repeatedStart) {
    Serial.print("Sr ");
  }
  Serial.print(read ? "R " : "W ");
  Serial.print("0x");
  printHexByte(address);
  Serial.print(transaction.ack[0] ? " ACK" : " NACK");
  Serial.print(" :");

  for (uint16_t i = 1; i < transaction.length; i++) {
    Serial.print(" 0x");
    printHexByte(transaction.bytes[i]);
    Serial.print(transaction.ack[i] ? '+' : '-');
  }

  if (transaction.overflow) {
    Serial.print(" ...OVERFLOW");
  }

  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(SDA_PIN), onSdaChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SCL_PIN), onSclRise, RISING);

  Serial.println();
  Serial.println("ESP32 passive I2C sniffer");
  Serial.print("SDA GPIO ");
  Serial.print(SDA_PIN);
  Serial.print(", SCL GPIO ");
  Serial.println(SCL_PIN);
  Serial.println("Output: <time> <R/W> <7-bit addr> <address ACK> : <data byte><ACK + / NACK ->");
}

void loop() {
  I2CTransaction transaction;
  while (popTransaction(transaction)) {
    printTransaction(transaction);
  }

  static uint32_t lastStatusMs = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastStatusMs >= 2000) {
    lastStatusMs = nowMs;

    uint32_t dropped;
    portENTER_CRITICAL(&i2cMux);
    dropped = droppedTransactions;
    portEXIT_CRITICAL(&i2cMux);

    if (dropped > 0) {
      Serial.print("WARN dropped transactions: ");
      Serial.println(dropped);
    }
  }
}

