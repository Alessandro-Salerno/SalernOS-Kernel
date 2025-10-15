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
#include <kernel/opt/pci.h>
#include <kernel/platform/x86-64/lapic.h>

uint64_t
arch_pci_msi_format_message(uint32_t *data, uintmax_t intvec, int flags) {
    bool edge_trigger = OPT_PCI_MSIFMT_EDGE_TRIGGER & flags;
    bool deassert     = OPT_PCI_MSIFMT_DEASSERT & flags;

    *data = (edge_trigger ? 0 : (1 << 15)) | (deassert ? 0 : (1 << 14)) |
            intvec;
    return 0xfee00000 | (ARCH_CPU_GET_HARDWARE_ID() << 12);
}

void arch_pci_msi_eoi(com_isr_t *isr) {
    x86_64_lapic_eoi(isr);
}
