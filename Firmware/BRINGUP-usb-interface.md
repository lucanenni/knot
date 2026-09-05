# Bring-up checklist - USB-C MIDI interface mode

Branch: `feature/usb_to_trs_midi`. Build verified on ESP-IDF **v5.5** /
`esp32s3`.

## Status (2026-09-05, first board)

- [x] Host mode regression: white pulse, controller -> TRS OUT, thru toggle
      (green/blue), all confirmed on hardware
- [x] Long-press mode switch: red confirmation -> reboot into interface mode
- [x] USB-C enumeration: shows up on macOS as `303A:8123` "Intech Studio /
      Knot" - mux polarity guess worked, no auto-probe toggling observed/needed
- [x] TRS IN -> PC: confirmed working
- [x] PC -> TRS OUT: confirmed working (after a false negative caused by a
      macOS CoreMIDI class-compliant-driver quirk on the first enumeration of
      a session - a clean power-cycle with no button presses works
      immediately; not a firmware issue, MIDI-thru state does not gate the
      USB->TRS path in the code)

- [x] Bus-power-only boot: confirmed - the board runs off USB-C power alone
      throughout this bring-up session, no DC-in connected
- [x] Clean USB identity: patched vendored grid_common's grid_usb.c for a
      plain single-interface MIDI device. Confirmed on macOS: "USB Product
      Name" = "Knot" (was "Grid"), no more stray CDC tty
      (`/dev/cu.usbmodem*`) appearing. MIDI re-verified working after the
      change.

Not yet tested: A/B switch on the IN jack (no Type-B TRS device available -
blocked on hardware access, not a code concern), UF2/factory-reset button
gestures after this firmware (low risk, untouched code paths).

## 0. Build + flash

```sh
cd Firmware
idf.py set-target esp32s3        # first time only
idf.py build
python3 ./tools/uf2conv.py -f ESP32S3 ./build/midi_host_fw.bin -b 0x0 -c -o ./knot.uf2
```

Flash one of:
- **UF2**: hold the mode button while plugging USB-C -> `KNOT-S3` mass storage
  -> copy `knot.uf2`. Reboots itself.
- **Serial/JTAG**: `idf.py -p <PORT> flash monitor`

Keep a serial monitor attached for the logs below (`idf.py monitor`, or the
USB-Serial-JTAG console - note it is disabled in interface mode, see step 4).

## 1. Host mode regression (default, no config file)

Expected: identical to today.
- 3 LEDs pulse **white** = waiting for a USB-A device
- plug a class-compliant controller (Grid, MPK Mini, ...) into USB-A
- controller <-> TRS OUT / TRS IN works
- LED 2 steady: **green** = thru off, **blue** = thru on; short-press mode
  button toggles it
- **A/B switch now affects both jacks**: verify with a Type-A and a Type-B
  TRS device on IN and OUT (previously only OUT was switched)

Log markers: `===== USB MODE: HOST (USB-A) =====`, `KNOT_MIDI_USB` enumeration.

## 2. Switch to interface mode

- Long-press the mode button (>=1.5 s): all 3 LEDs go **solid red**
- Release: `usbmode.cfg` is written, device reboots
- After reboot: `===== USB MODE: INTERFACE (USB-C device) =====`

## 3. USB mux polarity (the main unknown)

`knot_usb_mode.h` `KNOT_USB_SELECT_*_LEVEL_INTERFACE` is a guess. The firmware
**auto-probes**: while unmounted it toggles `USB_SELECT_NATIVE` (GPIO11) every
2 s. Watch the log:

```
KNOT_USB_DEV: not mounted - trying USB_SELECT_NATIVE=1
KNOT_USB_DEV: not mounted - trying USB_SELECT_NATIVE=0
KNOT_USB_DEV: host enumerated us (USB_SELECT_NATIVE=0)   <- note this value
```

Once the winning value is known, set it as `KNOT_USB_SELECT_NATIVE_LEVEL_INTERFACE`
in `knot_usb_mode.h` and drop the auto-probe (or leave it as a safety net).
Also check whether `USB_SELECT_SOFT` (GPIO12) needs a matching value - try the
schematic / measure with the UF2 bootloader active (it routes native USB to
CONN_C).

## 4. Enumeration + MIDI

- PC should see a MIDI device: **"Intech Studio / Knot"**, VID/PID
  `303A:8123`, a single "Knot MIDI" port - no CDC/HID interfaces (grid_common's
  grid_usb.c is patched locally to advertise a plain MIDI-only descriptor).
- 3 LEDs pulse **yellow** until the PC enumerates, then stop.
- **PC -> TRS OUT**: send notes/CC from a DAW -> should come out TRS OUT.
  LED 0 blips.
- **TRS IN -> PC**: play a keyboard into TRS IN -> DAW receives. LED 1 blips.
- Test running status, SysEx (dump), MIDI clock / real-time.

Note: the USB-Serial-JTAG console shares the OTG pads and is off in this mode
(`CONFIG_ESP_CONSOLE_SECONDARY_NONE`). Use UART0 for logs, or a JTAG probe.

## 5. Power

- In interface mode Knot is bus-powered from the PC (`grid_usb_init`,
  self_powered = false). Confirm it boots and runs on USB-C alone (no DC-in).
- Confirm DC-in still works if present.

## 6. Button matrix still sane

| gesture | effect |
|---|---|
| hold at plug-in (USB-C) | TinyUF2 bootloader (`KNOT-S3` MSC) |
| hold during boot | factory reset (LEDs blink yellow 4 Hz) |
| short press, running | toggle MIDI-thru |
| long press >=1.5 s, running | switch USB mode + reboot |

## Known follow-ups

- RX A/B polarity: assumed equal to TX (`knot_midi_uart.c` TODO)
- dedicated VID/PID
- `uf2` factory partition (256 KB) < image (578 KB) - app runs from `ota_0`;
  confirm the UF2 flow targets OTA not the factory slot
- runtime robustness: grid_sys rx-mode gating + swsr draining under load;
  grid_transport is not initialised (we never call the *_process functions)
