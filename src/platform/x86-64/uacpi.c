#include <arch/cpu.h>
#include <kernel/com/util.h>
#include <kernel/opt/acpi.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/ioapic.h>
#include <kernel/platform/x86-64/lapic.h>

int arch_uacpi_io_map(UACPIIOAddr base, UACPISize len,
                      UACPIHandle *out_handle) {
    (void)len;
    *out_handle = (UACPIHandle)base;
    return 0;
}

void arch_uacpi_io_unmap(UACPIHandle handle) { (void)handle; }

int arch_uacpi_io_read8(UACPIHandle handle, UACPISize offset,
                        UACPIUint8 *out_value) {
    *out_value = io_inb((UACPIIOAddr)handle + offset);
    return 0;
}

int arch_uacpi_io_read16(UACPIHandle handle, UACPISize offset,
                         UACPIUint16 *out_value) {
    *out_value = io_inw((UACPIIOAddr)handle + offset);
    return 0;
}

int arch_uacpi_io_read32(UACPIHandle handle, UACPISize offset,
                         UACPIUint32 *out_value) {
    *out_value = io_ind((UACPIIOAddr)handle + offset);
    return 0;
}

int arch_uacpi_io_write8(UACPIHandle handle, UACPISize offset,
                         UACPIUint8 value) {
    io_outb((UACPIIOAddr)handle + offset, value);
    return 0;
}

int arch_uacpi_io_write16(UACPIHandle handle, UACPISize offset,
                          UACPIUint16 value) {
    io_outw((UACPIIOAddr)handle + offset, value);
    return 0;
}

int arch_uacpi_io_write32(UACPIHandle handle, UACPISize offset,
                          UACPIUint32 value) {
    io_outd((UACPIIOAddr)handle + offset, value);
    return 0;
}

int arch_uacpi_set_irq(UACPIUint32 irq, InterruptHandler *isr) {
    ioapic_set_irq(irq, isr->vec & 0xff, cpu_get_hardware_id(), false);
    return 0;
}

void arch_uacpi_irq_eoi(InterruptHandler *isr) { lapic_eoi(isr); }

int arch_uacpi_early_init(void) {
    ioapic_init();
    acpi_init_pci();
    return 0;
}
