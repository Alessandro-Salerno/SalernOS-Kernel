#include <arch/cpu.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/io/log.h>
#include <kernel/com/util.h>
#include <kernel/platform/x86-64/ist.h>
#include <printf.h>

static u64 g_gdt_template[7] = {
    0x0000000000000000, // 0x00 null
    0x00af9b000000ffff, // 0x08 64-bit kernel code
    0x00af93000000ffff, // 0x10 64-bit kernel data
    0x00aff3000000ffff, // 0x18 64-bit user data
    0x00affb000000ffff, // 0x20 64-bit user code
    0x0020890000000000, // 0x28 low InterruptStackTable
    0x0000000000000000  // 0x30 high ist (null?)
};

typedef struct {
    u16 size;
    u64 address;
} packed GDTRegister;

void gdt_init(void) {
    log_debug("copying gdt  to cpu struct");
    Cpu *cur_cpu = cpu_get();

    cur_cpu->gdt[0] = g_gdt_template[0];
    cur_cpu->gdt[1] = g_gdt_template[1];
    cur_cpu->gdt[2] = g_gdt_template[2];
    cur_cpu->gdt[3] = g_gdt_template[3];
    cur_cpu->gdt[4] = g_gdt_template[4];
    cur_cpu->gdt[5] = g_gdt_template[5];
    cur_cpu->gdt[6] = g_gdt_template[6];

    log_debug("applying changes to gdt template");
    uptr ist_addr = (uptr)&cpu_get()->ist;

    // No I/O permission bitmap, user code cannot access any port
    cpu_get()->ist.iopb = sizeof(InterruptStackTable);

    // Add InterruptStackTable to GDT
    cur_cpu->gdt[5] |= ((ist_addr & 0xff000000) << 32) |
        ((ist_addr & 0xff0000) << 16) | ((ist_addr & 0xffff) << 16) |
        sizeof(InterruptStackTable);
    cur_cpu->gdt[6] = (ist_addr >> 32) & 0xffffffff;

    GDTRegister gdtr = {.size = sizeof(g_gdt_template) - 1,
                        .address = (uptr)&cpu_get()->gdt};

    log("loading gdt");
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
