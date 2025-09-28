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

#include <kernel/opt/nvme.h>
#include <kernel/opt/pci.h>
#include <lib/util.h>

#define NVME_LOG(fmt, ...) KOPTMSG("NVME", fmt "\n", __VA_ARGS__)
#define NVME_PANIC()       com_sys_panic(NULL, "nvme error")

static int nvme_init_device(opt_pci_enum_t *device) {
    NVME_LOG("(info) found device " OPT_PCI_ADDR_PRINTF_FMT,
             OPT_PCI_ADDR_PRINTF_VALUES(device->addr));
    uint16_t msix_entries = opt_pci_msix_init(device);
    NVME_LOG("(debug) found %u msix entries", msix_entries);
    return 0;
}

static opt_pci_dev_driver_t NVMEDriver = {
    .wildcard      = {.clazz    = OPT_PCI_CLASS_STORAGE,
                      .subclass = OPT_PCI_SUBCLASS_STORAGE_NVM},
    .wildcard_mask = OPT_PCI_QUERYMASK_CLASS | OPT_PCI_QUERYMASK_SUBCLASS,
    .init_dev      = nvme_init_device};

int opt_nvme_init(void) {
    KLOG("initializing nvme");
    int ret = opt_pci_install_driver(&NVMEDriver);
    KASSERT(0 == ret);
    return ret;
}
