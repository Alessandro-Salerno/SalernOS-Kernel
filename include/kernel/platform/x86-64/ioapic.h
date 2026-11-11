#pragma once

#include <kernel/com/util.h>

#define IOAPIC_IRQ_KEYBOARD 1
#define IOAPIC_IRQ_MOUSE    12

void ioapic_init(void);
void ioapic_set_irq(u8 irq, u8 vector, u8 proc, bool masked);
