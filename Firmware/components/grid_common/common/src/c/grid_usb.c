/*
 * grid_usb.c
 *
 * Created: 7/6/2020 12:07:54 PM
 *  Author: suku
 *
 * USB functionality has been refactored into:
 *   grid_usb_midi.c  - MIDI buffer/TX/RX/SysEx/RTM
 *   grid_usb_hid.c   - HID keyboard model + gamepad + enable/disable
 */

#include "tusb.h"

#include "grid_usb.h"

#include <string.h>

#include "grid_protocol.h"
#include "grid_swsr.h"

struct grid_usb_model grid_usb_state = {0};

bool grid_usb_connected(void) { return tud_mounted(); }

void grid_usb_task(void) { tud_task_ext(0, false); }

// NOTE (Knot fork): grid_usb.c owns the TinyUSB descriptor callbacks
// unconditionally (grid_decode.c/grid_port.c always pull this file into the
// link), so a device using this stack can only ever advertise ONE descriptor
// set. Knot doesn't want Grid's CDC+HID composite (a dormant serial port and
// HID device confusing the host for a MIDI interface), so this file has been
// patched, here only, to advertise a plain single-interface MIDI device
// instead. grid_usb_hid.c / grid_usb_acm.c still compile and their TinyUSB
// class drivers are still linked in - they are simply never opened by the
// host since no CDC/HID interface exists in the config descriptor below, so
// tud_mount_cb()/tud_umount_cb() no longer call their on_connect/on_disconnect.
void tud_mount_cb(void) { grid_usb_midi_on_connect(&grid_usb_state.midi); }

void tud_umount_cb(void) { grid_usb_midi_on_disconnect(&grid_usb_state.midi); }

static tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00, // device class comes from the (single) interface
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0,
    .idProduct = 0,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x00,
    .bNumConfigurations = 0x01,
};

static const uint16_t s_lang_id[] = {0x0409};

static const void* s_str_table[] = {
    s_lang_id,       // 0: language ID
    "Intech Studio", // 1: Manufacturer
    "Knot",          // 2: Product
    NULL,            // 3: Serial (set at init)
    "Knot MIDI",     // 4: MIDI interface
};

#define STR_TABLE_COUNT ((uint8_t)(sizeof(s_str_table) / sizeof(s_str_table[0])))

// Knot-only: plain MIDI device, 2 interfaces (Audio Control + MIDIStreaming),
// no CDC/HID. Endpoint 1 is free to hardcode since it is the only interface
// the host ever sees.
#define KNOT_USB_MIDI_ITF_NUM 0
#define KNOT_USB_MIDI_STR_IDX 4
#define KNOT_USB_MIDI_EP_OUT 0x01
#define KNOT_USB_MIDI_EP_IN 0x81
#define KNOT_USB_ITF_COUNT 2

#define GRID_USB_CFG_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t s_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, KNOT_USB_ITF_COUNT, 0, GRID_USB_CFG_DESC_TOTAL_LEN, 0, 500),
    TUD_MIDI_DESCRIPTOR(KNOT_USB_MIDI_ITF_NUM, KNOT_USB_MIDI_STR_IDX, KNOT_USB_MIDI_EP_OUT, KNOT_USB_MIDI_EP_IN, 64),
};

static uint8_t const* grid_usb_config_desc(void) { return s_cfg_desc; }

#define GRID_USB_STR_DESC_MAX_LEN 33

static uint16_t const* grid_usb_string_desc(const void** str_table, uint8_t count, uint8_t index) {

  static uint16_t desc_str[GRID_USB_STR_DESC_MAX_LEN];
  uint8_t chr_count;

  if (index == 0) {

    memcpy(&desc_str[1], str_table[0], 2);
    chr_count = 1;
  } else if (index < count && str_table[index] != NULL) {

    const char* str = (const char*)str_table[index];
    chr_count = (uint8_t)strnlen(str, GRID_USB_STR_DESC_MAX_LEN - 1);

    for (uint8_t i = 0; i < chr_count; i++) {
      desc_str[1 + i] = (uint16_t)str[i];
    }
  } else {

    return NULL;
  }

  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * chr_count + 2u));

  return desc_str;
}

void grid_usb_init(uint16_t vid, uint16_t pid, const char* serial) {
  s_device_desc.idVendor = vid;
  s_device_desc.idProduct = pid;
  s_str_table[3] = serial;
  s_device_desc.iSerialNumber = (serial != NULL) ? 0x03 : 0x00;
  grid_usb_acm_init(&grid_usb_state.acm, GRID_PARAMETER_SPI_TRANSACTION_length * 2);
  grid_usb_midi_init(&grid_usb_state.midi, GRID_MIDI_TX_BUFFER_SIZE, GRID_MIDI_VOICE_RX_BUFFER_SIZE, GRID_MIDI_SYSEX_BUFFER_SIZE, GRID_MIDI_RTM_BUFFER_SIZE);
  grid_usb_hid_init(&grid_usb_state.hid);
  tusb_init();
}

uint8_t const* tud_descriptor_device_cb(void) { return (uint8_t const*)&s_device_desc; }

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return grid_usb_config_desc();
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  return grid_usb_string_desc(s_str_table, STR_TABLE_COUNT, index);
}
