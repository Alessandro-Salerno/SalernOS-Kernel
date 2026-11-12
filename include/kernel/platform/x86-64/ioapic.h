#pragma once

#include <kernel/com/util.h>

#define IOAPIC_IRQ_KEYBOARD 1
#define IOAPIC_IRQ_MOUSE    12

void ioapic_init(void);
int ioapic_set_irq(u32 irq, u8 vector, u8 proc, bool masked);
int ioapic_mask_irq(u32 irq);
