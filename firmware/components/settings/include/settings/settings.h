#pragma once
#include <stdint.h>
#include <stdbool.h>

// Runtime-adjustable device settings, persisted in NVS.
// Default values fall back to Kconfig when NVS has no entry.

typedef struct {
    uint8_t brightness;        // Display backlight 10–100 %
    uint16_t screen_dim_secs;  // 0 = disabled; otherwise seconds until dim
    char ble_name[32];         // BLE advertising name
} device_settings_t;

// Load from NVS (falls back to Kconfig defaults on first boot).
void settings_load(device_settings_t *out);

// Persist current values to NVS.
void settings_save(const device_settings_t *s);
