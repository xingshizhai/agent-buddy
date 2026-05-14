idf_component_register(
    SRCS "src/usage_rate.c" "src/splash.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__esp_lvgl_port lvgl espressif__esp-box-3 esp_timer
)
