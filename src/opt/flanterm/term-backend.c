#include <kernel/com/io/term.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/util.h>
#include <kernel/opt/flanterm.h>
#include <kernel/platform/info.h>

#include "vendor/flanterm_backends/fb.h"

// This should not be done, but we do it because we need it in case of panic
#define FLANTERM_IN_FLANTERM
#include "vendor/flanterm_private.h"
#undef FLANTERM_IN_FLANTERM

#include "vendor/flanterm.h"

static void *flanterm_backend_init(void) {
    return flanterm_init(info_get_fb(), 0xffffffff, 0, 0, 0);
}

static void flanterm_backend_putsn(void *termdata, const char *s, usize n) {
    struct flanterm_context *context = termdata;
    flanterm_write(context, s, n);
}

static void flanterm_backend_get_size(void *termdata, usize *rows,
                                      usize *cols) {
    struct flanterm_context *context = termdata;
    flanterm_get_dimensions(context, cols, rows);
}

static void flanterm_backend_set_size(void *termdata, usize rows, usize cols) {
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
    *context = *(struct flanterm_context *)flanterm_bootstrap_get_context();
}

static void *flanterm_backend_malloc(usize bytes) {
    return (void *)PHYS_TO_HHDM(pmm_allocn(bytes / PAGE_SIZE + 1));
}

static void flanterm_backend_free(void *buf, usize bytes) {
    pmm_freen((void *)HHDM_TO_PHYS(buf), bytes / PAGE_SIZE + 1);
}

void *flanterm_init(Framebuffer *fb, u32 fg_color, u32 bg_color, usize scale_x,
                    usize scale_y) {
    struct flanterm_context *ret = flanterm_fb_init(
        flanterm_backend_malloc, flanterm_backend_free, fb->address, fb->width,
        fb->height, fb->pitch, fb->red_mask_size, fb->red_mask_shift,
        fb->green_mask_size, fb->green_mask_shift, fb->blue_mask_size,
        fb->blue_mask_shift, NULL, NULL, NULL, &bg_color, &fg_color, NULL, NULL,
        NULL, 0, 0, 1, scale_x, scale_y, 0);
    flanterm_set_oob_output(ret, 0);
    flanterm_backend_disable(ret);
    return ret;
}

TermBackend flanterm_new_context(void) {
    static TermBackendOps ops = {.init = flanterm_backend_init,
                                 .putsn = flanterm_backend_putsn,
                                 .get_size = flanterm_backend_get_size,
                                 .set_size = flanterm_backend_set_size,
                                 .flush = flanterm_backend_flush,
                                 .enable = flanterm_backend_enable,
                                 .disable = flanterm_backend_disable,
                                 .refresh = flanterm_backend_refresh,
                                 .panic = flanterm_backend_panic};
    return (TermBackend){.ops = &ops, .data = NULL};
}
