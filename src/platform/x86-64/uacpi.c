/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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
#include <kernel/opt/acpi.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/ioapic.h>
#include <kernel/platform/x86-64/lapic.h>
#include <lib/util.h>

int arch_uacpi_io_map(opt_uacpi_handle_t *out_handle,
                      opt_uacpi_io_addr_t base,
                      opt_uacpi_size_t    len) {
    (void)len;
    *out_handle = (opt_uacpi_handle_t)base;
    return 0;
}

void arch_uacpi_io_unmap(opt_uacpi_handle_t handle) {
    (void)handle;
}

int arch_uacpi_io_read8(opt_uacpi_u8_t    *out_value,
                        opt_uacpi_handle_t handle,
                        opt_uacpi_size_t   offset) {
    *out_value = X86_64_IO_INB((opt_uacpi_io_addr_t)handle + offset);
    return 0;
}

int arch_uacpi_io_read16(opt_uacpi_u16_t   *out_value,
                         opt_uacpi_handle_t handle,
                         opt_uacpi_size_t   offset) {
    *out_value = X86_64_IO_INW((opt_uacpi_io_addr_t)handle + offset);
    return 0;
}

int arch_uacpi_io_read32(opt_uacpi_u32_t   *out_value,
                         opt_uacpi_handle_t handle,
                         opt_uacpi_size_t   offset) {
    *out_value = X86_64_IO_IND((opt_uacpi_io_addr_t)handle + offset);
    return 0;
}

int arch_uacpi_io_write8(opt_uacpi_handle_t handle,
                         opt_uacpi_size_t   offset,
                         opt_uacpi_u8_t     value) {
    X86_64_IO_OUTB((opt_uacpi_io_addr_t)handle + offset, value);
    return 0;
}

int arch_uacpi_io_write16(opt_uacpi_handle_t handle,
                          opt_uacpi_size_t   offset,
                          opt_uacpi_u16_t    value) {
    X86_64_IO_OUTW((opt_uacpi_io_addr_t)handle + offset, value);
    return 0;
}

int arch_uacpi_io_write32(opt_uacpi_handle_t handle,
                          opt_uacpi_size_t   offset,
                          opt_uacpi_u32_t    value) {
    X86_64_IO_OUTD((opt_uacpi_io_addr_t)handle + offset, value);
    return 0;
}

int arch_uacpi_set_irq(opt_uacpi_u32_t irq, com_isr_t *isr) {
    x86_64_ioapic_set_irq(irq,
                          isr->vec & 0xff,
                          ARCH_CPU_GET_HARDWARE_ID(),
                          false);
    return 0;
}

void arch_uacpi_irq_eoi(com_isr_t *isr) {
    x86_64_lapic_eoi(isr);
}

int arch_uacpi_early_init(void) {
    x86_64_ioapic_init();
    opt_acpi_init_pci();
    return 0;
}
