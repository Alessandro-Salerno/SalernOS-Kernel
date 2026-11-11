#pragma once

#include <arch/info.h>

MemMap *info_get_memmap(void);
KAddr *info_get_kaddr(void);
InfoFile *info_get_initrd(void);
Framebuffer *info_get_fb(void);
