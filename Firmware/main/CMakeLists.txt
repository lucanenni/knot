idf_component_register(
    SRCS
        "midi_host_fw.c"
    INCLUDE_DIRS
        "."
)
# NOTE: the `main` component implicitly depends on every other component in the
# project, so knot_usb_mode / knot_midi_usb_device are picked up automatically.
