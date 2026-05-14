#include "splash/splash.h"
#include "splash/usage_rate.h"
#include "splash_animations.h"
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "SPLASH"

// Sprite is 20x20 pixels; scale up 8x → 160x160 centered on 320x240 display
#define SPRITE_W      20
#define SPRITE_H      20
#define SCALE         8
#define CANVAS_W      (SPRITE_W * SCALE)   // 160
#define CANVAS_H      (SPRITE_H * SCALE)   // 160

// Category string → usage rate group mapping
static int category_to_group(const char *cat)
{
    if (!cat) return 0;
    if (cat[0] == 'W') return 1;          // "Work"
    if (cat[0] == 'E') return 2;          // "Expressions"
    if (cat[0] == 'D') return 3;          // "Dance"
    return 0;                             // "Idle" and fallback
}

static lv_obj_t    *s_root       = NULL;
static lv_obj_t    *s_canvas     = NULL;
static uint16_t    *s_canvas_buf = NULL;
static bool         s_active     = false;
static int          s_cur_anim   = 0;
static int          s_cur_frame  = 0;
static uint32_t     s_frame_ms   = 0;   // timestamp of last frame advance

// Expand one palette-indexed 20x20 frame into the 160x160 RGB565 canvas buffer
static void draw_frame(void)
{
    if (!s_canvas_buf || s_cur_anim >= SPLASH_ANIM_COUNT) return;
    const splash_anim_def_t *a = &splash_anims[s_cur_anim];
    if (s_cur_frame >= a->frame_count) return;
    const uint8_t *src = a->frames[s_cur_frame];

    for (int y = 0; y < SPRITE_H; y++) {
        for (int x = 0; x < SPRITE_W; x++) {
            uint16_t color = a->palette[src[y * SPRITE_W + x]];
            // Swap bytes: LVGL canvas expects little-endian RGB565 on ESP32-S3
            color = (color >> 8) | (color << 8);
            // Scale 8x: fill SCALE×SCALE block
            for (int dy = 0; dy < SCALE; dy++) {
                int row = (y * SCALE + dy) * CANVAS_W;
                for (int dx = 0; dx < SCALE; dx++) {
                    s_canvas_buf[row + x * SCALE + dx] = color;
                }
            }
        }
    }
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, CANVAS_W, CANVAS_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_invalidate(s_canvas);
}

void splash_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);

    size_t buf_size = CANVAS_W * CANVAS_H * sizeof(uint16_t);
    s_canvas_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!s_canvas_buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed (%u bytes)", (unsigned)buf_size);
        return;
    }
    memset(s_canvas_buf, 0, buf_size);

    s_canvas = lv_canvas_create(s_root);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, CANVAS_W, CANVAS_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 0);

    splash_pick_for_current_rate();
    draw_frame();
    ESP_LOGI(TAG, "splash ready — %d animations, canvas %dx%d", SPLASH_ANIM_COUNT, CANVAS_W, CANVAS_H);
}

void splash_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_active || !s_canvas_buf) return;

    const splash_anim_def_t *a = &splash_anims[s_cur_anim];
    uint32_t hold = a->holds[s_cur_frame];
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_frame_ms < hold) return;

    s_cur_frame = (s_cur_frame + 1) % a->frame_count;
    s_frame_ms = now;
    draw_frame();
}

void splash_next(void)
{
    s_cur_anim  = (s_cur_anim + 1) % SPLASH_ANIM_COUNT;
    s_cur_frame = 0;
    s_frame_ms  = (uint32_t)(esp_timer_get_time() / 1000);
    draw_frame();
}

void splash_pick_for_current_rate(void)
{
    int group = usage_rate_group();
    for (int i = 0; i < SPLASH_ANIM_COUNT; i++) {
        if (category_to_group(splash_anims[i].category) == group) {
            s_cur_anim  = i;
            s_cur_frame = 0;
            s_frame_ms  = (uint32_t)(esp_timer_get_time() / 1000);
            return;
        }
    }
    s_cur_anim  = 0;
    s_cur_frame = 0;
    s_frame_ms  = (uint32_t)(esp_timer_get_time() / 1000);
}

void splash_show(void)
{
    s_active = true;
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    splash_pick_for_current_rate();
}

void splash_hide(void)
{
    s_active = false;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

bool      splash_is_active(void) { return s_active; }
lv_obj_t *splash_get_root(void)  { return s_root; }
