#pragma once

#include <kernel/com/sys/interrupt.h>

void syscall_isr(InterruptHandler *isr, Context *ctx);
void syscall_handle_sce(Context *ctx);
void syscall_sce_entry();
