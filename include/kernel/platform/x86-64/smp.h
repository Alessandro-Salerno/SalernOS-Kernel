#pragma once

#include <arch/cpu.h>
#include <kernel/com/util.h>

void smp_init(void);
Cpu *smp_get_random();
Cpu *smp_get_cpu(usize cpu);
