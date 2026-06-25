# CB2 DSI1 I2C1 Display Overlay

This overlay enables the BIGTREETECH CB2 DSI1 display path while moving the Raspberry Pi 7-inch display control MCU and FT5406-compatible touch controller from the stock CB2 I2C2 pins to I2C1.

Use this for carrier boards where the display I2C lines are:

- SCL: CM pin 35, `GPIO0_B3`
- SDA: CM pin 26, `GPIO0_B4`

The overlay was tested on the CB2 Debian 12 kernel `6.1.115-btt-rk35xx`.

Expected I2C devices after boot:

- `1-0045`: display MCU/backlight
- `1-0038`: FT5406-compatible touch controller

## Files

- `cb2-dsi-i2c1-rpi7.dts`: editable device tree overlay source.
- `cb2-dsi-i2c1-rpi7.dtbo`: compiled overlay to install on the CB2.
- `install.sh`: installs the overlay and updates `/boot/armbianEnv.txt`.

## Install

Copy this folder to the CB2, then run:

```bash
cd cb2-dsi-i2c1-overlay
sudo ./install.sh
sudo reboot
```

The installer:

- Creates `/boot/overlay-user/`.
- Copies `cb2-dsi-i2c1-rpi7.dtbo` into it.
- Backs up `/boot/armbianEnv.txt`.
- Clears `overlays=` so the stock `dsi` overlay does not also enable the wrong I2C2 nodes.
- Sets `user_overlays=cb2-dsi-i2c1-rpi7`.

The relevant `/boot/armbianEnv.txt` lines should look like:

```ini
overlays=
user_overlays=cb2-dsi-i2c1-rpi7
```

## Build

If you edit the source, rebuild the compiled overlay with:

```bash
dtc -@ -I dts -O dtb -o cb2-dsi-i2c1-rpi7.dtbo cb2-dsi-i2c1-rpi7.dts
```

Install `device-tree-compiler` first if `dtc` is missing.

## Verify

After reboot:

```bash
ls /sys/class/drm
dmesg | grep -Ei 'dsi|panel|i2c|touch|raspberry'
```

You should see a DSI connector such as `card0-DSI-1`, and the old failing probes on I2C2 (`2-0045`, `2-0038`) should be gone.

Touch should appear as an input device:

```bash
grep -E 'Name=|Handlers=' /proc/bus/input/devices
```

The working setup reports a device similar to `1-0038 generic ft5x06`.

## Rollback

If the board does not boot/display correctly, restore the backup from SSH or serial:

```bash
sudo cp /boot/armbianEnv.txt.pre-cb2-dsi-i2c1-rpi7 /boot/armbianEnv.txt
sudo reboot
```
