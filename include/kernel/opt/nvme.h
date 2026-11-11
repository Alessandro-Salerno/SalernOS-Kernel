#pragma once

#include <kernel/opt/pci.h>

#ifndef HAVE_OPT_NVME
#error "cannot include this header without having opt/nvme"
#endif

int nvme_init(void);
