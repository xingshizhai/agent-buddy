idf_component_register(
    SRCS "src/ui.c" "src/ui_usage.c" "src/ui_bluetooth.c" "src/ui_splash.c"
    INCLUDE_DIRS "include"
    REQUIRES splash protocol ble fonts
    PRIV_REQUIRES espressif__esp_lvgl_port espressif__esp-box-3 lvgl
)
