#pragma once

#include <kernel/com/sys/interrupt.h>
#include <kernel/com/util.h>

noreturn void idt_exception_isr(InterruptHandler *isr, Context *ctx);
void idt_set_user_invocable(u64 vec);
void idt_init();
void idt_reload();
void idt_stub(void);
