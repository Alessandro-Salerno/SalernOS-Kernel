#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/io/log.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <vendor/printf.h>

static Spinlock g_interrupt_table_lock = SPINLOCK_NEW();
static InterruptHandler g_interrupt_table[NUM_INTERRUPTS] = {0};

static InterruptHandler *interrupt_register_nolock(u64 vec,
                                                   InterruptHandlerFn func,
                                                   InterruptAckHandlerFn eoi) {
    log_debug("registering handler at %p for vector %zu (%p)", func, vec, vec);
    InterruptHandler *isr = &g_interrupt_table[vec];
    spinlock_acq(&isr->dispatch_lock);
    assert(isr->active == 0);
    waitlist_init(&isr->idle);
    isr->active = 0;
    isr->disabled = false;
    isr->flags = 0;
    isr->extra = NULL;
    isr->func = func;
    isr->eoi = eoi;
    isr->vec = vec;
    isr->taken = true;
    spinlock_rel(&isr->dispatch_lock);
    return isr;
}

InterruptHandler *interrupt_register(u64 vec, InterruptHandlerFn func,
                                     InterruptAckHandlerFn eoi) {
    spinlock_acq(&g_interrupt_table_lock);
    InterruptHandler *ret = interrupt_register_nolock(vec, func, eoi);
    spinlock_rel(&g_interrupt_table_lock);
    return ret;
}

InterruptHandler *interrupt_allocate(InterruptHandlerFn func,
                                     InterruptAckHandlerFn eoi) {
    spinlock_acq(&g_interrupt_table_lock);
    InterruptHandler *ret = NULL;

    for (u64 i = 0; i < NUM_INTERRUPTS; i++) {
        InterruptHandler *isr = &g_interrupt_table[i];
        if (!isr->taken) {
            interrupt_register_nolock(i, func, eoi);
            ret = isr;
            break;
        }
    }

    spinlock_rel(&g_interrupt_table_lock);
    return ret;
}

void interrupt_free(InterruptHandler *isr) {
    spinlock_acq(&isr->dispatch_lock);
    isr->disabled = true;
    while (isr->active != 0) {
        spinlock_wait(&isr->dispatch_lock, &isr->idle, 0, false);
    }
    isr->func = NULL;
    isr->extra = NULL;
    spinlock_rel(&isr->dispatch_lock);
    spinlock_acq(&g_interrupt_table_lock);
    isr->taken = false;
    spinlock_rel(&g_interrupt_table_lock);
}

void interrupt_synchronize(InterruptHandler *isr) {
    spinlock_acq(&isr->dispatch_lock);
    while (isr->active != 0) {
        spinlock_wait(&isr->dispatch_lock, &isr->idle, 0, false);
    }
    spinlock_rel(&isr->dispatch_lock);
}

void interrupt_isr(u64 vec, Context *ctx) {
    Thread *curr_thread = cpu_get_thread();
    InterruptHandler *isr = &g_interrupt_table[vec];
    bool eoi_after = isr->flags & INTERRUPT_FLAGS_EOI_AFTER;
    bool no_reset = isr->flags & INTERRUPT_FLAGS_NO_RESET;

    if (curr_thread != NULL && !no_reset) curr_thread->lock_depth = 1;
    if (isr == NULL) panic(ctx, "isr not set for interrupt vector %zu", vec);

    // The majority of interrupt handlers are not synchronized, but some are so
    // we do this as a fast path. Flags are read-only and we can fetch them
    // easily outside the lock, other flags are not and we use separate fields
    bool synchronized = isr->flags & INTERRUPT_FLAGS_SYNCHRONIZED;
    if (synchronized) spinlock_acq(&isr->dispatch_lock);
    bool dispatch = !synchronized || (isr->taken && !isr->disabled);
    InterruptHandlerFn func = dispatch ? isr->func : NULL;
    InterruptAckHandlerFn eoi = isr->eoi;
    if (synchronized) {
        if (dispatch) isr->active++;
        spinlock_rel(&isr->dispatch_lock);
    }

    if (!eoi_after && eoi != NULL) eoi(isr);
    if (func != NULL) func(isr, ctx);
    if (eoi_after && eoi != NULL) eoi(isr);

    if (synchronized && dispatch) {
        spinlock_acq(&isr->dispatch_lock);
        if (--isr->active == 0) sched_notify_all(&isr->idle);
        spinlock_rel(&isr->dispatch_lock);
    }

    if (context_is_user(ctx)) {
        // NOTE: events that interrupt a user thread are by definition thread
        // context, so this is safe
        int depth = curr_thread->lock_depth;
        assert(depth == 0 || depth == 1);
        curr_thread->lock_depth = 0;
        cpu_enable_interrupts();
        signal_dispatch(ctx, curr_thread);
        cpu_disable_interrupts();
        curr_thread->lock_depth = depth;
    }

    if (curr_thread != NULL && !no_reset) {
        // Here we reset lock_depth = 0 because if we got here, this was a fully
        // async interrupt and thus an event that can only occur in user code or
        // kernel code with lock_depth = 0
        assert(curr_thread == cpu_get_thread());
        assert(curr_thread->tid == cpu_get_thread()->tid);
        assert(curr_thread->lock_depth == 1);
        curr_thread->lock_depth = 0;
    }
}
