#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/util.h>
#include <kernel/opt/acpi.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/ioapic.h>
#include <kernel/platform/x86-64/lapic.h>
#include <kernel/platform/x86-64/smp.h>

struct UACPIIOMap {
    UACPIIOAddr base;
    UACPISize length;
};

int arch_uacpi_io_map(UACPIIOAddr base, UACPISize len,
                      UACPIHandle *out_handle) {
    *out_handle = NULL;
    if (len == 0 || base > 0xffff || len > 0x10000 - base) return EINVAL;
    struct UACPIIOMap *map = slab_alloc(sizeof(*map));
    if (map == NULL) return ENOMEM;
    map->base = base;
    map->length = len;
    *out_handle = map;
    return 0;
}

void arch_uacpi_io_unmap(UACPIHandle handle) {
    slab_free(handle, sizeof(struct UACPIIOMap));
}

static bool io_access_valid(struct UACPIIOMap *map, UACPISize offset,
                            UACPISize width) {
    return map != NULL && width <= map->length && offset <= map->length - width;
}

int arch_uacpi_io_read8(UACPIHandle handle, UACPISize offset,
                        UACPIUint8 *out_value) {
    struct UACPIIOMap *map = handle;
    if (!io_access_valid(map, offset, 1)) return EINVAL;
    *out_value = io_inb(map->base + offset);
    return 0;
}

int arch_uacpi_io_write8(UACPIHandle handle, UACPISize offset,
                         UACPIUint8 value) {
    struct UACPIIOMap *map = handle;
    if (!io_access_valid(map, offset, 1)) return EINVAL;
    io_outb(map->base + offset, value);
    return 0;
}

int arch_uacpi_io_read16(UACPIHandle handle, UACPISize offset,
                         UACPIUint16 *out_value) {
    struct UACPIIOMap *map = handle;
    if (!io_access_valid(map, offset, 2)) return EINVAL;
    *out_value = io_inw(map->base + offset);
    return 0;
}

int arch_uacpi_io_write16(UACPIHandle handle, UACPISize offset,
                          UACPIUint16 value) {
    struct UACPIIOMap *map = handle;
    if (!io_access_valid(map, offset, 2)) return EINVAL;
    io_outw(map->base + offset, value);
    return 0;
}

int arch_uacpi_io_read32(UACPIHandle handle, UACPISize offset,
                         UACPIUint32 *out_value) {
    struct UACPIIOMap *map = handle;
    if (!io_access_valid(map, offset, 4)) return EINVAL;
    *out_value = io_ind(map->base + offset);
    return 0;
}

int arch_uacpi_io_write32(UACPIHandle handle, UACPISize offset,
                          UACPIUint32 value) {
    struct UACPIIOMap *map = handle;
    if (!io_access_valid(map, offset, 4)) return EINVAL;
    io_outd(map->base + offset, value);
    return 0;
}

int arch_uacpi_set_irq(UACPIUint32 irq, InterruptHandler *isr) {
    return ioapic_set_irq(irq, isr->vec & 0xff, cpu_get_hardware_id(), false);
}

int arch_uacpi_mask_irq(UACPIUint32 irq) { return ioapic_mask_irq(irq); }

Cpu *arch_uacpi_work_cpu(void) {
    Cpu *cpu = smp_get_cpu(0);
    return cpu != NULL ? cpu : cpu_get();
}

void arch_uacpi_irq_eoi(InterruptHandler *isr) { lapic_eoi(isr); }

int arch_uacpi_early_init(void) {
    ioapic_init();
    acpi_init_pci();
    return 0;
}
