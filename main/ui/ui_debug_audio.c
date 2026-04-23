#include "ui_debug_audio.h"
#include "ui.h"
#include "app_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include <math.h>

static const char *TAG = "ui_debug_audio";

#define REC_SAMPLE_RATE   16000
#define REC_MAX_SECONDS   60
#define REC_BUF_SAMPLES   (REC_SAMPLE_RATE * 2 * REC_MAX_SECONDS)
#define REC_CHUNK         1600   // 50 ms per read
#define PLAY_CHUNK        3200   // 100 ms per write

#define TONE_FREQ_HZ      440.0f
#define TONE_SECONDS      3
#define TONE_SAMPLES      (REC_SAMPLE_RATE * 2 * TONE_SECONDS)  // stereo

static lv_obj_t    *s_status_lbl   = NULL;
static lv_obj_t    *s_time_lbl     = NULL;
static lv_obj_t    *s_rec_btn      = NULL;
static lv_obj_t    *s_rec_btn_lbl  = NULL;
static lv_obj_t    *s_level_bar    = NULL;
static lv_obj_t    *s_vol_lbl      = NULL;
static lv_timer_t  *s_tick_timer   = NULL;

static int16_t     *s_rec_buf       = NULL;
static size_t       s_rec_samples   = 0;
static bool         s_recording     = false;
static bool         s_playing       = false;
static uint32_t     s_rec_start_ms  = 0;
static uint32_t     s_play_start_ms = 0;
static volatile int s_level_pct     = 0;

// ── Peak level ────────────────────────────────────────────────────────────────

static int compute_level_pct(const int16_t *buf, size_t samples)
{
    if (!buf || samples == 0) return 0;
    int32_t peak = 0;
    for (size_t i = 0; i < samples; i++) {
        int32_t v = buf[i] < 0 ? -(int32_t)buf[i] : (int32_t)buf[i];
        if (v > peak) peak = v;
    }
    return (int)(peak * 100 / 32767);
}

// ── Status helper ─────────────────────────────────────────────────────────────

static void ui_set_status(const char *txt, lv_color_t color)
{
    if (!s_status_lbl) return;
    if (lvgl_port_lock(50)) {
        lv_label_set_text(s_status_lbl, txt);
        lv_obj_set_style_text_color(s_status_lbl, color, LV_PART_MAIN);
        lvgl_port_unlock();
    }
}

// ── LVGL timer — 100 ms, updates time label and level bar ─────────────────────

static void on_tick(lv_timer_t *t)
{
    lv_color_t bar_color;
    if (s_level_pct < 60)      bar_color = lv_color_hex(0x4CAF50);
    else if (s_level_pct < 85) bar_color = lv_color_hex(0xFFAA00);
    else                        bar_color = lv_color_hex(0xFF4444);
    lv_obj_set_style_bg_color(s_level_bar, bar_color, LV_PART_INDICATOR);
    lv_bar_set_value(s_level_bar, s_level_pct, LV_ANIM_OFF);

    char buf[32];
    if (s_recording) {
        uint32_t elapsed = (lv_tick_get() - s_rec_start_ms) / 1000;
        snprintf(buf, sizeof(buf), "%02" PRIu32 ":%02" PRIu32, elapsed / 60, elapsed % 60);
        lv_label_set_text(s_time_lbl, buf);
        if (elapsed >= REC_MAX_SECONDS) {
            s_recording = false;
            lv_label_set_text(s_rec_btn_lbl, LV_SYMBOL_AUDIO " Record");
            lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
            ui_set_status("Max length reached — ready to play", lv_color_hex(0xFFAA00));
        }
    } else if (s_playing) {
        uint32_t elapsed = (lv_tick_get() - s_play_start_ms) / 1000;
        snprintf(buf, sizeof(buf), "%02" PRIu32 ":%02" PRIu32, elapsed / 60, elapsed % 60);
        lv_label_set_text(s_time_lbl, buf);
    } else {
        lv_bar_set_value(s_level_bar, 0, LV_ANIM_OFF);
        lv_timer_delete(t);
        s_tick_timer = NULL;
    }
}

static void start_tick_timer(void)
{
    if (!s_tick_timer)
        s_tick_timer = lv_timer_create(on_tick, 100, NULL);
}

// ── Recording task ─────────────────────────────────────────────────────────────

static void rec_task(void *arg)
{
    while (s_recording) {
        size_t avail = REC_BUF_SAMPLES - s_rec_samples;
        if (avail == 0) break;
        size_t to_read = (avail < REC_CHUNK) ? avail : REC_CHUNK;
        size_t got = 0;
        esp_err_t ret = app_audio_record_read(s_rec_buf + s_rec_samples, to_read, &got);
        if (ret != ESP_OK || got == 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        s_level_pct = compute_level_pct(s_rec_buf + s_rec_samples, got);
        s_rec_samples += got;
    }
    s_level_pct = 0;
    app_audio_record_stop();
    s_recording = false;

    size_t secs = s_rec_samples / (REC_SAMPLE_RATE * 2);
    char msg[48];
    snprintf(msg, sizeof(msg), "Recorded %zu sec — ready to play", secs);
    ui_set_status(msg, lv_color_hex(0x4CAF50));

    if (lvgl_port_lock(50)) {
        lv_label_set_text(s_rec_btn_lbl, LV_SYMBOL_AUDIO " Record");
        lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

// ── Playback task ─────────────────────────────────────────────────────────────

static void play_task(void *arg)
{
    s_playing = true;
    esp_err_t ret = app_audio_play_start(REC_SAMPLE_RATE);
    if (ret == ESP_OK) {
        size_t offset = 0;
        while (offset < s_rec_samples && s_playing) {
            size_t chunk = s_rec_samples - offset;
            if (chunk > PLAY_CHUNK) chunk = PLAY_CHUNK;
            ret = app_audio_play_write(s_rec_buf + offset, chunk);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "play_write err 0x%x at offset %zu", ret, offset);
                break;
            }
            s_level_pct = compute_level_pct(s_rec_buf + offset, chunk);
            offset += chunk;
        }
        app_audio_play_stop();
    } else {
        ESP_LOGE(TAG, "play_start failed 0x%x", ret);
    }
    s_level_pct = 0;
    s_playing = false;
    ui_set_status("Playback done", lv_color_hex(0x4CAF50));
    vTaskDelete(NULL);
}

// ── 440 Hz reference tone task ────────────────────────────────────────────────

static void tone_task(void *arg)
{
    s_playing = true;

    int16_t *buf = heap_caps_malloc(TONE_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!buf) {
        s_playing = false;
        ui_set_status("Alloc failed", lv_color_hex(0xFF5555));
        vTaskDelete(NULL);
        return;
    }

    // Generate 440 Hz sine wave, amplitude 70 %
    for (size_t i = 0; i < TONE_SAMPLES; i += 2) {
        float t   = (float)(i / 2) / REC_SAMPLE_RATE;
        int16_t v = (int16_t)(0.70f * 32767.0f * sinf(2.0f * (float)M_PI * TONE_FREQ_HZ * t));
        buf[i]     = v;  // L
        buf[i + 1] = v;  // R
    }

    esp_err_t ret = app_audio_play_start(REC_SAMPLE_RATE);
    if (ret == ESP_OK) {
        size_t offset = 0;
        while (offset < TONE_SAMPLES && s_playing) {
            size_t chunk = TONE_SAMPLES - offset;
            if (chunk > PLAY_CHUNK) chunk = PLAY_CHUNK;
            app_audio_play_write(buf + offset, chunk);
            s_level_pct = compute_level_pct(buf + offset, chunk);
            offset += chunk;
        }
        app_audio_play_stop();
    } else {
        ESP_LOGE(TAG, "tone play_start failed 0x%x", ret);
    }

    heap_caps_free(buf);
    s_level_pct = 0;
    s_playing = false;
    ui_set_status("Tone done", lv_color_hex(0x4CAF50));
    vTaskDelete(NULL);
}

// ── Button callbacks ──────────────────────────────────────────────────────────

static void on_back(lv_event_t *e)
{
    (void)e;
    s_recording = false;
    s_playing   = false;
    if (s_tick_timer) { lv_timer_delete(s_tick_timer); s_tick_timer = NULL; }
    ui_navigate_to(UI_SCREEN_DEBUG);
}

static void on_rec_btn(lv_event_t *e)
{
    (void)e;

    if (s_playing) {
        lv_label_set_text(s_status_lbl, "Wait for playback to finish");
        return;
    }

    if (s_recording) {
        s_recording = false;
        lv_label_set_text(s_status_lbl, "Stopping...");
        lv_label_set_text(s_rec_btn_lbl, LV_SYMBOL_AUDIO " Record");
        lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
        return;
    }

    if (!app_audio_mic_available()) {
        lv_label_set_text(s_status_lbl, "Mic not available (ES7210 init failed)");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xFF5555), LV_PART_MAIN);
        return;
    }

    if (!s_rec_buf) {
        s_rec_buf = heap_caps_malloc(REC_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!s_rec_buf) {
            lv_label_set_text(s_status_lbl, "PSRAM alloc failed");
            return;
        }
    }

    esp_err_t err = app_audio_record_start(REC_SAMPLE_RATE);
    if (err != ESP_OK) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Record start failed (0x%x)", err);
        lv_label_set_text(s_status_lbl, msg);
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xFF5555), LV_PART_MAIN);
        return;
    }

    s_rec_samples  = 0;
    s_recording    = true;
    s_rec_start_ms = lv_tick_get();

    lv_label_set_text(s_time_lbl, "00:00");
    lv_label_set_text(s_status_lbl, "Recording...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_label_set_text(s_rec_btn_lbl, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(0xFF4444), LV_PART_MAIN);

    start_tick_timer();
    xTaskCreate(rec_task, "rec", 4096, NULL, 5, NULL);
}

static void on_play_btn(lv_event_t *e)
{
    (void)e;
    if (s_recording) { lv_label_set_text(s_status_lbl, "Stop recording first"); return; }
    if (s_playing)   { lv_label_set_text(s_status_lbl, "Already playing...");    return; }
    if (!s_rec_buf || s_rec_samples == 0) {
        lv_label_set_text(s_status_lbl, "Nothing recorded yet");
        return;
    }
    s_play_start_ms = lv_tick_get();
    lv_label_set_text(s_time_lbl, "00:00");
    lv_label_set_text(s_status_lbl, "Playing...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x64B5F6), LV_PART_MAIN);
    start_tick_timer();
    xTaskCreate(play_task, "play", 4096, NULL, 5, NULL);
}

static void update_vol_label(void)
{
    if (!s_vol_lbl) return;
    char buf[16];
    snprintf(buf, sizeof(buf), LV_SYMBOL_VOLUME_MAX " %d%%", app_audio_get_volume());
    lv_label_set_text(s_vol_lbl, buf);
}

static void on_vol_down(lv_event_t *e)
{
    (void)e;
    app_audio_set_volume(app_audio_get_volume() - 5);
    update_vol_label();
}

static void on_vol_up(lv_event_t *e)
{
    (void)e;
    app_audio_set_volume(app_audio_get_volume() + 5);
    update_vol_label();
}

static void on_tone_btn(lv_event_t *e)
{
    (void)e;
    if (s_recording) { lv_label_set_text(s_status_lbl, "Stop recording first"); return; }
    if (s_playing)   { lv_label_set_text(s_status_lbl, "Already playing...");    return; }
    s_play_start_ms = lv_tick_get();
    lv_label_set_text(s_time_lbl, "00:00");
    lv_label_set_text(s_status_lbl, "440 Hz tone...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xFFD700), LV_PART_MAIN);
    start_tick_timer();
    xTaskCreate(tone_task, "tone", 4096, NULL, 5, NULL);
}

// ── Screen create ──────────────────────────────────────────────────────────────

lv_obj_t *ui_debug_audio_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Top bar
    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, LV_PCT(100), 48);
    lv_obj_align(topbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(topbar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(topbar, 0, LV_PART_MAIN);

    lv_obj_t *bb = lv_button_create(topbar);
    lv_obj_set_size(bb, 80, 36);
    lv_obj_align(bb, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(bb, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_add_event_cb(bb, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bbl = lv_label_create(bb);
    lv_label_set_text(bbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(bbl);

    lv_obj_t *title = lv_label_create(topbar);
    lv_label_set_text(title, LV_SYMBOL_AUDIO " Audio Test");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Elapsed time
    s_time_lbl = lv_label_create(scr);
    lv_label_set_text(s_time_lbl, "--:--");
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, -110);

    // Status label
    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, app_audio_mic_available() ? "Ready" : "Mic not available");
    lv_obj_set_style_text_color(s_status_lbl,
        app_audio_mic_available() ? lv_color_hex(0x64B5F6) : lv_color_hex(0xFF5555),
        LV_PART_MAIN);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_lbl, 600);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, -60);

    // Level bar
    s_level_bar = lv_bar_create(scr);
    lv_obj_set_size(s_level_bar, 600, 22);
    lv_obj_align(s_level_bar, LV_ALIGN_CENTER, 0, -18);
    lv_bar_set_range(s_level_bar, 0, 100);
    lv_bar_set_value(s_level_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_level_bar, lv_color_hex(0x1E2A45), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_level_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_level_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_level_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_level_bar, 4, LV_PART_INDICATOR);

    // Three action buttons: Record | Play | 440Hz
    s_rec_btn = lv_button_create(scr);
    lv_obj_set_size(s_rec_btn, 160, 60);
    lv_obj_align(s_rec_btn, LV_ALIGN_CENTER, -210, 50);
    lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_set_style_radius(s_rec_btn, 30, LV_PART_MAIN);
    lv_obj_add_event_cb(s_rec_btn, on_rec_btn, LV_EVENT_CLICKED, NULL);
    s_rec_btn_lbl = lv_label_create(s_rec_btn);
    lv_label_set_text(s_rec_btn_lbl, LV_SYMBOL_AUDIO " Record");
    lv_obj_set_style_text_color(s_rec_btn_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(s_rec_btn_lbl);

    lv_obj_t *play_btn = lv_button_create(scr);
    lv_obj_set_size(play_btn, 160, 60);
    lv_obj_align(play_btn, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_radius(play_btn, 30, LV_PART_MAIN);
    lv_obj_add_event_cb(play_btn, on_play_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play_lbl = lv_label_create(play_btn);
    lv_label_set_text(play_lbl, LV_SYMBOL_PLAY " Play");
    lv_obj_set_style_text_color(play_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(play_lbl);

    lv_obj_t *tone_btn = lv_button_create(scr);
    lv_obj_set_size(tone_btn, 160, 60);
    lv_obj_align(tone_btn, LV_ALIGN_CENTER, 210, 50);
    lv_obj_set_style_bg_color(tone_btn, lv_color_hex(0x7B5EA7), LV_PART_MAIN);
    lv_obj_set_style_radius(tone_btn, 30, LV_PART_MAIN);
    lv_obj_add_event_cb(tone_btn, on_tone_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tone_lbl = lv_label_create(tone_btn);
    lv_label_set_text(tone_lbl, LV_SYMBOL_VOLUME_MAX " 440Hz");
    lv_obj_set_style_text_color(tone_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(tone_lbl);

    // Volume control row: [Vol-]  Vol: 75%  [Vol+]
    lv_obj_t *vdown = lv_button_create(scr);
    lv_obj_set_size(vdown, 80, 44);
    lv_obj_align(vdown, LV_ALIGN_CENTER, -130, 128);
    lv_obj_set_style_bg_color(vdown, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_radius(vdown, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(vdown, on_vol_down, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vd_lbl = lv_label_create(vdown);
    lv_label_set_text(vd_lbl, LV_SYMBOL_MINUS " Vol");
    lv_obj_set_style_text_color(vd_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(vd_lbl);

    s_vol_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_vol_lbl, lv_color_hex(0xB0BEC5), LV_PART_MAIN);
    lv_obj_align(s_vol_lbl, LV_ALIGN_CENTER, 0, 128);
    update_vol_label();

    lv_obj_t *vup = lv_button_create(scr);
    lv_obj_set_size(vup, 80, 44);
    lv_obj_align(vup, LV_ALIGN_CENTER, 130, 128);
    lv_obj_set_style_bg_color(vup, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_radius(vup, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(vup, on_vol_up, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vu_lbl = lv_label_create(vup);
    lv_label_set_text(vu_lbl, LV_SYMBOL_PLUS " Vol");
    lv_obj_set_style_text_color(vu_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(vu_lbl);

    // Hint
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Record → Play to test mic/speaker. 440Hz plays a reference tone.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555577), LV_PART_MAIN);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, 620);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    ESP_LOGI(TAG, "Audio debug screen created (mic=%s)",
             app_audio_mic_available() ? "OK" : "N/A");
    return scr;
}
