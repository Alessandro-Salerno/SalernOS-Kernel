#include "User/Output/Text/textrenderer.h"
#include "kernelpanic.h"
#include <kstdio.h>
#include <kdebug.h>


void kernel_panic_throw(const char* __message) {
    klogerr("Kernel panic: execution of the kernel has been halted due to a critical system fault.");
    kprintf("\n\n%s\n", __message);

    while (1);
}

void kernel_panic_assert(uint8_t __cond, const char* __message) {
    SOFTASSERT(!(__cond), RETVOID);
    kernel_panic_throw(__message);
}
