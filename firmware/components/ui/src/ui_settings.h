#pragma once
#include "lvgl.h"
#include "settings/settings.h"

// Initialise the settings screen widgets on *parent.
// *cfg is the live settings struct; widgets read and write it directly.
// on_exit is called (from LVGL task) when the user saves and exits.
void ui_settings_init(lv_obj_t *parent, device_settings_t *cfg,
                      void (*on_exit)(void));

// Refresh displayed values from *cfg (call after external change).
void ui_settings_refresh(void);
