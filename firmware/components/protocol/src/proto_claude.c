#include "cJSON.h"
#include "protocol/usage_data.h"
#include <string.h>
#include <stdbool.h>

bool proto_claude_parse(const cJSON *payload, usage_data_t *out)
{
    if (!payload || !out) return false;

    cJSON *s  = cJSON_GetObjectItemCaseSensitive(payload, "s");
    cJSON *sr = cJSON_GetObjectItemCaseSensitive(payload, "sr");
    cJSON *w  = cJSON_GetObjectItemCaseSensitive(payload, "w");
    cJSON *wr = cJSON_GetObjectItemCaseSensitive(payload, "wr");
    cJSON *st = cJSON_GetObjectItemCaseSensitive(payload, "st");
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(payload, "ok");

    out->session_pct        = (s  && cJSON_IsNumber(s))  ? (float)s->valuedouble  : 0.0f;
    out->session_reset_mins = (sr && cJSON_IsNumber(sr)) ? sr->valueint            : -1;
    out->weekly_pct         = (w  && cJSON_IsNumber(w))  ? (float)w->valuedouble  : 0.0f;
    out->weekly_reset_mins  = (wr && cJSON_IsNumber(wr)) ? wr->valueint            : -1;

    strlcpy(out->status,
            (st && cJSON_IsString(st)) ? st->valuestring : "unknown",
            sizeof(out->status));

    out->ok    = cJSON_IsTrue(ok);
    out->valid = out->ok;

    return out->ok;
}
