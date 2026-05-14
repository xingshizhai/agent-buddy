#pragma once
#include "usage_data.h"
#include <stdbool.h>

bool protocol_parse(const char *json, usage_data_t *out);
const char *protocol_ack(void);
const char *protocol_nack(void);
