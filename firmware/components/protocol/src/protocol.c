#include "protocol/protocol.h"
#include "protocol/usage_data.h"
#include <string.h>

bool proto_claude_parse(const char *json, usage_data_t *out);

bool protocol_parse(const char *json, usage_data_t *out)
{
    if (!json || !out) return false;

    if (strstr(json, "\"platform\":\"claude\"") || !strstr(json, "\"platform\"")) {
        return proto_claude_parse(json, out);
    }
    return false;
}

const char *protocol_ack(void)  { return "{\"ack\":true}\n"; }
const char *protocol_nack(void) { return "{\"ack\":false}\n"; }
