#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    AI_STATE_DISCONNECTED = 0,
    AI_STATE_CONNECTING,
    AI_STATE_IDLE,
    AI_STATE_RECORDING,
    AI_STATE_PROCESSING,
    AI_STATE_PLAYING,
} ai_state_t;

typedef void (*ai_state_cb_t)(ai_state_t state);
typedef void (*ai_text_cb_t)(const char *text);   // transcript or response

// Initialize WebSocket client and register callbacks.
// Call after WiFi is connected.
esp_err_t app_ai_init(void);

// Start/stop mic recording and stream to server.
esp_err_t app_ai_start_recording(void);
esp_err_t app_ai_stop_recording(void);

ai_state_t app_ai_get_state(void);

// Callbacks are called from the LVGL task (via lv_async_call).
void app_ai_set_state_cb(ai_state_cb_t cb);
void app_ai_set_transcript_cb(ai_text_cb_t cb);   // ASR result
void app_ai_set_response_cb(ai_text_cb_t cb);     // LLM response
void app_ai_set_status_cb(ai_text_cb_t cb);       // status string
