#pragma once

#include <kernel/com/sys/interrupt.h>
#include <kernel/com/util.h>

#define LAPIC_TIMER_INTERRUPT 0x30

void lapic_eoi(InterruptHandler *isr);
void lapic_bsp_init(void);
void lapic_init(void);
void lapic_selfipi(u8 vector);
void lapic_send_ipi(u8 apic_id, u8 vector);
void lapic_broadcast_ipi(u8 vector);
