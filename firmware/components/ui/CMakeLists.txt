idf_component_register(
    SRCS
        "src/ui.c"
        "src/ui_usage.c"
        "src/ui_bluetooth.c"
        "src/ui_splash.c"
        "src/ui_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES splash protocol ble fonts settings
    PRIV_REQUIRES espressif__esp_lvgl_port espressif__esp-box-3 lvgl
)
