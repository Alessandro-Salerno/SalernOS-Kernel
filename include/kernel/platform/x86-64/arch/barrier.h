#pragma once

static inline void barrier_generic_read(void) { asm volatile("" ::: "memory"); }
static inline void barrier_generic_write(void) {
    asm volatile("" ::: "memory");
}
