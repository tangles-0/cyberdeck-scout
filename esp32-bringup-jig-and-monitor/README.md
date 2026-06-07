# Unified ESP32 service

Single sketch combining:

- **BQ76905** — direct I2C read/monitor (same tile UI idea as `esp-bq-monitor`), plus actions: reconfigure, toggle deep sleep, ship/shutdown I2C commands.
- **TPS25751 + BQ25792** — JSON snapshot on **I2Ct** only (TPS as I2C target + BQ25792). The ESP does **not** use I2Cc for monitoring; I2Cc is controller-side on your board and is not where TPS status is read.
- **Flashing** — browser upload to LittleFS staging (`/stg_eeprom.bin`, `/stg_attiny.bin`), then background jobs:
  - **EEPROM** on **I2Cc** (AT24). **Power TPS25751 down** before starting — firmware temporarily remaps the second hardware I2C peripheral from I2Ct pins to I2Cc pins for the duration of the job, then restores I2Ct for `/api/charger`.
  - **ATtiny202** over UART-mode **UPDI** (same framing ideas as `jtag2updi`).

## I2C wiring model

| Bus   | Role |
|-------|------|
| First `TwoWire(0)` | BQ76905 (pack / ATTiny host side) |
| Second `TwoWire(1)` @ **I2Ct** pins | TPS25751 target + BQ25792 — **monitoring** |
| Same peripheral @ **I2Cc** pins | EEPROM programming **only** while TPS is off |

Adjust **`PIN_I2C_I2CT_*`** and **`PIN_I2C_I2CC_*`** in [config.h](config.h) to match your PCB.

## Configure before build

Edit **[config.h](config.h)**:

- `WIFI_SSID` / `WIFI_PASS`
- `PIN_I2C_BQ_*` — pack monitor I2C.
- `PIN_I2C_I2CT_*` — charger / PD monitor (TPS + BQ25792).
- `PIN_I2C_I2CC_*` — EEPROM (flash job only).
- `PIN_UPDI_TX` / `PIN_UPDI_RX` — UPDI (typical: TX through resistor to UPDI pin, RX tied to that pin; **230400 8E2** after double-break).

## Build / upload

Arduino IDE or CLI, ESP32 core, board such as **ESP32 Dev Module** (`esp32:esp32:esp32`).

Partition scheme: default usually suffices; LittleFS is created with `LittleFS.begin(true)` on first boot.

## Web UI

Open device IP in a browser. Tabs:

1. **BQ76905** — telemetry + tiles; buttons hit `/api/bq/action?cmd=reconfig|toggle_sleep|ship`.
2. **Charger / PD** — formatted `/api/charger` JSON (or a suspended message while an EEPROM job holds the bus).
3. **Flash** — upload EEPROM / ATTiny binaries, then **Start**; poll `/api/flash/status`.

## Limitations

- **ATTiny UPDI** path is a focused application programmer (no Arduino-IDE JTAG2 bridge, no fuse writes). If chip/part NVM layout differs from ATtiny202 defaults in `config.h`, adjust `ATTINY202_FLASH_*` constants.
- During EEPROM programming the charger tab may show `charger_monitor_suspended` — normal until the job finishes and I2Ct is restored.
