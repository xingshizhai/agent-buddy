#pragma once
#include "protocol/usage_data.h"
#include "ble/ble_gatt.h"
#include <stdbool.h>

typedef enum {
    SCREEN_SPLASH    = 0,
    SCREEN_SERVICE   = 1,   // any registered service screen
    SCREEN_BLUETOOTH = 2,
} screen_t;

void     ui_init(void);

// Register a usage screen for the given service name.
// Must be called after ui_init() and before the main loop runs.
// Screens appear in registration order in the button-press cycle.
void     ui_register_service_screen(const char *svc_name);

// Update the usage screen for data->platform (no-op if not registered).
void     ui_update(const usage_data_t *data);

void     ui_show_screen(screen_t screen);
void     ui_cycle_screen(void);
screen_t ui_get_current_screen(void);
void     ui_update_ble_status(ble_gatt_state_t state, const char *name, const char *mac);
