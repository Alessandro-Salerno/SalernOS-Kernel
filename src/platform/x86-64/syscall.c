#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <kernel/platform/syscall.h>
#include <kernel/platform/x86-64/msr.h>

void syscall_isr(InterruptHandler *isr, Context *ctx) {
    (void)isr;

    cpu_get_thread()->lock_depth = 0;
    cpu_enable_interrupts();
    SyscallRet ret = syscall_invoke(ctx->rax, ctx, ctx->rdi, ctx->rsi, ctx->rdx,
                                    ctx->rcx, 0, 0, ctx->rip);

    if (!ret.discarded) {
        ctx->rax = ret.value;
        ctx->rdx = ret.err;
    }

    assert(cpu_get_thread()->lock_depth == 0);
    cpu_disable_interrupts();
    cpu_get_thread()->lock_depth = 1;
}

void syscall_handle_sce(Context *ctx) {
    Thread *curr_thread = cpu_get_thread();
    curr_thread->lock_depth = 0;
    cpu_enable_interrupts();
    SyscallRet ret = syscall_invoke(ctx->rax, ctx, ctx->rdi, ctx->rsi, ctx->rdx,
                                    ctx->r10, ctx->r8, ctx->r9, ctx->rip);

    if (!ret.discarded) {
        ctx->rax = ret.value;
        ctx->rdx = ret.err;
    }

    signal_dispatch(ctx, curr_thread);

    // Here we set lock_depth = 1 because we want interrupts to be disabled
    // while we return from the syscall and this ensures that, regardless of
    // whether the caller makes use of spinlocks or not, interrupt won't be
    // enabled by mistake
    assert(curr_thread->lock_depth == 0);
    cpu_disable_interrupts();
    curr_thread->lock_depth = 1;
}

SYSCALL(syscall_set_tls) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    uptr ptr = SYSCALL_ARG(uptr, 1);

    // wrmsr faults on a non-canonical address
    if (!MMU_ADDR_IS_USER(ptr)) return SYSCALL_ERR(EINVAL);

    cpu_get_thread()->xctx.fsbase = ptr;
    msr_write(MSR_FSBASE, ptr);

    return SYSCALL_OK(0);
}
