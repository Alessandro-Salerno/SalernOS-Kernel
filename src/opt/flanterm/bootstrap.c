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
#include <kernel/opt/flanterm.h>
#include <kernel/platform/info.h>

#include "vendor/flanterm_backends/fb.h"

#include "vendor/flanterm.h"

static struct flanterm_context *FlantermBootstrapContext = NULL;

void opt_flanterm_init_bootstrap(void) {
    uint32_t bg_color = 0x00000000;
    uint32_t fg_color = 0xFFFFFFFF;

    arch_framebuffer_t *fb   = arch_info_get_fb();
    FlantermBootstrapContext = flanterm_fb_init(NULL,
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
                                                &bg_color,
                                                &fg_color,
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

void opt_flanterm_bootstrap_putsn(const char *s, size_t n) {
    if (NULL != FlantermBootstrapContext) {
        flanterm_write(FlantermBootstrapContext, s, n);
    }
}
