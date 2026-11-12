#pragma once

#include <arch/context.h>
#include <kernel/com/sync/waitlist.h>
#include <kernel/com/util.h>

#define INTERRUPT_FLAGS_EOI_AFTER    (1 << 8)
#define INTERRUPT_FLAGS_NO_RESET     (1 << 9)
#define INTERRUPT_FLAGS_SYNCHRONIZED (1 << 10)

typedef struct InterruptHandler InterruptHandler;
typedef void (*InterruptHandlerFn)(InterruptHandler *isr, Context *ctx);
typedef void (*InterruptAckHandlerFn)(InterruptHandler *isr);

typedef struct InterruptHandler {
    InterruptHandlerFn func;
    InterruptAckHandlerFn eoi;
    u64 vec;
    void *extra;
    int flags;
    bool taken;
    Spinlock dispatch_lock;
    Waitlist idle;
    usize active;
    bool disabled;
} InterruptHandler;

bool interrupt_set(bool status);
InterruptHandler *interrupt_register(u64 vec, InterruptHandlerFn func,
                                     InterruptAckHandlerFn eoi);
InterruptHandler *interrupt_allocate(InterruptHandlerFn func,
                                     InterruptAckHandlerFn eoi);
void interrupt_free(InterruptHandler *isr);
void interrupt_synchronize(InterruptHandler *isr);
void interrupt_isr(u64 vec, Context *ctx);
