#include "knot_usb_mode.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "grid_platform.h"

static const char* TAG = "KNOT_USB_MODE";

knot_usb_mode_t knot_usb_mode_read(void) {

  char* contents = grid_platform_read_file_contents(KNOT_USB_MODE_CFG_FILE);
  if (contents == NULL) {
    ESP_LOGI(TAG, "%s missing/empty, defaulting to HOST", KNOT_USB_MODE_CFG_FILE);
    return KNOT_USB_MODE_HOST;
  }

  knot_usb_mode_t mode = (contents[0] == '1') ? KNOT_USB_MODE_INTERFACE : KNOT_USB_MODE_HOST;
  free(contents);
  return mode;
}

int knot_usb_mode_write(knot_usb_mode_t mode) {

  int status = grid_platform_write_file_contents(mode == KNOT_USB_MODE_INTERFACE ? "1" : "0", KNOT_USB_MODE_CFG_FILE);
  if (status) {
    ESP_LOGE(TAG, "grid_platform_write_file_contents(%s) returned %d", KNOT_USB_MODE_CFG_FILE, status);
    return status;
  }
  ESP_LOGI(TAG, "persisted USB mode = %d", (int)mode);
  return 0;
}

void knot_usb_mode_apply_mux(knot_usb_mode_t mode) {

  gpio_set_direction(KNOT_USB_SELECT_NATIVE_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(KNOT_USB_SELECT_SOFT_PIN, GPIO_MODE_OUTPUT);

  if (mode == KNOT_USB_MODE_INTERFACE) {
    gpio_set_level(KNOT_USB_SELECT_NATIVE_PIN, KNOT_USB_SELECT_NATIVE_LEVEL_INTERFACE);
    gpio_set_level(KNOT_USB_SELECT_SOFT_PIN, KNOT_USB_SELECT_SOFT_LEVEL_INTERFACE);
    ESP_LOGI(TAG, "USB mux -> CONN_C (interface/device)");
  } else {
    gpio_set_level(KNOT_USB_SELECT_NATIVE_PIN, KNOT_USB_SELECT_NATIVE_LEVEL_HOST);
    gpio_set_level(KNOT_USB_SELECT_SOFT_PIN, KNOT_USB_SELECT_SOFT_LEVEL_HOST);
    ESP_LOGI(TAG, "USB mux -> CONN_A (host)");
  }
}

void knot_usb_mode_toggle_and_reboot(void) {

  knot_usb_mode_t next = (knot_usb_mode_read() == KNOT_USB_MODE_HOST) ? KNOT_USB_MODE_INTERFACE : KNOT_USB_MODE_HOST;

  knot_usb_mode_write(next);

  ESP_LOGW(TAG, "rebooting into USB mode %d", (int)next);
  vTaskDelay(pdMS_TO_TICKS(150)); // let the log/LED flush
  esp_restart();
}
