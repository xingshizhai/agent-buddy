idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        ble
        protocol
        splash
        ui
        nvs_flash
        espressif__esp-box-3
        espressif__esp_lvgl_port
        espressif__icm42670
        espressif__button
        esp_timer
)
