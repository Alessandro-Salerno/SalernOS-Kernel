#include <kernel/com/util.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/io.h>

void e9_putsn(const char *s, usize n) {
    for (usize i = 0; i < n; i++) io_outb(0xE9, s[i]);
}
