#include <arch/cpu.h>
#include <kernel/com/util.h>
#include <kernel/opt/pci.h>
#include <kernel/platform/x86-64/lapic.h>

u64 pci_msi_format_message(u32 *data, u64 intvec, int flags) {
    bool edge_trigger = flags & PCI_MSIFMT_EDGE_TRIGGER;
    bool deassert = flags & PCI_MSIFMT_DEASSERT;

    *data = (edge_trigger ? 0 : (1 << 15)) | (deassert ? 0 : (1 << 14)) |
        intvec;
    return (cpu_get_hardware_id() << 12) | 0xfee00000;
}

void pci_msi_eoi(InterruptHandler *isr) { lapic_eoi(isr); }
