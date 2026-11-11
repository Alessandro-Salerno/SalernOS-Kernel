#pragma once

#include <kernel/com/sys/interrupt.h>

#define MMU_FLAGS_COW (1 << 9)

#define MMU_IPI_INVALIDATE 0x34

void mmu_init_cpu(void);
void mmu_fault_isr(InterruptHandler *isr, Context *ctx);
void mmu_invalidate_isr(InterruptHandler *isr, Context *ctx);
