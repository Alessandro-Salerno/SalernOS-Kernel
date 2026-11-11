#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <kernel/platform/x86-64/context.h>
#include <vendor/printf.h>

void context_print(Context *ctx) {
    printf("\ncr2: %p  gs: %p\nrax: %p  fs: %p\nrbx: %p  es: "
           "%p\nrcx: %p  ds: %p\nrdx: %p  cs: %p\n r8: %p  ss: "
           "%p\n r9: %p r10: %p\nr11: %p r12: %p\nr13: %p "
           "r14: %p\nr15: %p rdi: %p\nrsi: %p rbp: %p\nrip: "
           "%p rsp: %p\nerr: %p rfl: %p\n\n",
           ctx->cr2, ctx->gs, ctx->rax, ctx->fs, ctx->rbx, ctx->es, ctx->rcx,
           ctx->ds, ctx->rdx, ctx->cs, ctx->r8, ctx->ss, ctx->r9, ctx->r10,
           ctx->r11, ctx->r12, ctx->r13, ctx->r14, ctx->r15, ctx->rdi, ctx->rsi,
           ctx->rbp, ctx->rip, ctx->rsp, ctx->error, ctx->rflags);

    // printf("instruction at (%%rip):\n\t");
    // printf("%p %p\n", *(u64 *)ctx->rip, *(((u64 *)ctx->rip) + 1));
}

// The FPU state goes right after the frame, aligned for fxsave/fxrstor
#define SIGFRAME_FPU_OFFSET ((sizeof(SigFrame) + 15) & ~(usize)15)
#define SIGFRAME_SIZE       (SIGFRAME_FPU_OFFSET + sizeof(struct _fpstate))

int context_alloc_sigframe(uptr *stackptr, Context *ctx,
                           SigFrame **out_sframe) {
    u64 stack = (u64)ctx->rsp;
    stack -= 128;
    stack &= ~0xFL;
    stack -= SIGFRAME_SIZE;
    *out_sframe = (void *)stack;
    *stackptr = stack;

    return MMU_RANGE_IS_USER(*out_sframe, SIGFRAME_SIZE) ? 0 : EFAULT;
}

int context_setup_sigframe(SigFrame *sframe, SigFrame *uframe, Context *ctx) {
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R8] = ctx->r8;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R9] = ctx->r9;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R10] = ctx->r10;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R11] = ctx->r11;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R12] = ctx->r12;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R13] = ctx->r13;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R14] = ctx->r14;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R15] = ctx->r15;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RDI] = ctx->rdi;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RSI] = ctx->rsi;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RBP] = ctx->rbp;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RBX] = ctx->rbx;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RDX] = ctx->rdx;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RAX] = ctx->rax;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RCX] = ctx->rcx;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RSP] = ctx->rsp;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RIP] = ctx->rip;
    sframe->uc.uc_mcontext.gregs[CONTEXT_REG_EFL] = ctx->rflags;

    struct _fpstate *ufpu = (void *)((uptr)uframe + SIGFRAME_FPU_OFFSET);
    sframe->uc.uc_mcontext.fpregs = ufpu;
    // A switch during the copy saves the same live state again
    ContextExtra *xctx = &cpu_get_thread()->xctx;
    CONTEXT_SAVE_EXTRA(*xctx);
    return memcpy_to_user(ufpu, xctx->fpu, sizeof(xctx->fpu));
}

int context_setup_sigrestore(uptr *stackptr, void *restorer) {
    u64 stack = *stackptr - 8;
    int ret = memcpy_to_user((void *)stack, &restorer, sizeof(restorer));
    if (ret == 0) *stackptr = stack;
    return ret;
}

void context_signal_trampoline(SigFrame *sframe, Context *ctx, uptr stack,
                               void *handler, int sig) {
    ctx->rip = (u64)handler;
    ctx->rsp = (u64)stack;
    ctx->rdi = sig;
    ctx->rsi = (u64)(&sframe->info);
    ctx->rdx = (u64)(&sframe->uc);

    u32 mxcsr = 0x1f80;
    asm volatile("fninit\n\tldmxcsr %0" : : "m"(mxcsr));
}

int context_restore_sigframe(SigFrame *sframe, Context *ctx) {
    SigFrame *uframe = (void *)ctx->rsp;
    int ret = memcpy_from_user(sframe, uframe, sizeof(*sframe));
    if (ret != 0) return ret;

    struct _fpstate *ufpu = sframe->uc.uc_mcontext.fpregs;
    if (ufpu == NULL) return EFAULT;
    // Returning to a non-canonical rip faults in ring 0
    if (!MMU_ADDR_IS_USER(sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RIP])) {
        return EFAULT;
    }
    // Reserved MXCSR bits would make fxrstor fault
    struct _fpstate fpu align(16);
    asm volatile("fxsave %0" : "=m"(fpu));
    u32 mxcsr_mask = (fpu.mxcr_mask != 0) ? fpu.mxcr_mask : 0xffbf;
    ret = memcpy_from_user(&fpu, ufpu, sizeof(fpu));
    if (ret != 0) return ret;
    fpu.mxcsr &= mxcsr_mask;
    asm volatile("fxrstor %0" : : "m"(fpu));

    ctx->r8 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R8];
    ctx->r9 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R9];
    ctx->r10 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R10];
    ctx->r11 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R11];
    ctx->r12 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R12];
    ctx->r13 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R13];
    ctx->r14 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R14];
    ctx->r15 = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_R15];
    ctx->rdi = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RDI];
    ctx->rsi = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RSI];
    ctx->rbp = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RBP];
    ctx->rbx = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RBX];
    ctx->rdx = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RDX];
    ctx->rax = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RAX];
    ctx->rcx = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RCX];
    ctx->rsp = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RSP];
    ctx->rip = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_RIP];
    ctx->rflags = sframe->uc.uc_mcontext.gregs[CONTEXT_REG_EFL];

    // Do not let user code set privileged flags (e.g., IOPL)
    ctx->rflags = (ctx->rflags & 0x40DD5) | 0x202;
    return 0;
}

void ctx_prepare(Context *ctx, Context *frame) {
    uptr base = ((uptr)frame - sizeof(ContextMessage) - sizeof(frame)) &
        ~(uptr)15;
    *(Context **)(base + sizeof(ContextMessage)) = frame;
    uptr *stack = (void *)base;
    *--stack = (uptr)context_fork_trampoline;
    *--stack = base;
    ctx->rsp = (uptr)stack;
}

void ctx_first_entry(ContextMessage *message, Context *frame) {
    sched_switch_finish(*message);
    context_trampoline(frame);
    __builtin_unreachable();
}
