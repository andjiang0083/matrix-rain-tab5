// ── Timesync shared declarations ──
#pragma once

#include <time.h>

extern time_t g_epoch;
extern uint32_t g_epochBase;
extern bool g_timeValid;
extern int g_tzOffset;

bool syncNTP();
