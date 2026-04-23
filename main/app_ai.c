#include "app_ai.h"
#include "app_audio.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_transport_ws.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "app_ai";

// ── Configuration ─────────────────────────────────────────────────────────────
#define MIC_SAMPLE_RATE     16000
#define MIC_FRAME_MS        40          // 40ms per mic read chunk
#define MIC_FRAME_SAMPLES   (MIC_SAMPLE_RATE * MIC_FRAME_MS / 1000 * 2)  // stereo
#define MIC_FRAME_BYTES     (MIC_FRAME_SAMPLES * sizeof(int16_t))

// VAD: auto end-of-speech after silence
#define VAD_SILENCE_MS      1500        // 1.5s of silence → end speech
#define VAD_ENERGY_THRESH   200         // RMS threshold for speech

// Play queue: number of PCM chunks buffered
#define PLAY_QUEUE_LEN      16
#define PLAY_CHUNK_MAX      4096        // bytes per chunk in queue

// Event bits
#define EVT_CONNECTED       BIT0
#define EVT_DISCONNECTED    BIT1
#define EVT_READY           BIT2
#define EVT_STOP_MIC        BIT3

// ── State ─────────────────────────────────────────────────────────────────────
static esp_websocket_client_handle_t s_ws         = NULL;
static volatile ai_state_t           s_state      = AI_STATE_DISCONNECTED;
static EventGroupHandle_t            s_evg        = NULL;
static QueueHandle_t                 s_play_queue = NULL; // of play_chunk_t*
static TaskHandle_t                  s_mic_task   = NULL;
static TaskHandle_t                  s_play_task  = NULL;

static ai_state_cb_t s_state_cb      = NULL;
static ai_text_cb_t  s_transcript_cb = NULL;
static ai_text_cb_t  s_response_cb   = NULL;
static ai_text_cb_t  s_status_cb     = NULL;

typedef struct {
    uint8_t *data;
    size_t   len;
} play_chunk_t;

// ── LVGL async callbacks ──────────────────────────────────────────────────────
// lv_async_call can carry a single void* — use a heap-allocated struct

typedef struct { ai_state_t state; } async_state_arg_t;
typedef struct { char text[512]; } async_text_arg_t;

static void lvgl_state_cb(void *arg)
{
    async_state_arg_t *a = arg;
    if (s_state_cb) s_state_cb(a->state);
    free(a);
}

static void lvgl_transcript_cb(void *arg)
{
    async_text_arg_t *a = arg;
    if (s_transcript_cb) s_transcript_cb(a->text);
    free(a);
}

static void lvgl_response_cb(void *arg)
{
    async_text_arg_t *a = arg;
    if (s_response_cb) s_response_cb(a->text);
    free(a);
}

static void lvgl_status_cb(void *arg)
{
    async_text_arg_t *a = arg;
    if (s_status_cb) s_status_cb(a->text);
    free(a);
}

static void fire_state(ai_state_t state)
{
    s_state = state;
    if (!s_state_cb) return;
    async_state_arg_t *a = malloc(sizeof(*a));
    if (!a) return;
    a->state = state;
    lv_async_call(lvgl_state_cb, a);
}

static void fire_text(ai_text_cb_t cb, lv_async_cb_t lvgl_cb, const char *text)
{
    if (!cb) return;
    async_text_arg_t *a = malloc(sizeof(*a));
    if (!a) return;
    strlcpy(a->text, text, sizeof(a->text));
    lv_async_call(lvgl_cb, a);
}

// ── WebSocket send helpers ─────────────────────────────────────────────────────

static esp_err_t ws_send_json(const char *json_str)
{
    if (!s_ws || s_state == AI_STATE_DISCONNECTED) return ESP_FAIL;
    int r = esp_websocket_client_send_text(s_ws, json_str, strlen(json_str),
                                           pdMS_TO_TICKS(2000));
    return r >= 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t ws_send_pcm(const void *data, size_t len)
{
    if (!s_ws) return ESP_FAIL;
    int r = esp_websocket_client_send_bin(s_ws, data, len, pdMS_TO_TICKS(2000));
    return r >= 0 ? ESP_OK : ESP_FAIL;
}

// ── Minimal JSON string extractor ────────────────────────────────────────────
// Only handles flat {"key":"value"} objects — sufficient for our protocol.

static bool json_get_str(const char *json, int json_len,
                          const char *key, char *out, size_t out_len)
{
    // Build null-terminated copy for strstr
    char *buf = strndup(json, json_len);
    if (!buf) return false;

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    char *found = strstr(buf, needle);
    if (!found) { free(buf); return false; }

    const char *p = found + strlen(needle);
    // skip whitespace and ':'
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p != '"') { free(buf); return false; }
    p++; // skip opening quote

    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\') { p++; if (*p) out[i++] = *p++; }
        else out[i++] = *p++;
    }
    out[i] = '\0';
    free(buf);
    return true;
}

// ── JSON parsing ──────────────────────────────────────────────────────────────

static void handle_json(const char *txt, int len)
{
    char type_str[32]  = {0};
    char value_str[480] = {0};

    if (!json_get_str(txt, len, "type", type_str, sizeof(type_str)))
        return;

    if (strcmp(type_str, "ready") == 0) {
        xEventGroupSetBits(s_evg, EVT_READY);
        fire_state(AI_STATE_IDLE);

    } else if (strcmp(type_str, "status") == 0) {
        if (json_get_str(txt, len, "msg", value_str, sizeof(value_str)))
            fire_text(s_status_cb, lvgl_status_cb, value_str);

    } else if (strcmp(type_str, "transcript") == 0) {
        if (json_get_str(txt, len, "text", value_str, sizeof(value_str)))
            fire_text(s_transcript_cb, lvgl_transcript_cb, value_str);

    } else if (strcmp(type_str, "response_start") == 0) {
        fire_state(AI_STATE_PLAYING);

    } else if (strcmp(type_str, "response_end") == 0) {
        if (json_get_str(txt, len, "text", value_str, sizeof(value_str)))
            fire_text(s_response_cb, lvgl_response_cb, value_str);
        fire_state(AI_STATE_IDLE);

    } else if (strcmp(type_str, "error") == 0) {
        if (json_get_str(txt, len, "msg", value_str, sizeof(value_str)))
            fire_text(s_status_cb, lvgl_status_cb, value_str);
        fire_state(AI_STATE_IDLE);
    }
}

// ── Audio playback task ────────────────────────────────────────────────────────

static void play_task(void *arg)
{
    ESP_LOGI(TAG, "play_task started");
    bool playing = false;

    while (true) {
        play_chunk_t *chunk = NULL;
        if (xQueueReceive(s_play_queue, &chunk, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (!playing) {
                app_audio_play_start(MIC_SAMPLE_RATE);
                playing = true;
            }
            app_audio_play_write((const int16_t *)chunk->data,
                                 chunk->len / sizeof(int16_t));
            free(chunk->data);
            free(chunk);
        } else {
            // Queue empty — if we were playing and state went back to idle, stop
            if (playing && s_state != AI_STATE_PLAYING) {
                app_audio_play_stop();
                playing = false;
            }
        }
    }
}

// ── Mic streaming task ────────────────────────────────────────────────────────

static void mic_task(void *arg)
{
    ESP_LOGI(TAG, "mic_task started");

    int16_t *buf = malloc(MIC_FRAME_BYTES);
    if (!buf) { vTaskDelete(NULL); return; }

    uint32_t silence_ms    = 0;
    bool     speech_active = false;

    app_audio_record_start(MIC_SAMPLE_RATE);
    ws_send_json("{\"type\":\"start_speech\"}");
    fire_state(AI_STATE_RECORDING);

    while (!(xEventGroupGetBits(s_evg) & EVT_STOP_MIC)) {
        size_t got = 0;
        esp_err_t r = app_audio_record_read(buf, MIC_FRAME_SAMPLES, &got);
        if (r != ESP_OK || got == 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // Send raw stereo PCM to server
        ws_send_pcm(buf, got * sizeof(int16_t));

        // VAD: RMS energy
        int64_t energy = 0;
        for (size_t i = 0; i < got; i++)
            energy += (int32_t)buf[i] * buf[i];
        int rms = (int)sqrtf((float)(energy / (int64_t)got));

        if (rms >= VAD_ENERGY_THRESH) {
            speech_active = true;
            silence_ms    = 0;
        } else if (speech_active) {
            silence_ms += MIC_FRAME_MS;
            if (silence_ms >= VAD_SILENCE_MS) {
                ESP_LOGI(TAG, "VAD: silence timeout → end speech");
                break;
            }
        }
    }

    app_audio_record_stop();
    ws_send_json("{\"type\":\"end_speech\"}");
    fire_state(AI_STATE_PROCESSING);
    free(buf);
    xEventGroupClearBits(s_evg, EVT_STOP_MIC);
    s_mic_task = NULL;
    vTaskDelete(NULL);
}

// ── WebSocket event handler ───────────────────────────────────────────────────

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        xEventGroupSetBits(s_evg, EVT_CONNECTED);
        xEventGroupClearBits(s_evg, EVT_DISCONNECTED);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        xEventGroupSetBits(s_evg, EVT_DISCONNECTED);
        xEventGroupClearBits(s_evg, EVT_CONNECTED | EVT_READY);
        fire_state(AI_STATE_DISCONNECTED);
        break;

    case WEBSOCKET_EVENT_DATA: {
        uint8_t op = data->op_code & ~WS_TRANSPORT_OPCODES_FIN;
        if (op == WS_TRANSPORT_OPCODES_TEXT && data->data_len > 0) {
            handle_json(data->data_ptr, data->data_len);
        } else if (op == WS_TRANSPORT_OPCODES_BINARY && data->data_len > 0) {
            // TTS audio from server: queue for playback
            play_chunk_t *chunk = malloc(sizeof(play_chunk_t));
            if (chunk) {
                chunk->data = malloc(data->data_len);
                chunk->len  = data->data_len;
                if (chunk->data) {
                    memcpy(chunk->data, data->data_ptr, data->data_len);
                    if (xQueueSend(s_play_queue, &chunk, 0) != pdTRUE) {
                        free(chunk->data);
                        free(chunk);
                        ESP_LOGW(TAG, "play_queue full, dropping chunk");
                    }
                } else {
                    free(chunk);
                }
            }
        }
        break;
    }

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        break;

    default:
        break;
    }
}

// ── Public API ─────────────────────────────────────────────────────────────────

void app_ai_set_state_cb(ai_state_cb_t cb)      { s_state_cb      = cb; }
void app_ai_set_transcript_cb(ai_text_cb_t cb)  { s_transcript_cb = cb; }
void app_ai_set_response_cb(ai_text_cb_t cb)    { s_response_cb   = cb; }
void app_ai_set_status_cb(ai_text_cb_t cb)      { s_status_cb     = cb; }

ai_state_t app_ai_get_state(void) { return s_state; }

esp_err_t app_ai_init(void)
{
    s_evg = xEventGroupCreate();
    if (!s_evg) return ESP_ERR_NO_MEM;

    s_play_queue = xQueueCreate(PLAY_QUEUE_LEN, sizeof(play_chunk_t *));
    if (!s_play_queue) return ESP_ERR_NO_MEM;

    xTaskCreatePinnedToCore(play_task, "ai_play", 8192, NULL, 5, &s_play_task, 1);

    esp_websocket_client_config_t ws_cfg = {
        .uri                = CONFIG_AGENT_BUDDY_SERVER_URL,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
        .buffer_size          = 16384,
        .task_stack           = 8192,
    };
    s_ws = esp_websocket_client_init(&ws_cfg);
    if (!s_ws) return ESP_FAIL;

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);

    fire_state(AI_STATE_CONNECTING);
    esp_err_t r = esp_websocket_client_start(s_ws);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket start failed: 0x%x", r);
        return r;
    }
    ESP_LOGI(TAG, "WebSocket connecting to %s", CONFIG_AGENT_BUDDY_SERVER_URL);
    return ESP_OK;
}

esp_err_t app_ai_start_recording(void)
{
    if (s_state != AI_STATE_IDLE) {
        ESP_LOGW(TAG, "start_recording: not idle (state=%d)", s_state);
        return ESP_ERR_INVALID_STATE;
    }
    if (!app_audio_mic_available()) {
        ESP_LOGE(TAG, "mic not available");
        return ESP_ERR_NOT_SUPPORTED;
    }
    xEventGroupClearBits(s_evg, EVT_STOP_MIC);
    xTaskCreatePinnedToCore(mic_task, "ai_mic", 8192, NULL, 6, &s_mic_task, 1);
    return ESP_OK;
}

esp_err_t app_ai_stop_recording(void)
{
    if (s_state != AI_STATE_RECORDING) return ESP_OK;
    xEventGroupSetBits(s_evg, EVT_STOP_MIC);
    return ESP_OK;
}
