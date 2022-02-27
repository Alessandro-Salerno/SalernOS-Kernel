#include <kstdio.h>
#include "kdebug.h"


void klogdebug(const char* __msg) {
    kprintf("DEBUG: ");
    kprintf(__msg);
    kprintf("\n");
}
