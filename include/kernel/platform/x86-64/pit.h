#pragma once

#include <kernel/com/util.h>

#define PIT_FREQUENCY 1193182UL

u64 pit_wait_ticks(u64 ticks);
