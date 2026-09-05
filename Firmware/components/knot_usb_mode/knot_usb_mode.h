#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * KNOT USB operating mode.
 *
 * The ESP32-S3 has a single USB-OTG controller, so it can act EITHER as a USB
 * host (for a class-compliant controller plugged into the USB-A port) OR as a
 * USB device (a class-compliant USB-MIDI interface exposed on the USB-C port),
 * never both at the same time.
 *
 * The active mode is chosen at boot from a small config file in LittleFS
 * ("usbmode.cfg", same mechanism as "midithrough.cfg") and toggled at runtime
 * by a long-press of the mode button, which rewrites the file and reboots.
 */
typedef enum {
  KNOT_USB_MODE_HOST = 0,     // USB-A host  <-> TRS   (legacy behaviour, default)
  KNOT_USB_MODE_INTERFACE = 1, // USB-C device <-> TRS  (PC MIDI interface)
} knot_usb_mode_t;

#define KNOT_USB_MODE_CFG_FILE "usbmode.cfg"

// GPIOs that steer the external analog USB mux (BL1551B U2/U3 = native path).
#define KNOT_USB_SELECT_NATIVE_PIN 11
#define KNOT_USB_SELECT_SOFT_PIN 12

// TODO(hw): confirm the mux polarity on real hardware / with Intech.
//   Host mode today drives both selects HIGH and talks to CONN_A (USB-A).
//   These are the levels we believe route the native USB to CONN_C (USB-C).
#define KNOT_USB_SELECT_NATIVE_LEVEL_HOST 1
#define KNOT_USB_SELECT_SOFT_LEVEL_HOST 1
#define KNOT_USB_SELECT_NATIVE_LEVEL_INTERFACE 0
#define KNOT_USB_SELECT_SOFT_LEVEL_INTERFACE 1

// Long-press threshold (ms) on the mode button that triggers a USB-mode switch.
// Anything shorter keeps its existing meaning (MIDI-thru toggle in host mode).
#define KNOT_USB_MODE_LONGPRESS_MS 1500

/* Read the persisted mode from LittleFS. Returns KNOT_USB_MODE_HOST if the file
 * is missing or malformed. */
knot_usb_mode_t knot_usb_mode_read(void);

/* Persist a new mode to LittleFS. Returns 0 on success. */
int knot_usb_mode_write(knot_usb_mode_t mode);

/* Configure GPIO11/GPIO12 as outputs and drive the analog USB mux for `mode`.
 * Call once, early in app_main, before bringing up any USB stack. */
void knot_usb_mode_apply_mux(knot_usb_mode_t mode);

/* Convenience: flip the stored mode and reboot into it. Never returns. */
void knot_usb_mode_toggle_and_reboot(void);

#ifdef __cplusplus
}
#endif
