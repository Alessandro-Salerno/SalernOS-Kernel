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

#pragma once

#include <arch/info.h>
#include <arch/mmu.h>
#include <lib/radixtree.h>
#include <stdint.h>

typedef kradixtree_t com_pagecache_t;

bool             com_fs_pagecache_get(uintptr_t       *out,
                                      com_pagecache_t *root,
                                      uint64_t         index);
void             com_fs_pagecache_default(uintptr_t       *out,
                                          com_pagecache_t *root,
                                          uint64_t         index);
com_pagecache_t *com_fs_pagecache_new(void);
