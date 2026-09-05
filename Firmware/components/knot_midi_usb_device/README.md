# USB-C MIDI interface mode

Adds a second operating mode to Knot: instead of being a USB **host** for a
controller on the USB-A port, Knot becomes a class-compliant USB-**MIDI device**
on the USB-C port, so a PC can talk to TRS MIDI gear bidirectionally.

```
HOST mode (legacy, default)         INTERFACE mode (new)
  USB-A controller                     PC (USB-C)
        |  usb_host + class driver           |  TinyUSB MIDI device
        v                                    v
   usbout / trsout queues  <---- shared ---->  usbout / trsout queues
        |                                    |
        v                                    v
   TRS IN / OUT (UART @ 31250)          TRS IN / OUT (UART @ 31250)
```

The ESP32-S3 has **one** USB-OTG controller, so the two modes are mutually
exclusive and chosen at boot.

## Selecting the mode

* Stored in LittleFS as `usbmode.cfg` (`'0'` = host, `'1'` = interface), same
  mechanism as `midithrough.cfg`.
* **Long-press the mode button (≥1.5 s)** during normal operation: all 3 LEDs go
  solid **red** to confirm the press; on release the file is written and the
  device **reboots automatically** into the other mode (no manual power-cycle).
* Short-press keeps its old meaning (toggle MIDI-thru).

### LED scheme (both USB modes)

| LED | meaning |
|-----|---------|
| index 2 steady | MIDI-thru: **green** = off, **blue** = on |
| all 3, **white** pulse | host mode, waiting for a USB device on USB-A |
| all 3, **yellow** pulse | interface mode, waiting for the PC to enumerate on USB-C |
| all 3, solid red | long-press registered → about to switch USB mode + reboot |
| index 0 / 1 white blip | TRS RX / TX activity |

## Hardware notes / TODO before shipping

1. **USB mux polarity** — `knot_usb_mode.h` `KNOT_USB_SELECT_*_LEVEL_INTERFACE`
   are guesses. Confirm on hardware which GPIO11/GPIO12 levels route the native
   USB (GPIO19/20) to CONN_C. Host mode uses `1,1` today.
2. **USB-C as UFP** — CC1/CC2 carry 5k1 pulldowns on the schematic, so the port
   already presents as a device/sink. Verify a PC enumerates it.
3. **Power** — in interface mode Knot is bus-powered from the PC (`self_powered
   = false`). Confirm the PMIC path is happy at 5 V from USB-C with no DC-in.
4. **PID** — `KNOT_USB_PID` 0x4009 is a placeholder; register one with Espressif.
5. **`sdkconfig`** — apply `sdkconfig.defaults` (`idf.py reconfigure`), which
   enables `CONFIG_TINYUSB_MIDI_ENABLED` and drops the USB-Serial-JTAG secondary
   console (shares the OTG pads).
6. `TRS_RX_AB_SELECT` (GPIO16) is now driven together with `TRS_TX_AB_SELECT`
   from the A/B switch (`knot_midi_uart_set_miditrsab_state`). Confirm both
   analog switches share the same polarity on hardware.
