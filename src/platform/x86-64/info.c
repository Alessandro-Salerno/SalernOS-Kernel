/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#include <arch/info.h>
#include <kernel/com/log.h>
#include <kernel/platform/info.h>
#include <vendor/limine.h>

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_memmap_request
    MemoryMapRequest = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct
    limine_kernel_address_request KernelAddress =
        (struct limine_kernel_address_request){
            .id       = LIMINE_KERNEL_ADDRESS_REQUEST,
            .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_rsdp_request
    RsdpRequest =
        (struct limine_rsdp_request){.id = LIMINE_RSDP_REQUEST, .revision = 0};

arch_memmap_t *arch_info_get_memmap(void) {
  ASSERT(NULL != MemoryMapRequest.response);
  return MemoryMapRequest.response;
}

arch_kaddr_t *arch_info_get_kaddr(void) {
  ASSERT(NULL != KernelAddress.response);
  return KernelAddress.response;
}

arch_rsdp_t *arch_info_get_rsdp(void) {
  ASSERT(NULL != RsdpRequest.response);
  return RsdpRequest.response;
}
