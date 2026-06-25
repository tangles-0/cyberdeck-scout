# 5-inch DSI ILI9881C Overlay

This directory contains the working overlay for the 5-inch 720x1280 DSI display on a Raspberry Pi CM5.
Sold as "Surenoo" brand on Aliexpress: https://www.aliexpress.com/item/1005006436051194.html
Listed as a WKS50HD002-WCT from supplier WKS Technology Co., LTD according to datasheet

## Files

- `ili9881c.dtbo` - compiled overlay to copy onto the Pi
- `ili9881c-overlay.dts` - source used to build the overlay

## Install

Copy the compiled overlay to:

```bash
sudo cp ili9881c.dtbo /boot/firmware/overlays/ili9881c.dtbo
```

Then enable it in `/boot/firmware/config.txt`:

```ini
[cm5]
dtoverlay=ili9881c
```

Keep other DSI panel overlays disabled at the same time, for example:

```ini
#dtoverlay=vc4-kms-dsi-7inch,dsi1
#dtoverlay=vc4-kms-dsi-ili9881-7inch
```

## Build

```bash
dtc -@ -I dts -O dtb -o ili9881c.dtbo ili9881c-overlay.dts
```

The known-good panel compatible is:

```dts
compatible = "crystalfontz,cfaf7201280a0_050tx";
```
