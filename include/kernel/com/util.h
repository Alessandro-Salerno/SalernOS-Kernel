#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t usize;
typedef ssize_t isize;
typedef uintptr_t uptr;
typedef intptr_t iptr;

#define used          __attribute__((used))
#define maybe_unused  __attribute__((unused))
#define noreturn      __attribute__((noreturn))
#define noinline      __attribute__((noinline))
#define packed        __attribute__((packed))
#define align(x)      __attribute__((aligned(x)))
#define cache_aligned align(CACHE_ALIGN)
#define user_text     __attribute__((section(".user_text")))
#define user_data     __attribute__((section(".user_data")))
#define likely(x)     __builtin_expect(!!(x), 1)
#define unlikely(x)   __builtin_expect(!!(x), 0)

#define min(x, y)           \
    ({                      \
        typeof(x) _x = (x); \
        typeof(y) _y = (y); \
        _x < _y ? _x : _y;  \
    })

#define max(x, y)           \
    ({                      \
        typeof(x) _x = (x); \
        typeof(y) _y = (y); \
        _x < _y ? _y : _x;  \
    })

#define NANOS_PER_SEC  1000000000UL
#define MICROS_PER_SEC (NANOS_PER_SEC / 1000)

static inline u64 fps_to_ns(u64 fps) { return NANOS_PER_SEC / fps; }

// pre: multiple is a power of 2
static inline usize mod_pow2(usize value, usize multiple) {
    return value & (multiple - 1);
}

#include <arch/info.h>
#include <kernel/com/crt/assert.h>
#include <kernel/com/io/log.h>
