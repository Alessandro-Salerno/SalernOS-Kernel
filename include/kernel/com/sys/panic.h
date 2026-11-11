#pragma once

#include <arch/context.h>

noreturn void panic(Context *ctx, const char *fmt, ...);
