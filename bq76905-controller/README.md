# BQ76905 Controller (ATtiny202)

This firmware configures a `BQ76905` for a 2S Li-ion pack, handles button-driven
sleep/ship mode actions, and now streams pack telemetry out of a single pin
(`PA6`) for external diagnostics.

## What Was Added

- State-of-charge (SoC) tracking in firmware (voltage-based estimate)
- One-wire TX-only telemetry stream on `PA6`
- A fixed, compact binary frame protocol with checksum
- 10 Hz polling/telemetry update loop (100 ms period)
- Pack current telemetry from BQ current register

## SoC Tracking Method

The `BQ76905` does not report SoC directly, so this firmware estimates SoC from
cell voltage:

- Reads `Cell1Voltage` and `Cell2Voltage` every 100 ms over I2C
- Computes average cell voltage from the 2S pack
- Maps average cell voltage to SoC using a piecewise Li-ion OCV table

Important:
- This SoC is an estimate, best at low current / rested conditions
- Under heavy load or charging, voltage sag/rise causes temporary SoC bias
- If needed later, this can be upgraded to hybrid OCV + coulomb integration

## Telemetry Output (PA6)

### Physical / Electrical

- Pin: `PA6`
- Direction: TX only (no RX)
- UART style: `2400 8N1`
- Idle line level: HIGH
- Logic level: same as ATtiny202 VCC

This is software UART bit-banged in firmware so only one MCU pin is required.

### Stream Rate

- Nominal: 10 frames/s (every 100 ms)
- Chosen as the highest practical rate that stays simple and robust with:
  - I2C reads for all required registers
  - software UART TX on a small MCU
  - easy parsing on ESP32

## Protocol Specification

Each frame is a fixed 21-byte binary packet:

```
Byte 0  : 0xA5             // sync 1
Byte 1  : 0x5A             // sync 2
Byte 2  : frame_lsb
Byte 3  : frame_msb
Byte 4  : cell1_mV_lsb
Byte 5  : cell1_mV_msb
Byte 6  : cell2_mV_lsb
Byte 7  : cell2_mV_msb
Byte 8  : current_raw_lsb  // signed int16 from BQ current register
Byte 9  : current_raw_msb
Byte 10 : soc_pct              // reserved in current firmware, presently 0
Byte 11 : battery_status_lsb   // raw BatteryStatus (0x12)
Byte 12 : battery_status_msb
Byte 13 : alarm_status_lsb     // raw AlarmStatus (0x62)
Byte 14 : alarm_status_msb
Byte 15 : alarm_raw_status_lsb // raw AlarmRawStatus (0x64)
Byte 16 : alarm_raw_status_msb
Byte 17 : safety_status_a      // raw SafetyStatusA (0x03)
Byte 18 : safety_status_b      // raw SafetyStatusB (0x05)
Byte 19 : flags                // bitfield summary
Byte 20 : checksum_xor         // XOR of bytes 0..19
```

Checksum:
- `checksum_xor` is a single-byte XOR over bytes `0..19`

### Field Notes

- `frame` is a rolling 16-bit counter (`uint16`)
- `cell1_mV` / `cell2_mV` are raw BQ readings in mV
- `current_raw` is signed raw current register data from BQ (LSB/MSB little-endian)
- `soc_pct` is reserved in current firmware and currently fixed to 0
- `battery_status` is raw `BatteryStatus` (`0x12`)
- `alarm_status` is raw `AlarmStatus` (`0x62`)
- `alarm_raw_status` is raw `AlarmRawStatus` (`0x64`)
- `safety_status_a` / `safety_status_b` are raw `SafetyStatusA/B` (`0x03` / `0x05`)
- `flags bit0` deep-sleep state (host state)
- `flags bit1` balancing gate active (cell/current criteria true)
- `flags bit2` BQ-reported balancing bit from `BatteryStatus`
- `flags bit3` `AlarmStatus != 0`
- `flags bit4` `SafetyStatusA != 0`
- `flags bit5` `SafetyStatusB != 0`
- `flags bit6` `BatteryStatus != 0`

For mA conversion on ESP32, apply the transfer function from your BQ76905 TRM
and your shunt value/calibration.

### Example Packet (hex bytes)

```
A5 5A 34 12 7E 0E 80 0E D2 FF 00 44 00 08 00 08 00 01 00 48 45
```

Interpretation:
- `frame = 0x1234`
- `cell1_mV = 0x0E7E = 3710`
- `cell2_mV = 0x0E80 = 3712`
- `current_raw = 0xFFD2 = -46`
- `soc_pct = 0x00` (reserved)
- `battery_status = 0x0044`
- `alarm_status = 0x0008`
- `alarm_raw_status = 0x0008`
- `safety_status_a = 0x01`, `safety_status_b = 0x00`
- `flags = 0x48`

## ESP32 Receiver Notes

- Connect ATtiny `PA6` -> ESP32 RX pin (same voltage domain)
- Configure UART with `2400 8N1`
- Find sync word `0xA5 0x5A`, then read the next 19 bytes
- Verify `xor(packet[0..19]) == packet[20]`
- Parse fields as little-endian integers

Minimal sketch skeleton:

```cpp
HardwareSerial diag(1);

void setup() {
  Serial.begin(115200);
  diag.begin(2400, SERIAL_8N1, 16, -1); // RX=GPIO16, TX disabled
}

void loop() {
  while (diag.available()) {
    if (diag.read() != 0xA5) {
      continue;
    }
    if (!diag.available() || diag.read() != 0x5A) {
      continue;
    }
    uint8_t pkt[14];
    pkt[0] = 0xA5;
    pkt[1] = 0x5A;
    if (diag.readBytes(&pkt[2], 12) != 12) {
      return;
    }
    uint8_t cs = 0;
    for (uint8_t i = 0; i < 13; i++) cs ^= pkt[i];
    if (cs != pkt[13]) {
      Serial.println("bad checksum");
      continue;
    }

    uint16_t frame = (uint16_t)pkt[2] | ((uint16_t)pkt[3] << 8);
    uint16_t c1 = (uint16_t)pkt[4] | ((uint16_t)pkt[5] << 8);
    uint16_t c2 = (uint16_t)pkt[6] | ((uint16_t)pkt[7] << 8);
    int16_t currentRaw = (int16_t)((uint16_t)pkt[8] | ((uint16_t)pkt[9] << 8));
    uint8_t soc = pkt[10];
    uint8_t batteryStatus = pkt[11];
    uint8_t flags = pkt[12];
    bool deepSleep = (flags & 0x01) != 0;
    bool balanceGate = (flags & 0x02) != 0;
    bool bqBalancing = (flags & 0x04) != 0;
    bool alarmActive = (flags & 0x08) != 0;
    bool safetyAActive = (flags & 0x10) != 0;
    bool safetyBActive = (flags & 0x20) != 0;
    bool batteryStatusNonZero = (flags & 0x40) != 0;

    Serial.printf("f=%u c1=%umV c2=%umV iRaw=%d soc=%u%% bstat=0x%02X flags=0x%02X [%s,%s,%s,%s,%s,%s,%s]\n",
                  frame, c1, c2, currentRaw, soc, batteryStatus, flags,
                  deepSleep ? "DEEPSLEEP" : "NORMAL",
                  balanceGate ? "BAL_GATE=1" : "BAL_GATE=0",
                  bqBalancing ? "BQ_BAL=1" : "BQ_BAL=0",
                  alarmActive ? "ALARM=1" : "ALARM=0",
                  safetyAActive ? "SAFEA=1" : "SAFEA=0",
                  safetyBActive ? "SAFEB=1" : "SAFEB=0",
                  batteryStatusNonZero ? "BSTAT!=0" : "BSTAT=0");
  }
}
```

## Build / Flash (ATtiny202)

### Arduino IDE

1. Install **megaTinyCore** (Spence Konde)
2. Board: **ATtiny202**
3. Clock: match your target (typically 20 MHz internal)
4. Open `bq76905-controller.ino`
5. Use **Sketch -> Export Compiled Binary**

### UPDI Wiring (Pro Micro running jtag2updi)

- Pro Micro `TX1` -> ATtiny202 `UPDI` through 4.7k
- Pro Micro `RX1` -> ATtiny202 `UPDI` direct
- GND common
- VCC common (3.3 V or 5 V, matched)

### avrdude

```bash
avrdude -C ./avrdude.conf -c jtag2updi -P /dev/ttyACM0 -b 115200 -p t202 \
  -U flash:w:bq76905-controller.ino.hex
```

Replace `/dev/ttyACM0` with your port.

