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

#include <arch/info.h>
#include <kernel/com/io/log.h>
#include <kernel/opt/acpi.h>
#include <kernel/platform/info.h>
#include <lib/str.h>
#include <stdint.h>
#include <vendor/limine.h>

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_memmap_request
    MemoryMapRequest = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct
    limine_kernel_address_request
        KernelAddress = (struct limine_kernel_address_request){
            .id       = LIMINE_KERNEL_ADDRESS_REQUEST,
            .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_rsdp_request
    RsdpRequest = (struct limine_rsdp_request){.id       = LIMINE_RSDP_REQUEST,
                                               .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_module_request
    ModuleRequest = (struct limine_module_request){.id = LIMINE_MODULE_REQUEST,
                                                   .revision = 0};

__attribute__((
    used,
    section(
        ".limine_response"))) static volatile struct limine_framebuffer_request
    FramebufferRequest = (struct limine_framebuffer_request){
        .id       = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0};

arch_memmap_t *arch_info_get_memmap(void) {
    KASSERT(NULL != MemoryMapRequest.response);
    return MemoryMapRequest.response;
}

arch_kaddr_t *arch_info_get_kaddr(void) {
    KASSERT(NULL != KernelAddress.response);
    return KernelAddress.response;
}

arch_rsdp_t *arch_info_get_rsdp(void) {
    KASSERT(NULL != RsdpRequest.response);
    return RsdpRequest.response;
}

arch_file_t *arch_info_get_initrd(void) {
    KASSERT(NULL != ModuleRequest.response);
    KASSERT(0 < ModuleRequest.response->module_count);

    for (uintmax_t i = 0; i < ModuleRequest.response->module_count; i++) {
        arch_file_t *mod = ModuleRequest.response->modules[i];

        if (0 == kstrcmp("/initrd", mod->path)) {
            return mod;
        }
    }

    KASSERT(!"no initrd found");
}

arch_framebuffer_t *arch_info_get_fb(void) {
    KASSERT(NULL != FramebufferRequest.response);
    KASSERT(FramebufferRequest.response->framebuffer_count > 0);

    return FramebufferRequest.response->framebuffers[0];
}
