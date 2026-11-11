#pragma once

#include <arch/info.h>
#include <kernel/com/io/term.h>
#include <kernel/com/util.h>

#ifndef HAVE_OPT_FLANTERM
#error "cannot including this header without having opt/flanterm"
#endif

void *flanterm_init(Framebuffer *fb, u32 fg_color, u32 bg_color, usize scale_x,
                    usize scale_y);
TermBackend flanterm_new_context(void);
void flanterm_init_bootstrap(void);
void flanterm_bootstrap_putsn(const char *s, usize n);
void *flanterm_bootstrap_get_context(void);
