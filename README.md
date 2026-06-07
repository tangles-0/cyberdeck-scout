## JetDeck SCOUT - https://jetdeck.io

# Scout Cyberdeck

Repository contents:

- PCB design files
  - `/pcb-deck` current working PCB design file
  - `/pcb-deck-r1` prototype 1 production
  - `/pcb-deck-r1-fixed` same as prototype 1 with issues addressed
- USB PD sink EEPROM flash
  - `/charger-firmware-ti` EEPROM images for the TPS25751
- Gamepad / mouse firmware
  - `/gamepad` RP2040 mbed sketch for the gamepad. reports HID Mouse and HID Gamepad to the host
- Keyboard firmware
  - `/qmk/` drop this into your `qmk_firmware` folder and run `qmk flash -kb rii/518bt -km default` to flash the keyboard
- Bring-up / test / QA jig PCB files (TBA) and firmware
  - `/esp32-bringup-jig-and-monitor` 
- 3D casing design files
  - `/3D-casing-designs`