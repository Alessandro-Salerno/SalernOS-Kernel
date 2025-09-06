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

#include <arch/info.h>
#include <kernel/com/io/term.h>
#include <stdint.h>

void              *opt_flanterm_init(arch_framebuffer_t *fb,
                                     uint32_t            fg_color,
                                     uint32_t            bg_color,
                                     size_t              scale_x,
                                     size_t              scale_y);
com_term_backend_t opt_flanterm_new_context(void);
void               opt_flanterm_init_bootstrap(void);
void               opt_flanterm_bootstrap_putsn(const char *s, size_t n);
