/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/

#include "acpi/acpi.h"
#include <kmem.h>

void *kernel_hw_acpi_table_find(sdthdr_t *__sdthdr, char *__signature) {
  uint64_t _nentries = (__sdthdr->_Length - sizeof(sdthdr_t)) / 8;

  for (uint64_t _i = 0; _i < _nentries; _i++) {
    sdthdr_t *_sdt = (sdthdr_t *)(*(uint64_t *)((uint64_t)(__sdthdr) +
                                                sizeof(sdthdr_t) + (_i * 8)));
    if (kmemcmp(_sdt->_Signature, __signature, 4))
      return _sdt;
  }

  return NULL;
}
