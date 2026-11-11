#pragma once

#include <kernel/com/crt/string.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>
#include <kernel/platform/x86-64/context.h>
#include <kernel/platform/x86-64/msr.h>

#define CONTEXT_THREAD_SET(src, stack, stack_size, entry) \
    (src).cs = 0x20 | 3;                                  \
    (src).ss = 0x18 | 3;                                  \
    (src).rsp = (u64)(stack) + (stack_size);              \
    (src).rip = (u64)(entry);                             \
    (src).rflags = (1ul << 1) | (1ul << 9) | (1ul << 21);

#define CONTEXT_THREAD_SET_KERNEL(src, stack_end, entry, arg) \
    (src).cs = 0x08;                                          \
    (src).ss = 0x10;                                          \
    (src).rip = (u64)(entry);                                 \
    (src).rdi = (u64)(arg);                                   \
    (src).rsp = (u64)(stack_end) - 8;                         \
    (src).rflags = (1ul << 1) | (1ul << 9) | (1ul << 21);     \
    (src).rsp -= sizeof(Context);                             \
    {                                                         \
        Context *new_context = (void *)(src).rsp;             \
        *new_context = (src);                                 \
        ctx_prepare(&(src), new_context);                     \
    }

#define CONTEXT_FORK(new_thread, orig_ctx)                    \
    new_thread->ctx.rsp = (u64)new_thread->kernel_stack;      \
    new_thread->ctx.rsp -= sizeof(Context);                   \
    {                                                         \
        Context *new_context = (void *)new_thread->ctx.rsp;   \
        *new_context = (orig_ctx); /* copy the old context */ \
        ctx_prepare(&new_thread->ctx, new_context);           \
        new_context->rax = 0;                                 \
        new_context->rdx = 0;                                 \
    }

#define CONTEXT_CLONE(new_thread, new_ip, new_sp)              \
    new_thread->ctx.rsp = (u64)new_thread->kernel_stack;       \
    new_thread->ctx.rsp -= sizeof(Context);                    \
    {                                                          \
        Context *new_context = (void *)new_thread->ctx.rsp;    \
        *new_context = (Context){0};                           \
        ctx_prepare(&new_thread->ctx, new_context);            \
        CONTEXT_THREAD_SET((*new_context), new_sp, 0, new_ip); \
    }

#define CONTEXT_RESUME_SAVED(ctx, saved, ret) \
    *(ctx) = *(saved);                        \
    (ctx)->rax = (u64)(ret)

#define CONTEXT_RESTORE_TLC(ctx) msr_write(MSR_KERNELGSBASE, (u64)(ctx)->gs)

#define CONTEXT_SAVE_EXTRA(xctx) \
    asm volatile("fxsave (%0)" : : "r"(&((xctx).fpu)) : "memory")

#define CONTEXT_RESTORE_EXTRA(xctx)                                 \
    asm volatile("fxrstor (%0)" : : "r"(&((xctx).fpu)) : "memory"); \
    msr_write(MSR_FSBASE, (xctx).fsbase);

#define CONTEXT_INIT_EXTRA(xctx)                          \
    {                                                     \
        memset((xctx).fpu, 0, sizeof((xctx).fpu));        \
        u16 fcw = 0x037f;                                 \
        u16 ftw = 0xffff;                                 \
        u32 mxcsr = 0x1f80;                               \
        memcpy((xctx).fpu + 0x00, &fcw, sizeof(fcw));     \
        memcpy((xctx).fpu + 0x08, &ftw, sizeof(ftw));     \
        memcpy((xctx).fpu + 0x18, &mxcsr, sizeof(mxcsr)); \
        (xctx).fsbase = 0;                                \
        (xctx).gsbase = 0;                                \
    }

#define CONTEXT_FORK_EXTRA(child_xctx)              \
    {                                               \
        CONTEXT_SAVE_EXTRA(child_xctx);             \
        (child_xctx).fsbase = msr_read(MSR_FSBASE); \
    }

typedef struct {
    u64 cr2;
    u64 gs;
    u64 fs;
    u64 es;
    u64 ds;
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rdi;
    u64 rsi;
    u64 rbp;
    u64 error;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} packed Context;

typedef struct {
    u8 fpu[512] align(16);
    u64 fsbase;
    u64 gsbase;
} packed ContextExtra;

// A three-word return uses the SysV hidden result pointer. The switch saves
// that pointer on the outgoing stack and fills the incoming caller's result
typedef struct ContextMessage {
    void (*callback)(void *, uptr, bool success);
    void *arg;
    uptr value;
} ContextMessage;

ContextMessage ctx_switch(Context *to, Context *from,
                          void (*callback)(void *, uptr, bool), void *arg,
                          uptr value);
int ctx_save_and_call(void (*fn)(Context *, void *), void *arg);
void ctx_prepare(Context *ctx, Context *frame);
void ctx_test_trampoline(void);

static inline bool context_intstatus(const Context *ctx) {
    return ctx->rflags & 0x200;
}

static inline bool context_is_user(const Context *ctx) {
    return ctx->cs == 0x23;
}
