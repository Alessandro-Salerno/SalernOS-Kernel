#pragma once

#include <arch/context.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/util.h>

void context_print(Context *ctx);
void context_trampoline(Context *ctx);
void context_fork_trampoline(void);

int context_alloc_sigframe(uptr *stackptr, Context *ctxptr,
                           SigFrame **out_sframe);
int context_setup_sigframe(SigFrame *kframe, SigFrame *uframe, Context *ctx);
int context_setup_sigrestore(uptr *stackptr, void *restorer);
void context_signal_trampoline(SigFrame *sframe, Context *ctx, uptr stack,
                               void *handler, int sig);
int context_restore_sigframe(SigFrame *sframe, Context *ctx);
