idf_component_register(
    SRCS
        "app_main.c"
        "buttons.c"
        "lcd.c"
        "mic.c"
        "battery_hw.c"
        "radio.c"
        "ble_media.c"
        "esp_hid_gap.c"
    INCLUDE_DIRS "."
    REQUIRES common driver esp_wifi nvs_flash esp_adc esp_timer esp_lcd bt esp_hid esp_event
)
