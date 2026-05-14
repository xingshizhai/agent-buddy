idf_component_register(
    SRCS "src/ble_gatt.c" "src/ble_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
    PRIV_REQUIRES bt
)
