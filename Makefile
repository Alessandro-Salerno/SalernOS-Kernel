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


OSNAME 	= SalernOS
MAKEDIR = make

FAMILY ?= x86
ARCH   ?= x86-64
BOOT   ?= SimpleBoot

include $(MAKEDIR)/files.mk
include $(MAKEDIR)/commands.mk
include $(MAKEDIR)/flags.mk
include $(MAKEDIR)/data.mk

include $(KSTDDIR)/Makefile
include $(KLIBDIR)/Makefile
include $(SRCDIR)/Makefile

include arch/Makefile


setup:
	@ mkdir $(BUILDDIR)
	@ mkdir $(OBJDIR)

kernel: $(OBJS) link

link:
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BUILDDIR)/kernel.elf $(OBJS)

clean:
	rm $(OBJS)
