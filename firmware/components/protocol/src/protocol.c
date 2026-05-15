// firmware/components/protocol/src/protocol.c
#include "protocol/protocol.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

// Forward declaration — defined in proto_claude.c
bool proto_claude_parse(const cJSON *payload, usage_data_t *out);

typedef bool (*parser_fn)(const cJSON *payload, usage_data_t *out);

static const struct { const char *svc; parser_fn fn; } s_parsers[] = {
    { "claude", proto_claude_parse },
    // To add a new service: { "cursor", proto_cursor_parse },
    { NULL, NULL }
};

bool protocol_parse(const char *json, proto_envelope_t *env, usage_data_t *out)
{
    if (!json || !env || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *type_j = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *v_j    = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *svc_j  = cJSON_GetObjectItemCaseSensitive(root, "svc");

    env->version = (v_j && cJSON_IsNumber(v_j)) ? v_j->valueint : 0;

    if (!type_j || !cJSON_IsString(type_j) ||
        strcmp(type_j->valuestring, "data") != 0) {
        env->type = MSG_UNKNOWN;
        cJSON_Delete(root);
        return false;
    }
    env->type = MSG_DATA;

    strlcpy(env->svc,
            (svc_j && cJSON_IsString(svc_j)) ? svc_j->valuestring : "claude",
            sizeof(env->svc));
    strlcpy(out->platform, env->svc, sizeof(out->platform));

    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!payload) {
        cJSON_Delete(root);
        return false;
    }

    bool ok = false;
    for (int i = 0; s_parsers[i].svc != NULL; i++) {
        if (strcmp(env->svc, s_parsers[i].svc) == 0) {
            ok = s_parsers[i].fn(payload, out);
            break;
        }
    }

    cJSON_Delete(root);
    return ok;
}

// ── Static message buffers ────────────────────────────────────────────────────

static char s_ack_buf[64];
static char s_err_buf[128];
static char s_req_buf[80];

const char *protocol_cap(void)
{
    return "{\"type\":\"cap\",\"v\":1,"
           "\"svcs\":[\"claude\"],"
           "\"screens\":[\"usage\",\"ble\"]}\n";
}

const char *protocol_ack(bool ok)
{
    snprintf(s_ack_buf, sizeof(s_ack_buf),
             "{\"type\":\"ack\",\"v\":1,\"ok\":%s}\n",
             ok ? "true" : "false");
    return s_ack_buf;
}

const char *protocol_err(int code, const char *msg)
{
    snprintf(s_err_buf, sizeof(s_err_buf),
             "{\"type\":\"err\",\"v\":1,\"code\":%d,\"msg\":\"%s\"}\n",
             code, msg ? msg : "");
    return s_err_buf;
}

const char *protocol_req(const char *svc)
{
    if (svc && svc[0]) {
        snprintf(s_req_buf, sizeof(s_req_buf),
                 "{\"type\":\"req\",\"v\":1,\"svc\":\"%s\"}\n", svc);
    } else {
        snprintf(s_req_buf, sizeof(s_req_buf),
                 "{\"type\":\"req\",\"v\":1}\n");
    }
    return s_req_buf;
}
