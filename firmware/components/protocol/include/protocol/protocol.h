#pragma once
#include "usage_data.h"
#include <stdbool.h>

// ── Incoming message envelope ─────────────────────────────────────────────────

typedef enum {
    MSG_DATA    = 0,   // "type":"data" — service data from PC
    MSG_UNKNOWN = 1,
} proto_msg_type_t;

typedef struct {
    proto_msg_type_t type;
    char             svc[16];   // service id, e.g. "claude"
    int              version;   // protocol version
} proto_envelope_t;

// Parse a JSON string received on the RX characteristic.
// Returns true and populates *env and *out on success.
// Returns false if the message is malformed, unknown type, or unsupported svc.
bool protocol_parse(const char *json, proto_envelope_t *env, usage_data_t *out);

// ── Outgoing message builders ─────────────────────────────────────────────────
// All functions return a pointer to an internal static buffer — use immediately,
// do not hold across calls.

// {"type":"cap","v":1,"svcs":["claude"],"screens":["usage","ble"]}
const char *protocol_cap(void);

// {"type":"ack","v":1,"ok":true/false}
const char *protocol_ack(bool ok);

// {"type":"err","v":1,"code":<code>,"msg":"<msg>"}
const char *protocol_err(int code, const char *msg);

// {"type":"req","v":1,"svc":"<svc>"}  (svc=NULL → omit svc field, refresh all)
const char *protocol_req(const char *svc);
