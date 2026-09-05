#include "knot_midi_usb_device.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "grid_led.h"
#include "grid_swsr.h"
#include "grid_sys.h"
#include "grid_usb.h"

#include "knot_midi_queue.h"
#include "knot_midi_translator.h"
#include "knot_midi_uart.h"
#include "knot_usb_mode.h"

static const char* TAG = "KNOT_USB_DEV";

// Knot's own USB identity (device / interface mode).
#define KNOT_USB_VID 0x303A // Espressif shared VID
#define KNOT_USB_PID 0x8123 // TODO: register a dedicated Knot MIDI PID

// Bound the work done per task iteration so the USB loop keeps servicing
// tud_task() even under a sustained MIDI flood.
#define KNOT_USB_DEV_MAX_TX_PER_ITER 32
#define KNOT_USB_DEV_MAX_RX_PER_ITER 64

// Mux-polarity auto-probe (the level that routes native USB to CONN_C is not
// yet verified on hardware): while unmounted, alternate GPIO11 between the two
// candidates until the host enumerates us.
#define KNOT_USB_DEV_PROBE_GRACE_MS 5000
#define KNOT_USB_DEV_PROBE_INTERVAL_MS 3000

static char s_serial[13] = "000000000000";
static int s_mux_native_level = KNOT_USB_SELECT_NATIVE_LEVEL_INTERFACE;

/* ------------------------------------------------------------------------- */

static void knot_usb_dev_waiting_led(bool waiting) {
  if (waiting) {
    grid_alert_all_set(&grid_led_state, 120, 80, 0, -1); // yellow pulse = waiting for host
  } else {
    grid_alert_all_set(&grid_led_state, 0, 0, 0, 0);
  }
}

uint8_t knot_midi_usb_device_is_mounted(void) { return grid_usb_connected() ? 1 : 0; }

static void knot_usb_phy_device_init(void) {
  usb_phy_handle_t phy_hdl;
  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .target = USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = USB_PHY_SPEED_FULL,
  };
  ESP_ERROR_CHECK(usb_new_phy(&phy_conf, &phy_hdl));
}

// TRS IN (usbout queue) -> grid_usb MIDI TX ring.
static void knot_usb_dev_pump_tx(void) {
  struct usb_midi_event_packet ev = {0};
  for (int i = 0; i < KNOT_USB_DEV_MAX_TX_PER_ITER; ++i) {
    if (knot_midi_queue_usbout_pop(&ev) != 0) {
      break;
    }
    struct grid_midi_event_desc d = {ev.byte0, ev.byte1, ev.byte2, ev.byte3};
    grid_usb_midi_tx_push(&grid_usb_state.midi, d);
    grid_alert_one_set(&grid_led_state, 1, 200, 200, 200, 30); // TX blip
  }
  if (grid_usb_midi_tx_available(&grid_usb_state.midi)) {
    grid_usb_midi_tx_flush(&grid_usb_state.midi);
  }
}

// grid_usb MIDI RX rings -> TRS OUT (trsout queue).
static void knot_usb_dev_pump_rx(void) {
  struct grid_usb_midi_model* midi = &grid_usb_state.midi;
  int budget = KNOT_USB_DEV_MAX_RX_PER_ITER;

  // Channel-voice / system-common: 4-byte USB-MIDI event packets.
  while (budget-- > 0 && grid_swsr_readable(&midi->voice_rx, sizeof(struct grid_midi_event_desc))) {
    struct grid_midi_event_desc d;
    grid_swsr_read(&midi->voice_rx, &d, sizeof(d));
    struct usb_midi_event_packet u = {d.byte0, d.byte1, d.byte2, d.byte3};
    struct uart_midi_event_packet ue = usb_midi_to_uart(u);
    if (ue.length) {
      knot_midi_queue_trsout_push(ue);
      grid_alert_one_set(&grid_led_state, 0, 200, 200, 200, 30); // RX blip
    }
  }

  // Real-time: single status bytes, straight through.
  while (budget-- > 0 && grid_swsr_readable(&midi->rtm_rx, 1)) {
    uint8_t b;
    grid_swsr_read(&midi->rtm_rx, &b, 1);
    knot_midi_queue_trsout_push((struct uart_midi_event_packet){.length = 1, .byte1 = b});
  }

  // SysEx: raw byte stream (0xF0 .. 0xF7), forwarded byte-by-byte.
  while (budget-- > 0 && grid_swsr_readable(&midi->sysex_rx, 1)) {
    uint8_t b;
    grid_swsr_read(&midi->sysex_rx, &b, 1);
    knot_midi_queue_trsout_push((struct uart_midi_event_packet){.length = 1, .byte1 = b});
  }
}

// Single task: ALL TinyUSB / grid_usb calls happen here (TinyUSB is built with
// CFG_TUSB_OS OPT_OS_NONE and is not thread-safe).
static void knot_usb_dev_task(void* arg) {
  (void)arg;
  uint8_t last_mounted = 0;
  TickType_t started = xTaskGetTickCount();
  TickType_t last_probe = started;

  for (;;) {
    knot_usb_dev_pump_tx();

    grid_usb_task();                          // tud_task()
    grid_usb_midi_rx_poll(&grid_usb_state.midi); // drain tud FIFO -> grid rx rings

    knot_usb_dev_pump_rx();

    uint8_t mounted = grid_usb_connected();
    if (mounted && !last_mounted) {
      knot_usb_dev_waiting_led(false);
      ESP_LOGI(TAG, "host enumerated us (USB_SELECT_NATIVE=%d)", s_mux_native_level);
    } else if (!mounted && last_mounted) {
      knot_usb_dev_waiting_led(true);
    }
    last_mounted = mounted;

    if (!mounted && (xTaskGetTickCount() - started) > pdMS_TO_TICKS(KNOT_USB_DEV_PROBE_GRACE_MS) &&
        (xTaskGetTickCount() - last_probe) > pdMS_TO_TICKS(KNOT_USB_DEV_PROBE_INTERVAL_MS)) {
      s_mux_native_level = !s_mux_native_level;
      gpio_set_level(KNOT_USB_SELECT_NATIVE_PIN, s_mux_native_level);
      ESP_LOGW(TAG, "not mounted - trying USB_SELECT_NATIVE=%d", s_mux_native_level);
      last_probe = xTaskGetTickCount();
    }

    vTaskDelay(1); // ~1 tick; keeps the loop cooperative
  }
}

void knot_midi_usb_device_start(void) {

  // Buffer incoming USB-MIDI in grid_common's rx rings instead of dropping it,
  // but we drain them ourselves (no Grid-protocol conversion / transport, so
  // grid_sys / grid_transport do not need to be initialised).
  grid_sys_set_rx_mode(&grid_sys_state, GRID_RX_TYPE_MIDIVOICE, GRID_RX_MODE_FORWARD_FROM_USB);
  grid_sys_set_rx_mode(&grid_sys_state, GRID_RX_TYPE_MIDISYSEX, GRID_RX_MODE_FORWARD_FROM_USB);
  grid_sys_set_rx_mode(&grid_sys_state, GRID_RX_TYPE_MIDIRTM, GRID_RX_MODE_FORWARD_FROM_USB);

  uint8_t mac[6] = {0};
  if (esp_efuse_mac_get_default(mac) == ESP_OK) {
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  ESP_LOGI(TAG, "USB PHY -> device, grid_usb_init(%04X:%04X, %s)", KNOT_USB_VID, KNOT_USB_PID, s_serial);
  knot_usb_phy_device_init();
  grid_usb_init(KNOT_USB_VID, KNOT_USB_PID, s_serial);

  knot_usb_dev_waiting_led(true);

  xTaskCreatePinnedToCore(knot_usb_dev_task, "usbdev", 4096, NULL, 3, NULL, 1);

  ESP_LOGI(TAG, "USB-MIDI device up");
}
