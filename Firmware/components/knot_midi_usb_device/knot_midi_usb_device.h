#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * USB-MIDI *device* side (KNOT_USB_MODE_INTERFACE).
 *
 * Knot links grid_common's USB device stack (grid_usb*.c) unconditionally, and
 * that stack owns the TinyUSB descriptor callbacks (a "Grid" CDC+MIDI+HID
 * composite). So instead of a second, conflicting TinyUSB device we drive
 * grid_common's stack:
 *
 *   PC --(USB-MIDI OUT)--> tud_midi_rx_cb -> grid_usb_state.midi.{voice,sysex,rtm}_rx
 *          -> (drained here) -> usb_midi_to_uart() -> trsout queue -> TRS OUT
 *   TRS IN -> knot_midi_uart_rx_task -> usbout queue
 *          -> grid_usb_midi_tx_push() -> tud_midi -> PC (USB-MIDI IN)
 *
 * The USB PHY is put in device/full-speed mode and grid_usb_init() is called
 * with Knot's own VID/PID. grid_sys rx-mode flags are set so the incoming
 * packets are buffered (not dropped) but NOT converted to Grid protocol.
 */

// Call after knot_usb_mode_apply_mux(KNOT_USB_MODE_INTERFACE) and after
// knot_midi_uart_init(). Must NOT be called together with the usb_host stack.
void knot_midi_usb_device_start(void);

// true once the USB host (PC) has configured the interface.
uint8_t knot_midi_usb_device_is_mounted(void);

#ifdef __cplusplus
}
#endif
