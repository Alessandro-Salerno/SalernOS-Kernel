/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/cpu.h>
#include <kernel/com/io/log.h>
#include <kernel/platform/x86-64/ist.h>
#include <lib/mem.h>
#include <printf.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t GdtTemplate[7] = {
    0x0000000000000000, // 0x00 null
    0x00af9b000000ffff, // 0x08 64-bit kernel code
    0x00af93000000ffff, // 0x10 64-bit kernel data
    0x00aff3000000ffff, // 0x18 64-bit user data
    0x00affb000000ffff, // 0x20 64-bit user code
    0x0020890000000000, // 0x28 low IST
    0x0000000000000000  // 0x30 high ist (null?)
};

typedef struct {
    uint16_t size;
    uint64_t address;
} __attribute__((packed)) gdtr_t;

void x86_64_gdt_init(void) {
    KLOG("copying gdt template to cpu struct");
    arch_cpu_t *cur_cpu = ARCH_CPU_GET();

    cur_cpu->gdt[0] = GdtTemplate[0];
    cur_cpu->gdt[1] = GdtTemplate[1];
    cur_cpu->gdt[2] = GdtTemplate[2];
    cur_cpu->gdt[3] = GdtTemplate[3];
    cur_cpu->gdt[4] = GdtTemplate[4];
    cur_cpu->gdt[5] = GdtTemplate[5];
    cur_cpu->gdt[6] = GdtTemplate[6];

    KLOG("applying changes to gdt template");
    uintptr_t ist_addr = (uintptr_t)&ARCH_CPU_GET()->ist;

    // Add IST to GDT
    cur_cpu->gdt[5] |= ((ist_addr & 0xff000000) << 32) |
                       ((ist_addr & 0xff0000) << 16) |
                       ((ist_addr & 0xffff) << 16) | sizeof(x86_64_ist_t);
    cur_cpu->gdt[6] = (ist_addr >> 32) & 0xffffffff;

    gdtr_t gdtr = {.size    = sizeof(GdtTemplate) - 1,
                   .address = (uintptr_t)&ARCH_CPU_GET()->gdt};

    KLOG("loading gdt");
    asm volatile("lgdt (%%rax)" : : "a"(&gdtr) : "memory");
    asm volatile("ltr %%ax" : : "a"(0x28));
    asm volatile("swapgs;"
                 "mov $0, %%ax;"
                 "mov %%ax, %%gs;"
                 "mov %%ax, %%fs;"
                 "swapgs;"
                 "mov $0x10, %%ax;"
                 "mov %%ax, %%ds;"
                 "mov %%ax, %%es;"
                 "mov %%ax, %%ss;"
                 "pushq $0x8;"
                 "pushq $.reload;"
                 "retfq;"
                 ".reload:"
                 :
                 :
                 : "ax");
}
