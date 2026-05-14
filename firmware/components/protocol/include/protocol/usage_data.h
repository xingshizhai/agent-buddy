#pragma once
#include <stdbool.h>

typedef struct {
    char  platform[16];        // "claude", "openai", etc. — defaults to "claude"
    float session_pct;         // 5h-window utilization 0–100
    int   session_reset_mins;  // minutes until session window resets (-1 = unknown)
    float weekly_pct;          // 7d-window utilization 0–100
    int   weekly_reset_mins;   // minutes until weekly window resets (-1 = unknown)
    char  status[16];          // "allowed" or "limited"
    bool  ok;                  // true when API call succeeded
    bool  valid;               // false until first successful parse
} usage_data_t;
