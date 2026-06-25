## JetDeck SCOUT - https://jetdeck.io

# Scout Cyberdeck

Repository contents:

- PCB design files
  - `/PCB/cyberdeck` current working PCB design file
  - `/PCB/cyberdeck-r1|r2 etc` snapshot of files sent for production
- USB PD sink EEPROM flash
  - `/Firmware/TPS25751` EEPROM images for the TPS25751 PD front-end
- Gamepad / mouse firmware
  - `/Firmware/gamepad-touch` RP2040 mbed sketch for the gamepad. reports HID Mouse, HID Gamepad, HID Touch Interface, and HID Battery to the host
- Keyboard firmware
  - `/Firmware/qmk/` drop this into your `qmk_firmware` folder and run `qmk flash -kb rii/518bt -km default` to flash the keyboard
- Bring-up / test / QA jig (PCB files TBA) and firmware
  - `/Firmware/esp32-bringup` 
- 3D casing design files
  - `/Casing CAD`
- Datasheets
  - `/Datasheets` for the major components used
- Display Overlays
  - `/Display Overlays` overlays for the DSI display used in testing
- Software
  - `/Software` will be used if any custom software is written