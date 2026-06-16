// firmware/components/ui/src/ui_usage.h
#pragma once
#include "lvgl.h"
#include "ble/ble_gatt.h"
#include "protocol/usage_data.h"

// Per-screen widget handles — one instance per registered service screen.
typedef struct {
    lv_obj_t *arc_session;
    lv_obj_t *arc_weekly;
    lv_obj_t *lbl_session_pct;
    lv_obj_t *lbl_weekly_pct;
    lv_obj_t *lbl_session_reset;
    lv_obj_t *lbl_weekly_reset;
    lv_obj_t *lbl_status;
    lv_obj_t *ble_dot;
} ui_usage_ctx_t;

// Create widgets on *parent for the given service name.
// Caller provides *ctx to receive widget handles.
void ui_usage_init(lv_obj_t *parent, const char *svc_name, ui_usage_ctx_t *ctx);

// Update all widgets from *data using *ctx.
void ui_usage_update(ui_usage_ctx_t *ctx, const usage_data_t *data);

// Update only the BLE indicator dot.
void ui_usage_update_ble(ui_usage_ctx_t *ctx, ble_gatt_state_t state);
