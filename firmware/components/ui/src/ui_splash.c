#include "ui_splash.h"
#include "ui/ui.h"
#include "splash/splash.h"

static void on_splash_touch(lv_event_t *e)
{
    ui_show_screen(SCREEN_USAGE);
}

void ui_splash_init(lv_obj_t *parent)
{
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, on_splash_touch, LV_EVENT_CLICKED, NULL);
}
