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

#pragma once

#include <stdint.h>
#include <vendor/limine.h>

#define ARCH_PAGE_SIZE 4096

#define ARCH_PHYS_TO_HHDM(addr) \
    ((uintptr_t)0xffff800000000000 + (uintptr_t)(addr))
#define ARCH_HHDM_TO_PHYS(addr) \
    ((uintptr_t)(addr) - (uintptr_t)0xffff800000000000)

#define ARCH_MEMMAP_IS_USABLE(entry) (LIMINE_MEMMAP_USABLE == entry->type)
#define ARCH_MEMMAP_IS_RECLAIMABLE(entry) \
    (LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE == entry->type)
#define ARCH_MEMMAP_IS_MAPPABLE(entry)                      \
    (LIMINE_MEMMAP_USABLE == entry->type ||                 \
     LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE == entry->type || \
     LIMINE_MEMMAP_KERNEL_AND_MODULES == entry->type ||     \
     LIMINE_MEMMAP_FRAMEBUFFER == entry->type)

// This type must contain the following fields:
//  - uintmax_t (or any integer) type
//  - uintptr_t (or uint64_t) base
//  - uintmax_t (or uint64_t, or size_t) length
typedef struct limine_memmap_entry arch_memmap_entry_t;

// This type must contain the following fields:
//  - arch_memmap_entry_t **entries
//  uintmax_t (or uint64_t, or size_t) entry_count
typedef struct limine_memmap_response arch_memmap_t;

// This type must contain the following fields:
//  - char *path
//  - char *cmdline
//  - uintmax_t (or uint64_t) size
typedef struct limine_file arch_file_t;

// This type must contain an arch_file_t pointer named "kernel_file"
typedef struct limine_kernel_file_response arch_kfile_t;

// This type must contain the following fields:
//  - uintptr_t (or uint64_t) virtual_base
//  - uintptr_t (or uint64_t) physical_base
typedef struct limine_kernel_address_response arch_kaddr_t;

// This type must contain an integer named "offset"
typedef struct limine_hhdm_response arch_hhdm_t;

// This type must contain a field named "address" of type void pointer
typedef struct limine_rsdp_response arch_rsdp_t;

typedef struct limine_framebuffer arch_framebuffer_t;
