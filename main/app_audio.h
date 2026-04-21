#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stddef.h>

// Initialize I2S + ES8311 speaker + ES7210 mic + PA via TCA9554.
// i2c_bus must already be initialized (shared with LCD).
esp_err_t app_audio_init(i2c_master_bus_handle_t i2c_bus);

// Play PCM samples (16-bit, stereo, sample_rate Hz) — blocking, full buffer
esp_err_t app_audio_play(const int16_t *data, size_t samples, uint32_t sample_rate);

// Chunked playback API for streaming with per-chunk level monitoring
esp_err_t app_audio_play_start(uint32_t sample_rate);
esp_err_t app_audio_play_write(const int16_t *data, size_t samples);
esp_err_t app_audio_play_stop(void);

// Microphone recording
esp_err_t app_audio_record_start(uint32_t sample_rate);
esp_err_t app_audio_record_read(int16_t *buf, size_t buf_samples, size_t *samples_read);
esp_err_t app_audio_record_stop(void);

// Volume 0-100
esp_err_t app_audio_set_volume(int vol);

// Returns true if the microphone codec (ES7210) was successfully initialized
bool app_audio_mic_available(void);
