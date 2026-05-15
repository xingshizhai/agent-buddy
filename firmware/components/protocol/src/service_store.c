#include "protocol/usage_data.h"
#include <string.h>

void store_init(service_store_t *s)
{
    memset(s, 0, sizeof(*s));
}

usage_data_t *store_get(service_store_t *s, const char *svc)
{
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->slots[i].platform, svc) == 0)
            return &s->slots[i];
    }
    return NULL;
}

usage_data_t *store_set(service_store_t *s, const usage_data_t *d)
{
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->slots[i].platform, d->platform) == 0) {
            s->slots[i] = *d;
            return &s->slots[i];
        }
    }
    if (s->count >= PROTO_MAX_SERVICES)
        return NULL;
    s->slots[s->count] = *d;
    return &s->slots[s->count++];
}
