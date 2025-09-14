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

#include <kernel/com/io/term.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/opt/flanterm.h>
#include <kernel/platform/info.h>

#include "vendor/flanterm_backends/fb.h"

// This should not be done, but we do it because we need it in case of panic
#define FLANTERM_IN_FLANTERM
#include "vendor/flanterm_private.h"
#undef FLANTERM_IN_FLANTERM

#include "vendor/flanterm.h"

static void *flanterm_backend_init(void) {
    return opt_flanterm_init(arch_info_get_fb(), 0xffffffff, 0, 0, 0);
}

static void flanterm_backend_putsn(void *termdata, const char *s, size_t n) {
    struct flanterm_context *context = termdata;
    flanterm_write(context, s, n);
}

static void
flanterm_backend_get_size(void *termdata, size_t *rows, size_t *cols) {
    struct flanterm_context *context = termdata;
    flanterm_get_dimensions(context, cols, rows);
}

static void
flanterm_backend_set_size(void *termdata, size_t rows, size_t cols) {
    (void)termdata;
    (void)rows;
    (void)cols;
}

static void flanterm_backend_flush(void *termdata) {
    struct flanterm_context *context = termdata;
    (void)context;
    flanterm_flush(context);
}

static void flanterm_backend_enable(void *termdata) {
    struct flanterm_context *context = termdata;
    flanterm_set_autoflush(context, true);
}

static void flanterm_backend_disable(void *termdata) {
    struct flanterm_context *context = termdata;
    flanterm_set_autoflush(context, false);
}

static void flanterm_backend_refresh(void *termdata) {
    struct flanterm_context *context = termdata;
    flanterm_full_refresh(context);
}

static void flanterm_backend_panic(void *termdata) {
    struct flanterm_context *context = termdata;
    *context = *(struct flanterm_context *)opt_flanterm_bootstrap_get_context();
}

static void *flanterm_backend_malloc(size_t bytes) {
    return (void *)ARCH_PHYS_TO_HHDM(
        com_mm_pmm_alloc_many(bytes / ARCH_PAGE_SIZE + 1));
}

static void flanterm_backend_free(void *buf, size_t bytes) {
    com_mm_pmm_free_many((void *)ARCH_HHDM_TO_PHYS(buf),
                         bytes / ARCH_PAGE_SIZE + 1);
}

void *opt_flanterm_init(arch_framebuffer_t *fb,
                        uint32_t            fg_color,
                        uint32_t            bg_color,
                        size_t              scale_x,
                        size_t              scale_y) {
    struct flanterm_context *ret = flanterm_fb_init(flanterm_backend_malloc,
                                                    flanterm_backend_free,
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
                                                    scale_x,
                                                    scale_y,
                                                    0);
    flanterm_set_oob_output(ret, 0);
    flanterm_backend_disable(ret);
    return ret;
}

com_term_backend_t opt_flanterm_new_context(void) {
    static com_term_backend_ops_t ops = {.init     = flanterm_backend_init,
                                         .putsn    = flanterm_backend_putsn,
                                         .get_size = flanterm_backend_get_size,
                                         .set_size = flanterm_backend_set_size,
                                         .flush    = flanterm_backend_flush,
                                         .enable   = flanterm_backend_enable,
                                         .disable  = flanterm_backend_disable,
                                         .refresh  = flanterm_backend_refresh,
                                         .panic    = flanterm_backend_panic};
    return (com_term_backend_t){.ops = &ops, .data = NULL};
}
