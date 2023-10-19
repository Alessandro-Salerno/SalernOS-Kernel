# SalernOS Kernel
# Copyright (C) 2021 - 2022 Alessandro Salerno

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


CINCLUDE = -I "$(INCDIR)" -I "$(INCDIR)/Libraries/External" -I "$(INCDIR)/Libraries/Kernel" -I "$(SRCDIR)" 
CFLAGS 	 = -Wall -Wextra -O2 -ffreestanding -fshort-wchar -mno-red-zone $(CINCLUDE)
CPPFLAGS = $(CFLAGS) -fno-exceptions
ASMFLAGS = 
LDFLAGS  = -T $(LDS) -static -Bsymbolic -nostdlib -z max-page-size=0x1000 --no-dynamic-linker 
