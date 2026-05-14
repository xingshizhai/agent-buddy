#pragma once
#include "lvgl.h"
#include <stdbool.h>

void      splash_init(lv_obj_t *parent);
void      splash_tick(lv_timer_t *t);
void      splash_next(void);
void      splash_show(void);
void      splash_hide(void);
void      splash_pick_for_current_rate(void);
bool      splash_is_active(void);
lv_obj_t *splash_get_root(void);
