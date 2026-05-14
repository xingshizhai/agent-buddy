#include "cJSON.h"
#include "protocol/usage_data.h"
#include <string.h>
#include <stdbool.h>

bool proto_claude_parse(const char *json, usage_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *s  = cJSON_GetObjectItemCaseSensitive(root, "s");
    cJSON *sr = cJSON_GetObjectItemCaseSensitive(root, "sr");
    cJSON *w  = cJSON_GetObjectItemCaseSensitive(root, "w");
    cJSON *wr = cJSON_GetObjectItemCaseSensitive(root, "wr");
    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "st");
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    cJSON *pl = cJSON_GetObjectItemCaseSensitive(root, "platform");

    strlcpy(out->platform,
            (pl && cJSON_IsString(pl)) ? pl->valuestring : "claude",
            sizeof(out->platform));

    out->session_pct        = (s  && cJSON_IsNumber(s))  ? (float)s->valuedouble  : 0.0f;
    out->session_reset_mins = (sr && cJSON_IsNumber(sr)) ? sr->valueint            : -1;
    out->weekly_pct         = (w  && cJSON_IsNumber(w))  ? (float)w->valuedouble  : 0.0f;
    out->weekly_reset_mins  = (wr && cJSON_IsNumber(wr)) ? wr->valueint            : -1;

    strlcpy(out->status,
            (st && cJSON_IsString(st)) ? st->valuestring : "unknown",
            sizeof(out->status));

    out->ok    = cJSON_IsTrue(ok);
    out->valid = out->ok;

    cJSON_Delete(root);
    return out->ok;
}
