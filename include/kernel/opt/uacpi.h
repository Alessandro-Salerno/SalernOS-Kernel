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
#include <kernel/com/sys/interrupt.h>
#include <stddef.h>
#include <stdint.h>

#ifndef HAVE_OPT_UACPI
#error "cannot include this header without having opt/uacpi"
#endif

// NOTE: these types are definedi n uACPI, but the kernel's option system tries
// to keep things separate, and expose a consistent API to common and
// platform-specific code. Thus these types are redefined here. The uACPI option
// code in src/opt/uacpi/glue.c uses _Static_assert to ensure that these types
// match those from uACPI
typedef void     *opt_uacpi_handle_t;
typedef size_t    opt_uacpi_size_t;
typedef uint8_t   opt_uacpi_u8_t;
typedef uint16_t  opt_uacpi_u16_t;
typedef uint32_t  opt_uacpi_u32_t;
typedef uint64_t  opt_uacpi_u64_t;
typedef int8_t    opt_uacpi_i8_t;
typedef int16_t   opt_uacpi_i16_t;
typedef int32_t   opt_uacpi_i32_t;
typedef int64_t   opt_uacpi_i64_t;
typedef uintptr_t opt_uacpi_phys_addr_t;
typedef uintptr_t opt_uacpi_io_addr_t;

int  arch_uacpi_io_map(opt_uacpi_handle_t *out_handle,
                       opt_uacpi_io_addr_t base,
                       opt_uacpi_size_t    len);
void arch_uacpi_io_unmap(opt_uacpi_handle_t handle);
int  arch_uacpi_io_read8(opt_uacpi_u8_t    *out_value,
                         opt_uacpi_handle_t handle,
                         opt_uacpi_size_t   offset);
int  arch_uacpi_io_read16(opt_uacpi_u16_t   *out_value,
                          opt_uacpi_handle_t handle,
                          opt_uacpi_size_t   offset);
int  arch_uacpi_io_read32(opt_uacpi_u32_t   *out_value,
                          opt_uacpi_handle_t handle,
                          opt_uacpi_size_t   offset);
int  arch_uacpi_io_write8(opt_uacpi_handle_t handle,
                          opt_uacpi_size_t   offset,
                          opt_uacpi_u8_t     value);
int  arch_uacpi_io_write16(opt_uacpi_handle_t handle,
                           opt_uacpi_size_t   offset,
                           opt_uacpi_u16_t    value);
int  arch_uacpi_io_write32(opt_uacpi_handle_t handle,
                           opt_uacpi_size_t   offset,
                           opt_uacpi_u32_t    value);
int  arch_uacpi_set_irq(opt_uacpi_u32_t irq, com_isr_t *isr);
void arch_uacpi_irq_eoi(com_isr_t *isr);

int opt_uacpi_init(opt_uacpi_u64_t flags);
