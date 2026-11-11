#include <arch/info.h>
#include <kernel/com/util.h>
#include <kernel/opt/flanterm.h>
#include <kernel/platform/info.h>

#include "vendor/flanterm_backends/fb.h"

#include "vendor/flanterm.h"

static struct flanterm_context *g_flanterm_bootstrap_context = NULL;

void flanterm_init_bootstrap(void) {
    u32 bg_color = 0x00000000;
    u32 fg_color = 0xFFFFFFFF;

    Framebuffer *fb = info_get_fb();
    g_flanterm_bootstrap_context = flanterm_fb_init(
        NULL, NULL, fb->address, fb->width, fb->height, fb->pitch,
        fb->red_mask_size, fb->red_mask_shift, fb->green_mask_size,
        fb->green_mask_shift, fb->blue_mask_size, fb->blue_mask_shift, NULL,
        NULL, NULL, &bg_color, &fg_color, NULL, NULL, NULL, 0, 0, 1, 0, 0, 0);
}

void flanterm_bootstrap_putsn(const char *s, usize n) {
    if (g_flanterm_bootstrap_context != NULL) {
        flanterm_write(g_flanterm_bootstrap_context, s, n);
    }
}

void *flanterm_bootstrap_get_context(void) {
    return g_flanterm_bootstrap_context;
}
