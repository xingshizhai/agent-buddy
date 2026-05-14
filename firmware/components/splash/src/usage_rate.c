#include "splash/usage_rate.h"
#include "esp_timer.h"
#include <stdint.h>

#define RATE_THRESH_NORMAL  0.10f
#define RATE_THRESH_ACTIVE  0.20f
#define RATE_THRESH_HEAVY   0.33f
#define MIN_WINDOW_US  (240ULL * 1000000ULL)
#define RING_SIZE 6

typedef struct { uint64_t us; float pct; } sample_t;

static sample_t s_ring[RING_SIZE];
static uint8_t  s_count = 0;
static uint8_t  s_head  = 0;

void usage_rate_reset(void)
{
    s_count = 0;
    s_head  = 0;
}

void usage_rate_sample(float session_pct)
{
    uint64_t now = (uint64_t)esp_timer_get_time();

    if (s_count > 0) {
        uint8_t latest = (s_head + RING_SIZE - 1) % RING_SIZE;
        if (session_pct + 5.0f < s_ring[latest].pct) {
            usage_rate_reset();
        }
    }

    s_ring[s_head] = (sample_t){ now, session_pct };
    s_head = (s_head + 1) % RING_SIZE;
    if (s_count < RING_SIZE) s_count++;
}

int usage_rate_group(void)
{
    if (s_count < 2) return 0;

    uint8_t oldest = (s_head + RING_SIZE - s_count) % RING_SIZE;
    uint8_t latest = (s_head + RING_SIZE - 1) % RING_SIZE;

    uint64_t dt = s_ring[latest].us - s_ring[oldest].us;
    if (dt < MIN_WINDOW_US) return 0;

    float dp = s_ring[latest].pct - s_ring[oldest].pct;
    if (dp < 0.0f) dp = 0.0f;
    float rate = dp * 60.0f / ((float)dt / 1000000.0f);

    if (rate < RATE_THRESH_NORMAL) return 0;
    if (rate < RATE_THRESH_ACTIVE) return 1;
    if (rate < RATE_THRESH_HEAVY)  return 2;
    return 3;
}
