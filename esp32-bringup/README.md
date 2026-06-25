# Unified ESP32 service

Single sketch combining:

- **BQ76905** — negotiated I2C control with the ATtiny404. The Tiny owns the BQ by default, then hands controller role to the ESP when it detects the ESP discovery target.
- **TPS25751 + BQ25792** — JSON snapshot on **I2Ct** only (TPS as I2C target + BQ25792). The ESP does **not** use I2Cc for monitoring; I2Cc is controller-side on your board and is not where TPS status is read.
- **Flashing** — browser upload to LittleFS staging (`/stg_eeprom.bin`, `/stg_attiny.bin`), then background jobs:
  - **EEPROM** on **I2Cc** (AT24). **Power TPS25751 down** before starting — firmware temporarily remaps the second hardware I2C peripheral from I2Ct pins to I2Cc pins for the duration of the job, then restores I2Ct for `/api/charger`.
  - **ATtiny404** over UART-mode **UPDI** (same framing ideas as `jtag2updi`).

## I2C wiring model

| Bus   | Role |
|-------|------|
| First `TwoWire(0)` | BQ76905 / ATtiny404 shared bus. ESP starts as discovery target `ESP_BQ_DISCOVERY_ADDR`, then becomes controller after Tiny handoff |
| Second `TwoWire(1)` @ **I2Ct** pins | TPS25751 target + BQ25792 — **monitoring** |
| Same peripheral @ **I2Cc** pins | EEPROM programming **only** while TPS is off |

Current defaults use GPIO **25/26** for **I2Ct** charger monitoring and GPIO **18/19** for the optional **I2Cc** EEPROM-flash remap. Adjust **`PIN_I2C_I2CT_*`** and **`PIN_I2C_I2CC_*`** in [config.h](config.h) to match your wiring before building.

## Configure before build

Edit **[config.h](config.h)**:

- `WIFI_SSID` / `WIFI_PASS`
- `PIN_I2C_BQ_*` — shared BQ76905 / ATtiny404 bus. Default firmware starts the ESP as an I2C discovery target at `0x42`; after handoff, the ESP becomes BQ controller and pings the Tiny target at `0x43`.
- `PIN_I2C_I2CT_*` — charger / PD monitor (TPS + BQ25792).
- `PIN_I2C_I2CC_*` — EEPROM (flash job only).
- `PIN_UPDI_RX` / `PIN_UPDI_TX` — jtag2updi-style UPDI wiring: RX direct to target UPDI, TX to target UPDI through a series resistor; **115200 8E2** after double-break by default.

## Build / upload

Arduino IDE or CLI, ESP32 core, board such as **ESP32 Dev Module** (`esp32:esp32:esp32`).

Partition scheme: default usually suffices; LittleFS is created with `LittleFS.begin(true)` on first boot.

## Web UI

Open device IP in a browser. Tabs:

1. **BQ76905** — telemetry + tiles from direct ESP reads after handoff. Before handoff, buttons are disabled because the Tiny still owns BQ configuration and balancing. Tiny probes the ESP every ~3 s, tells it to become controller, then listens as a target for 500 ms ESP pings. The first ping gets an 8 s startup grace window; after pings are established, a >1 s miss returns the Tiny to BQ master mode, reconfigures the BQ, and resumes ESP discovery.
2. **Charger / PD** — formatted `/api/charger` JSON (or a suspended message while an EEPROM job holds the bus).
3. **Flash** — upload EEPROM / ATTiny binaries, then **Start**; poll `/api/flash/status`.

## Limitations

- **ATTiny UPDI** path is a focused application programmer (no Arduino-IDE JTAG2 bridge, no fuse writes). Target is **ATtiny404** (4 KB flash) via `ATTINY_FLASH_*` in `config.h`. Wire the ESP32 like the working Pro Micro setup: RX direct to the ATtiny UPDI pin and TX through the resistor. On enter-prog failure, `/api/flash/status` includes `updi_asi_status` (raw `ASI_System_Status`; `0xFF` usually means no UPDI reply).
- During EEPROM programming the charger tab may show `charger_monitor_suspended` — normal until the job finishes and I2Ct is restored.
