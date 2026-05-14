#pragma once
#include "protocol/usage_data.h"
#include "ble/ble_gatt.h"
#include <stdbool.h>

typedef enum {
    SCREEN_SPLASH     = 0,
    SCREEN_USAGE      = 1,
    SCREEN_BLUETOOTH  = 2,
    SCREEN_COUNT      = 3,
} screen_t;

void     ui_init(void);
void     ui_update(const usage_data_t *data);
void     ui_show_screen(screen_t screen);
void     ui_cycle_screen(void);
screen_t ui_get_current_screen(void);
void     ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac);
