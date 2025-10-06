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

#include <stdbool.h>
#include <stdint.h>

#define ARCH_MMU_KERNELSPACE_START (void *)0xffff800000000000
#define ARCH_MMU_KERNELSPACE_END   (void *)0xffffffffffffffff
#define ARCH_MMU_USERSPACE_START   (void *)0x0000000000001000
#define ARCH_MMU_USERSPACE_END     (void *)0x0000800000000000

#define ARCH_MMU_ADDR_IS_USER(a) ((void *)a < ARCH_MMU_USERSPACE_END)

#define ARCH_MMU_FLAGS_READ    (uint64_t)1
#define ARCH_MMU_FLAGS_WRITE   (uint64_t)2
#define ARCH_MMU_FLAGS_USER    (uint64_t)4
#define ARCH_MMU_FLAGS_PRESENT (uint64_t)1
#define ARCH_MMU_FLAGS_NOEXEC  ((uint64_t)1 << 63)

#define ARCH_MMU_FLAGS_WB 0
#define ARCH_MMU_FLAGS_WT (1 << 3)
#define ARCH_MMU_FLAGS_UC (1 << 4)
#define ARCH_MMU_FLAGS_WC ((1 << 7) | ARCH_MMU_FLAGS_WT)

// Common code (arch-agnostic) should NOT assume the presence of these flags.
// However, it may use after checking with guards. The kernel is expected to
// provide a fallback (but currently does not)
#define ARCH_MMU_EXTRA_FLAGS_SHARED  (1 << 10)
#define ARCH_MMU_EXTRA_FLAGS_PRIVATE (1 << 9) // same as COW
#define ARCH_MMU_EXTRA_FLAGS_NOCOPY  (1 << 11)

typedef uint64_t arch_mmu_pagetable_t;
typedef uint64_t arch_mmu_flags_t;
