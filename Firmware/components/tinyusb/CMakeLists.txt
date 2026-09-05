# Local TinyUSB component - upstream hathach/tinyusb 0.20.0 (submodule commit
# b935f3f), vendored to match intechstudio/grid-fw's build. Replaces the
# espressif/esp_tinyusb managed component so grid_common's grid_usb_*.c (which
# target the raw TinyUSB device API + a static tusb_config.h) compile, and so
# Knot's own USB-C MIDI device mode uses the same stack.
#
# Device classes only (CDC + MIDI + HID); Knot's USB *host* mode uses IDF's
# native `usb` host driver, not this.
set(TUSB_ROOT "${CMAKE_CURRENT_LIST_DIR}")

set(tusb_srcs
    "${TUSB_ROOT}/src/tusb.c"
    "${TUSB_ROOT}/src/common/tusb_fifo.c"
    "${TUSB_ROOT}/src/device/usbd.c"
    "${TUSB_ROOT}/src/device/usbd_control.c"
    "${TUSB_ROOT}/src/class/cdc/cdc_device.c"
    "${TUSB_ROOT}/src/class/midi/midi_device.c"
    "${TUSB_ROOT}/src/class/hid/hid_device.c"
    "${TUSB_ROOT}/src/portable/synopsys/dwc2/dcd_dwc2.c"
    "${TUSB_ROOT}/src/portable/synopsys/dwc2/dwc2_common.c"
)

idf_component_register(
    SRCS        ${tusb_srcs}
    INCLUDE_DIRS
        "include"
        "src"
    PRIV_INCLUDE_DIRS
        "src/device"
    REQUIRES
        freertos
)

# usbd.c compares uint8_t against BUILTIN_DRIVER_COUNT which may always be
# true when no built-in class drivers are enabled - suppress the warning.
set_source_files_properties(
    "${TUSB_ROOT}/src/device/usbd.c"
    PROPERTIES COMPILE_FLAGS "-Wno-type-limits"
)
