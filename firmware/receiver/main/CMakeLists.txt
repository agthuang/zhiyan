set(srcs
    "app_main.c"
    "radio.c"
    "audio_ring.c"
    "status_led.c"
    "usb/usb_descriptors.c"
    "usb/vk_usb.c"
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "usb"
    REQUIRES common driver esp_wifi nvs_flash esp_timer usb led_strip
)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
if(NOT tusb_lib)
    idf_component_get_property(tusb_lib tinyusb COMPONENT_LIB)
endif()
if(tusb_lib)
    target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/usb")
endif()
