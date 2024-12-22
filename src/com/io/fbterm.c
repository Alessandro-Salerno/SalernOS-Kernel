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
#include <kernel/com/io/fbterm.h>
#include <lib/str.h>
#include <stddef.h>
#include <vendor/flanterm/backends/fb.h>
#include <vendor/flanterm/flanterm.h>

static volatile struct flanterm_context *FlantermContext = NULL;

void com_io_fbterm_putc(char c) {
  flanterm_write(FlantermContext, &c, 1);
}

void com_io_fbterm_puts(const char *s) {
  flanterm_write(FlantermContext, s, kstrlen(s));
}

void com_io_fbterm_putsn(const char *s, size_t n) {
  flanterm_write(FlantermContext, s, n);
}

void com_io_fbterm_init(arch_framebuffer_t *fb) {
  FlantermContext = flanterm_fb_init(NULL,
                                     NULL,
                                     fb->address,
                                     fb->width,
                                     fb->height,
                                     fb->pitch,
                                     fb->red_mask_size,
                                     fb->red_mask_shift,
                                     fb->green_mask_size,
                                     fb->green_mask_shift,
                                     fb->blue_mask_size,
                                     fb->blue_mask_shift,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     NULL,
                                     0,
                                     0,
                                     1,
                                     0,
                                     0,
                                     0);
}
