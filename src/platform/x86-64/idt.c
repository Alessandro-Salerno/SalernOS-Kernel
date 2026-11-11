#include <arch/context.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/util.h>
#include <kernel/platform/x86-64/idt.h>

typedef struct {
    u16 size;
    u64 offset;
} packed IDTRegister;

typedef struct {
    u16 offset;
    u16 segment;
    u8 ist;
    u8 flags;
    u16 offset2;
    u32 offset3;
    u32 reserved;
} packed IDTEntry;

static IDTEntry g_idt[256];
static IDTRegister g_idtr = {.size = sizeof(g_idt) - 1, .offset = (u64)g_idt};
extern const u64 g_isr_table[256];

static char *g_exceptions[] = {"division by 0",
                               "debug",
                               "mmi",
                               "breakpoint",
                               "overflow",
                               "bound range exceeded",
                               "invalid opcode",
                               "device not available",
                               "double fault",
                               "coprocessor segment overrun",
                               "invalid tss",
                               "segment not present",
                               "stack-segment fault",
                               "general protection fault",
                               "page fault",
                               "unknown",
                               "x87 floating-point exception",
                               "alignment check",
                               "machine check",
                               "simd exception"};

void idt_exception_isr(InterruptHandler *isr, Context *ctx) {
    if (isr->vec < 19) panic(ctx, "cpu exception (%s)", g_exceptions[isr->vec]);

    panic(ctx, "cpu exception (unknown)");
}

void idt_init() {
    log("initializing idt");

    for (u64 i = 0; i < 256; i++) {
        g_idt[i].offset = g_isr_table[i] & 0xffff;
        g_idt[i].segment = 0x08; // 64-bit GDT selector for kernel code
        g_idt[i].ist = 0;
        g_idt[i].flags = 0x8E;
        g_idt[i].offset2 = (g_isr_table[i] >> 16) & 0xffff;
        g_idt[i].offset3 = (g_isr_table[i] >> 32) & 0xffffffff;
    }
}

void idt_set_user_invocable(u64 vec) {
    log_debug("allowing user invocation of interrupt %zu", vec);
    g_idt[vec].flags = 0xEE;
}

void idt_reload() {
    log("reloading idt");
    asm volatile("lidt (%%rax)" : : "a"(&g_idtr));
}

void idt_stub(void) {
    for (u64 i = 0; i < 32; i++) {
        interrupt_register(i, idt_exception_isr, NULL);
    }
}
