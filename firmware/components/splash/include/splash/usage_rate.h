#pragma once

void usage_rate_sample(float session_pct);
int  usage_rate_group(void);   // 0=idle 1=normal 2=active 3=heavy
void usage_rate_reset(void);
