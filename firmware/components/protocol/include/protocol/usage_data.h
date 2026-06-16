#pragma once
#include <stdbool.h>

typedef struct {
    char  platform[16];        // "claude", "cursor", etc.
    float session_pct;         // 5h-window utilization 0–100
    int   session_reset_mins;  // minutes until session window resets (-1 = unknown)
    float weekly_pct;          // 7d-window utilization 0–100
    int   weekly_reset_mins;   // minutes until weekly window resets (-1 = unknown)
    char  status[16];          // "allowed" or "limited"
    bool  ok;
    bool  valid;
} usage_data_t;

// ── Multi-service store ───────────────────────────────────────────────────────

#define PROTO_MAX_SERVICES 4

typedef struct {
    usage_data_t slots[PROTO_MAX_SERVICES];
    int          count;
} service_store_t;

void          store_init(service_store_t *s);
usage_data_t *store_get(service_store_t *s, const char *svc);
usage_data_t *store_set(service_store_t *s, const usage_data_t *d);
