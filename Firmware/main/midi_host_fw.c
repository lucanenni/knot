#ifdef TEST_CDC_SERIAL

/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"

#define EXAMPLE_USB_HOST_PRIORITY (20)
#define EXAMPLE_USB_DEVICE_VID (0x303A)
#define EXAMPLE_USB_DEVICE_PID (0x8123)      // 0x303A:0x4001 (TinyUSB CDC device)
#define EXAMPLE_USB_DEVICE_DUAL_PID (0x4002) // 0x303A:0x4002 (TinyUSB Dual CDC device)
#define EXAMPLE_TX_STRING ("CDC test string!")
#define EXAMPLE_TX_TIMEOUT_MS (1000)

static const char* TAG = "USB-CDC";
static SemaphoreHandle_t device_disconnected_sem;

/**
 * @brief Data received callback
 *
 * @param[in] data     Pointer to received data
 * @param[in] data_len Length of received data in bytes
 * @param[in] arg      Argument we passed to the device open function
 * @return
 *   true:  We have processed the received data
 *   false: We expect more data
 */
static bool handle_rx(const uint8_t* data, size_t data_len, void* arg) {
  ESP_LOGI(TAG, "Data received");
  ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_INFO);
  return true;
}

/**
 * @brief Device event callback
 *
 * Apart from handling device disconnection it doesn't do anything useful
 *
 * @param[in] event    Device event type and data
 * @param[in] user_ctx Argument we passed to the device open function
 */
static void handle_event(const cdc_acm_host_dev_event_data_t* event, void* user_ctx) {
  switch (event->type) {
  case CDC_ACM_HOST_ERROR:
    ESP_LOGE(TAG, "CDC-ACM error has occurred, err_no = %i", event->data.error);
    break;
  case CDC_ACM_HOST_DEVICE_DISCONNECTED:
    ESP_LOGI(TAG, "Device suddenly disconnected");
    ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
    xSemaphoreGive(device_disconnected_sem);
    break;
  case CDC_ACM_HOST_SERIAL_STATE:
    ESP_LOGI(TAG, "Serial state notif 0x%04X", event->data.serial_state.val);
    break;
  case CDC_ACM_HOST_NETWORK_CONNECTION:
  default:
    ESP_LOGW(TAG, "Unsupported CDC event: %i", event->type);
    break;
  }
}

/**
 * @brief USB Host library handling task
 *
 * @param arg Unused
 */
static void usb_lib_task(void* arg) {
  while (1) {
    // Start handling system events
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_ERROR_CHECK(usb_host_device_free_all());
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      ESP_LOGI(TAG, "USB: All devices freed");
      // Continue handling USB events to allow device reconnection
    }
  }
}

/**
 * @brief Main application
 *
 * Here we open a USB CDC device and send some data to it
 */

#include "driver/gpio.h"

void app_main(void) {

#define USB_NATIVE_SELECT_PIN 11
#define USB_SOFT_SELECT_PIN 12

  gpio_set_direction(USB_NATIVE_SELECT_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(USB_SOFT_SELECT_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(USB_NATIVE_SELECT_PIN, 1);
  gpio_set_level(USB_SOFT_SELECT_PIN, 1);

#define PMIC_EN_PIN 48

  gpio_set_direction(PMIC_EN_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(PMIC_EN_PIN, 1);

  device_disconnected_sem = xSemaphoreCreateBinary();
  assert(device_disconnected_sem);

  // Install USB Host driver. Should only be called once in entire application
  ESP_LOGI(TAG, "Installing USB Host");
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  ESP_ERROR_CHECK(usb_host_install(&host_config));

  // Create a task that will handle USB library events
  BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), EXAMPLE_USB_HOST_PRIORITY, NULL);
  assert(task_created == pdTRUE);

  ESP_LOGI(TAG, "Installing CDC-ACM driver");
  ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

  const cdc_acm_host_device_config_t dev_config = {.connection_timeout_ms = 1000, .out_buffer_size = 512, .in_buffer_size = 512, .user_arg = NULL, .event_cb = handle_event, .data_cb = handle_rx};

  while (true) {
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    // Open USB device from tusb_serial_device example example. Either single or dual port configuration.
    ESP_LOGI(TAG, "Opening CDC ACM device 0x%04X:0x%04X...", EXAMPLE_USB_DEVICE_VID, EXAMPLE_USB_DEVICE_PID);
    esp_err_t err = cdc_acm_host_open(EXAMPLE_USB_DEVICE_VID, EXAMPLE_USB_DEVICE_PID, 0, &dev_config, &cdc_dev);
    if (ESP_OK != err) {
      ESP_LOGI(TAG, "Opening CDC ACM device 0x%04X:0x%04X...", EXAMPLE_USB_DEVICE_VID, EXAMPLE_USB_DEVICE_DUAL_PID);
      err = cdc_acm_host_open(EXAMPLE_USB_DEVICE_VID, EXAMPLE_USB_DEVICE_DUAL_PID, 0, &dev_config, &cdc_dev);
      if (ESP_OK != err) {
        ESP_LOGI(TAG, "Failed to open device");
        continue;
      }
    }
    cdc_acm_host_desc_print(cdc_dev);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Test sending and receiving: responses are handled in handle_rx callback
    ESP_ERROR_CHECK(cdc_acm_host_data_tx_blocking(cdc_dev, (const uint8_t*)EXAMPLE_TX_STRING, strlen(EXAMPLE_TX_STRING), EXAMPLE_TX_TIMEOUT_MS));
    vTaskDelay(pdMS_TO_TICKS(100));

    // Test Line Coding commands: Get current line coding, change it 9600 7N1 and read again
    ESP_LOGI(TAG, "Setting up line coding");

    cdc_acm_line_coding_t line_coding;
    ESP_ERROR_CHECK(cdc_acm_host_line_coding_get(cdc_dev, &line_coding));
    ESP_LOGI(TAG, "Line Get: Rate: %" PRIu32 ", Stop bits: %" PRIu8 ", Parity: %" PRIu8 ", Databits: %" PRIu8 "", line_coding.dwDTERate, line_coding.bCharFormat, line_coding.bParityType,
             line_coding.bDataBits);

    line_coding.dwDTERate = 9600;
    line_coding.bDataBits = 7;
    line_coding.bParityType = 1;
    line_coding.bCharFormat = 1;
    ESP_ERROR_CHECK(cdc_acm_host_line_coding_set(cdc_dev, &line_coding));
    ESP_LOGI(TAG, "Line Set: Rate: %" PRIu32 ", Stop bits: %" PRIu8 ", Parity: %" PRIu8 ", Databits: %" PRIu8 "", line_coding.dwDTERate, line_coding.bCharFormat, line_coding.bParityType,
             line_coding.bDataBits);

    ESP_ERROR_CHECK(cdc_acm_host_line_coding_get(cdc_dev, &line_coding));
    ESP_LOGI(TAG, "Line Get: Rate: %" PRIu32 ", Stop bits: %" PRIu8 ", Parity: %" PRIu8 ", Databits: %" PRIu8 "", line_coding.dwDTERate, line_coding.bCharFormat, line_coding.bParityType,
             line_coding.bDataBits);

    ESP_ERROR_CHECK(cdc_acm_host_set_control_line_state(cdc_dev, true, false));

    // We are done. Wait for device disconnection and start over
    ESP_LOGI(TAG, "Example finished successfully! You can reconnect the device to run again.");
    xSemaphoreTake(device_disconnected_sem, portMAX_DELAY);
  }
}

#else

#include "esp_intr_alloc.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

// must be last freertos relevant header to avoid #error
#include "esp_freertos_hooks.h"

#include <stdlib.h>
#include <string.h>

#include "knot_midi_queue.h"
#include "knot_midi_uart.h"
#include "knot_midi_usb.h"
#include "knot_midi_usb_device.h"
#include "knot_usb_mode.h"

#include "grid_littlefs.h"
#include "grid_platform.h"

#define SW_AB_PIN 35
#define SW_MODE_PIN 36

#include "driver/gpio.h"

#define DAEMON_TASK_PRIORITY 3
#define CLASS_TASK_PRIORITY 4
#define LED_TASK_PRIORITY 2

#define UART_RX_TASK_PRIORITY 0
#define UART_TX_TASK_PRIORITY 0

#define USB_RX_TASK_PRIORITY 0
#define USB_TX_TASK_PRIORITY 0

static const char* TAG = "DAEMON";

// USB operating mode, latched once at boot from usbmode.cfg (see knot_usb_mode).
static knot_usb_mode_t g_usb_mode = KNOT_USB_MODE_HOST;

// Paint LED index 2 (UI_A layer) to reflect the MIDI-thru state.
// Same green/blue convention in both USB modes.
static void knot_status_led_update(void) {
  if (knot_midi_uart_get_midithrough_state(&knot_midi_uart_state)) {
    grid_led_set_layer_color(&grid_led_state, 2, GRID_LED_LAYER_UI_A, 0, 0, 255); // blue = thru on
  } else {
    grid_led_set_layer_color(&grid_led_state, 2, GRID_LED_LAYER_UI_A, 0, 255, 0); // green = thru off
  }
}

void midi_config_file_update(uint8_t state) {

  ESP_LOGI(TAG, "midi_config_file_update, state: %d", state);

  int status = grid_platform_write_file_contents(state ? "1" : "0", "midithrough.cfg");
  if (status) {
    grid_platform_printf("grid_platform_write_file_contents returned %d\n", status);
  }
}

uint8_t midi_config_file_read(void) {

  ESP_LOGI(TAG, "midi_config_file_read");

  char* contents = grid_platform_read_file_contents("midithrough.cfg");
  if (contents == NULL) {
    return 0;
  }

  uint8_t state = (contents[0] == '1') ? 1 : 0;
  free(contents);
  return state;
}

static void host_lib_daemon_task(void* arg) {
  SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;

  ESP_LOGI(TAG, "Installing USB Host Library");
  usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  ESP_ERROR_CHECK(usb_host_install(&host_config));

  // Signal to the class driver task that the host library is installed
  xSemaphoreGive(signaling_sem);
  vTaskDelay(10); // Short delay to let client task spin up

  bool has_clients = true;
  bool has_devices = true;
  while (has_clients || has_devices) {
    uint32_t event_flags;
    ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      // has_clients = false;
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      // has_devices = false;
    }
    ESP_LOGI(TAG, "Event: %ld", event_flags);
    portYIELD();
  }
  ESP_LOGI(TAG, "No more clients and devices");

  // Uninstall the USB Host Library
  ESP_ERROR_CHECK(usb_host_uninstall());
  // Wait to be deleted
  xSemaphoreGive(signaling_sem);
  vTaskSuspend(NULL);
}

#include "rom/ets_sys.h" // For ets_printf
void grid_platform_printf(char const* fmt, ...) {

  va_list ap;

  char temp[200] = {0};

  va_start(ap, fmt);

  vsnprintf(temp, 199, fmt, ap);

  va_end(ap);

  ets_printf(temp);
}

#include "knot_platform.h"

#include "grid_ain.h"
#include "grid_led.h"
#include "grid_lua.h"
#include "grid_ui.h"
#include "grid_ui_system.h"

#include "grid_esp32_led.h"
#include "grid_esp32_nvm.h"

void knot_lua_ui_init(struct grid_lua_model* lua) {

  struct grid_ui_model* ui = &grid_ui_state;

  grid_lua_create_element_array(lua->L, ui->element_list_length);

  for (int i = 0; i < ui->element_list_length; ++i) {

    struct grid_ui_element* ele = grid_ui_element_find(&grid_ui_state, i);

    if (ele->type == GRID_PARAMETER_ELEMENT_SYSTEM) {
      grid_lua_dostring_unsafe(lua, GRID_LUA_S_META_init);
      grid_lua_register_index_meta_for_type(lua->L, GRID_LUA_S_TYPE, GRID_LUA_S_INDEX_META);
    }

    grid_lua_register_element(lua->L, i);
    grid_lua_register_index_meta_for_element(lua->L, i, GRID_LUA_S_TYPE);
  }
}

void knot_module_ui_init(struct grid_ain_model* ain, struct grid_led_model* led, struct grid_ui_model* ui) {

  grid_led_init(led, 3, NULL);
  grid_ui_model_init(ui, 0 + 1); // +1 for the system element

  grid_ui_element_system_init(&ui->element_list[ui->element_list_length - 1]);

  ui->lua_ui_init_callback = knot_lua_ui_init;
}

extern esp_err_t try_start_in_transfer(void);

bool idle_hook(void) {

  portYIELD();
  return 0; // return 1 causes one trigger / rtos tick
}

void app_main(void) {

  // Configure task timers
  struct grid_utask_timer timer_led = (struct grid_utask_timer){
      .last = grid_platform_rtc_get_micros(),
      .period = 9500, // 10000 really, but FreeRTOS is currently 100 Hz
  };

  // MIDI A/B SWITCH AND THROUGH BUTTON INTERACTIVITY
  gpio_set_direction(SW_AB_PIN, GPIO_MODE_INPUT);
  gpio_pullup_en(SW_AB_PIN);
  gpio_set_direction(SW_MODE_PIN, GPIO_MODE_INPUT);

  SemaphoreHandle_t nvm_or_port = xSemaphoreCreateBinary();
  xSemaphoreGive(nvm_or_port);

  ESP_LOGI(TAG, "===== MAIN START =====");

  // gpio_set_direction(GRID_ESP32_PINS_MAPMODE, GPIO_MODE_INPUT);
  // gpio_pullup_en(GRID_ESP32_PINS_MAPMODE);

  ESP_LOGI(TAG, "===== SYS START =====");
  // grid_sys_init(&grid_sys_state);

  ESP_LOGI(TAG, "===== UI INIT =====");

  knot_module_ui_init(&grid_ain_state, &grid_led_state, &grid_ui_state);
  grid_led_set_pin(&grid_led_state, 1);

  grid_led_set_layer_color(&grid_led_state, 0, 0, 0, 0, 0);
  grid_led_set_layer_color(&grid_led_state, 1, 0, 0, 0, 0);
  grid_led_set_layer_color(&grid_led_state, 2, 0, 0, 0, 0);

  grid_led_set_layer_color(&grid_led_state, 0, 1, 0, 0, 0);
  grid_led_set_layer_color(&grid_led_state, 1, 1, 0, 0, 0);
  grid_led_set_layer_color(&grid_led_state, 2, 1, 0, 0, 0);

  grid_led_set_layer_color(&grid_led_state, 0, 2, 0, 0, 0);
  grid_led_set_layer_color(&grid_led_state, 1, 2, 0, 0, 0);
  grid_led_set_layer_color(&grid_led_state, 2, 2, 0, 0, 0);

  grid_esp32_led_start(grid_led_get_pin(&grid_led_state));

  ESP_LOGI(TAG, "===== NVM START =====");

  xSemaphoreTake(nvm_or_port, 0);
  grid_esp32_nvm_mount(&grid_esp32_nvm_state, false);

  if (gpio_get_level(SW_MODE_PIN) == 0) {

    grid_alert_all_set(&grid_led_state, GRID_LED_COLOR_YELLOW_DIM, 1000);
    grid_alert_all_set_frequency(&grid_led_state, 4);
    grid_platform_nvm_format_and_mount();
    vTaskDelay(pdMS_TO_TICKS(1600));
  }

  xSemaphoreGive(nvm_or_port);

  ESP_LOGI(TAG, "===== LUA INIT =====");
  grid_lua_init(&grid_lua_state, NULL, NULL);
  grid_lua_start_vm(&grid_lua_state, &(struct luaL_Reg){NULL, NULL}, grid_ui_state.lua_ui_init_callback);

#define PMIC_EN_PIN 48

  gpio_set_direction(PMIC_EN_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(PMIC_EN_PIN, 1);

  SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();

  TaskHandle_t daemon_task_hdl;
  TaskHandle_t class_driver_task_hdl;

  TaskHandle_t uart_rx_task_hdl;
  TaskHandle_t uart_tx_task_hdl;

  vTaskDelay(10); // Add a short delay to let the tasks run

  knot_midi_uart_init(&knot_midi_uart_state);

  grid_platform_lsdir("");

  // --- persisted config (LittleFS is mounted by now) ------------------------
  // USB operating mode is latched once, here, for the rest of this boot.
  // A long-press of the mode button rewrites usbmode.cfg and reboots.
  g_usb_mode = knot_usb_mode_read();
  ESP_LOGI(TAG, "===== USB MODE: %s =====", g_usb_mode == KNOT_USB_MODE_INTERFACE ? "INTERFACE (USB-C device)" : "HOST (USB-A)");

  if (midi_config_file_read()) {
    ESP_LOGI(TAG, "Midi through enabled");
    knot_midi_uart_set_midithrough_state(&knot_midi_uart_state, true);
  } else {
    ESP_LOGI(TAG, "Midi through disabled");
    knot_midi_uart_set_midithrough_state(&knot_midi_uart_state, false);
  }

  // LED index 2 steady colour = MIDI-thru state (green off / blue on), both
  // USB modes. "Waiting for the other end" pulses all 3 LEDs: white in host
  // mode (knot_midi_usb.c), yellow in interface mode (knot_midi_usb_device.c).
  // Red is only the long-press mode-change confirmation.
  knot_status_led_update();

  // Steer the external analog USB mux (GPIO11/GPIO12) to the right connector,
  // then bring up exactly one USB stack (the ESP32-S3 has a single USB-OTG).
  knot_usb_mode_apply_mux(g_usb_mode);

  xTaskCreatePinnedToCore(knot_midi_uart_rx_task, "uart_rx", 4096, (void*)signaling_sem, UART_RX_TASK_PRIORITY, &uart_rx_task_hdl, 1);
  xTaskCreatePinnedToCore(knot_midi_uart_tx_task, "uart_tx", 4096, (void*)signaling_sem, UART_TX_TASK_PRIORITY, &uart_tx_task_hdl, 1);

  if (g_usb_mode == KNOT_USB_MODE_HOST) {
    // Legacy path: USB host library + MIDI class driver on the USB-A port.
    xTaskCreatePinnedToCore(host_lib_daemon_task, "daemon", 4096, (void*)signaling_sem, DAEMON_TASK_PRIORITY, &daemon_task_hdl, 1);
    xTaskCreatePinnedToCore(class_driver_task, "class", 4096, (void*)signaling_sem, CLASS_TASK_PRIORITY, &class_driver_task_hdl, 1);
    xTaskCreatePinnedToCore(knot_midi_usb_rx_task, "usb_rx", 4096, (void*)signaling_sem, USB_RX_TASK_PRIORITY, &uart_rx_task_hdl, 1);
    xTaskCreatePinnedToCore(knot_midi_usb_tx_task, "usb_tx", 4096, (void*)signaling_sem, USB_TX_TASK_PRIORITY, &uart_tx_task_hdl, 1);
  } else {
    // New path: TinyUSB MIDI device + PC<->TRS bridge tasks on the USB-C port.
    knot_midi_usb_device_start();
  }

  // Register idle hook to force yield from idle task to lowest priority task
  esp_register_freertos_idle_hook_for_cpu(idle_hook, 0);
  esp_register_freertos_idle_hook_for_cpu(idle_hook, 1);

  uint8_t last_button_state = 1;    // 1 = released (idle-high)
  uint64_t button_pressed_at = 0;   // us, valid while held (0 = not held)
  bool longpress_armed = false;     // crossed the long-press threshold this hold

  UBaseType_t highwatermark = uxTaskGetStackHighWaterMark(xTaskGetCurrentTaskHandle());
  ESP_LOGI(TAG, "highwatermark before main loop: %d", highwatermark);

  while (1) {

    vTaskDelay(pdMS_TO_TICKS(10));

    grid_esp32_utask_led(&timer_led);

    // Drives both TRS_TX_AB_SELECT (GPIO15) and TRS_RX_AB_SELECT (GPIO16).
    knot_midi_uart_set_miditrsab_state(&knot_midi_uart_state, !gpio_get_level(SW_AB_PIN));

    uint8_t current_button_state = gpio_get_level(SW_MODE_PIN);

    // --- press edge -------------------------------------------------------
    if (last_button_state == 1 && current_button_state == 0) {
      button_pressed_at = grid_platform_rtc_get_micros();
      longpress_armed = false;
    }

    // --- held: arm the USB-mode switch once past the threshold -----------
    if (current_button_state == 0 && !longpress_armed && button_pressed_at != 0 &&
        (grid_platform_rtc_get_micros() - button_pressed_at) >= (uint64_t)KNOT_USB_MODE_LONGPRESS_MS * 1000) {
      longpress_armed = true;
      // Solid red on all 3 LEDs = "long-press registered, release to switch".
      grid_alert_all_set(&grid_led_state, 255, 0, 0, -1);
    }

    // --- release edge --------------------------------------------------
    if (last_button_state == 0 && current_button_state == 1) {

      if (longpress_armed) {
        // Change accepted: hold the red confirmation briefly, then reboot
        // automatically into the other USB mode (no manual power-cycle).
        ESP_LOGW(TAG, "mode button long-press -> toggle USB mode");
        grid_alert_all_set(&grid_led_state, 255, 0, 0, -1);
        vTaskDelay(pdMS_TO_TICKS(700));
        knot_usb_mode_toggle_and_reboot(); // never returns
      }

      // Short press: toggle MIDI-thru (TRS IN -> TRS OUT). LED index 2 flips
      // green<->blue immediately (both USB modes) as the feedback.
      bool next = !knot_midi_uart_get_midithrough_state(&knot_midi_uart_state);
      knot_midi_uart_set_midithrough_state(&knot_midi_uart_state, next);
      midi_config_file_update(next);
      knot_status_led_update();

      button_pressed_at = 0;
    }

    last_button_state = current_button_state;
  }
}

#endif
