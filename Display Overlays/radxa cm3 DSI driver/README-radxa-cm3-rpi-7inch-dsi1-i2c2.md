# Radxa CM3 Raspberry Pi 7-inch DSI Overlay

This folder contains patched Radxa CM3 overlays for a Raspberry Pi 7-inch-touch-compatible DSI panel on a CM5-wired carrier board.

The shipped CM3 `disp1` overlay enables `dsi1`, but places the panel control/touch devices on `i2c0`. These patched overlays move the control/touch bus to `i2c2` using the CM3 `i2c2m1_xfer` pinmux.

Recommended overlay for the DSI panel:

```text
radxa-cm3-rpi-cm4-7inch-touchscreen-disp1-i2c2.dtbo
```

Experimental HDMI-enabled overlay:

```text
radxa-cm3-rpi-cm4-7inch-touchscreen-disp1-i2c2-hdmi.dtbo
```

The HDMI-enabled overlay leaves HDMI connected at the kernel/DRM level, with HDMI routed via `vp0` and DSI routed via `vp1`. On the tested Radxa CM3 image, KDE/X11 reports two extended displays, but both physical panels show the same scanout content. Treat this overlay as experimental or mirrored-output only, not a confirmed independent dual-display setup.

## Install

Copy the overlay to the Radxa:

```sh
scp radxa-cm3-rpi-cm4-7inch-touchscreen-disp1-i2c2.dtbo radxa@192.168.1.129:/tmp/
```

Install it into `/boot/dtbo`:

```sh
ssh radxa@192.168.1.129
sudo install -o root -g root -m 0644 /tmp/radxa-cm3-rpi-cm4-7inch-touchscreen-disp1-i2c2.dtbo /boot/dtbo/
```

Back up the boot config:

```sh
stamp=$(date +%Y%m%d-%H%M%S)
sudo cp -a /etc/default/u-boot "/etc/default/u-boot.bak.$stamp"
sudo cp -a /boot/extlinux/extlinux.conf "/boot/extlinux/extlinux.conf.bak.$stamp"
```

Add this to `/etc/default/u-boot`:

```sh
U_BOOT_FDT_OVERLAYS="radxa-cm3-rpi-cm4-7inch-touchscreen-disp1-i2c2.dtbo"
```

Regenerate the bootloader config and reboot:

```sh
sudo u-boot-update
sudo reboot
```

## Verify

After reboot, these checks should show the panel bound on `dsi1` and the touch/control devices bound on `i2c2`:

```sh
tr '\0' '\n' </proc/device-tree/dsi@fe070000/status
tr '\0' '\n' </proc/device-tree/i2c@fe5b0000/status
ls /sys/bus/i2c/devices/2-0030 /sys/bus/i2c/devices/2-0038 /sys/bus/i2c/devices/2-0045
cat /sys/class/drm/card0-DSI-1/status
cat /sys/class/drm/card0-DSI-1/modes
grep -A8 'Name="fts_ts"' /proc/bus/input/devices
```

Expected results:

```text
dsi1 status: okay
i2c2 status: okay
DRM connector: card0-DSI-1 connected
DSI mode: 800x480
touch input: fts_ts on i2c-2 address 0x38
panel MCU: rockpi_mcu on i2c-2 address 0x45
optional touch/client node: chipone_icn8505 on i2c-2 address 0x30
```
