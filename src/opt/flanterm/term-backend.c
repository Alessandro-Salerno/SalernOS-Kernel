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
#include <vendor/flanterm/backends/fb.h>
#include <vendor/flanterm/flanterm.h>

static void *flanterm_init(void) {
    return opt_flanterm_init(arch_info_get_fb(), 0xffffffff, 0, 0, 0);
}

static void flanterm_putsn(void *termdata, const char *s, size_t n) {
    struct flanterm_context *context = termdata;
    flanterm_write(context, s, n);
}

static void flanterm_get_size(void *termdata, size_t *rows, size_t *cols) {
    struct flanterm_context *context = termdata;

    if (NULL != rows) {
        *rows = context->rows;
    }

    if (NULL != cols) {
        *cols = context->cols;
    }
}

static void flanterm_set_size(void *termdata, size_t rows, size_t cols) {
    (void)termdata;
    (void)rows;
    (void)cols;
    // TODO: maybe do something with scaling?
}

static void flanterm_flush(void *termdata) {
    struct flanterm_context *context = termdata;
    context->double_buffer_flush(context);
}

static void flanterm_enable(void *termdata) {
    struct flanterm_context *context = termdata;
    context->autoflush               = true;
}

static void flanterm_disable(void *termdata) {
    struct flanterm_context *context = termdata;
    context->autoflush               = true;
}

static void *flanterm_malloc(size_t bytes) {
    return (void *)ARCH_PHYS_TO_HHDM(
        com_mm_pmm_alloc_many(bytes / ARCH_PAGE_SIZE + 1));
}

static void flanterm_free(void *buf, size_t bytes) {
    for (uintptr_t page = (uintptr_t)buf;
         page < (uintptr_t)buf + bytes * ARCH_PAGE_SIZE;
         page += ARCH_PAGE_SIZE) {
        com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(page));
    }
}

void *opt_flanterm_init(arch_framebuffer_t *fb,
                        uint32_t            fg_color,
                        uint32_t            bg_color,
                        size_t              scale_x,
                        size_t              scale_y) {
    struct flanterm_context *ret = flanterm_fb_init(NULL,
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
                                                    scale_x,
                                                    scale_y,
                                                    0);
    ret->autoflush               = false;
    return ret;
}

com_term_backend_t opt_flanterm_new_context() {
    static com_term_backend_ops_t ops = {.init     = flanterm_init,
                                         .putsn    = flanterm_putsn,
                                         .get_size = flanterm_get_size,
                                         .set_size = flanterm_set_size,
                                         .flush    = flanterm_flush,
                                         .enable   = flanterm_enable,
                                         .disable  = flanterm_disable};
    return (com_term_backend_t){.ops = &ops, .data = NULL};
}
