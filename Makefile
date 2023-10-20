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

include $(MAKEDIR)/files.mk
include $(MAKEDIR)/commands.mk
include $(MAKEDIR)/flags.mk
include $(MAKEDIR)/data.mk

include $(KSTDDIR)/Makefile
include $(KLIBDIR)/Makefile
include $(SRCDIR)/Makefile

bin:
	@mkdir -p $(BUILDDIR)
obj:
	@mkdir -p $(OBJDIR)

intf/Libraries/External/limine.h:
	@curl -Lo intf/Libraries/External/limine.h https://github.com/limine-bootloader/limine/raw/trunk/limine.h
src/External:
	@mkdir -p src/External
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/term.h
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/tterm.h
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/gterm.h
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/term.c
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/tterm.c
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/gterm.c
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/image.h
	@curl -Lo src/External/term.h https://github.com/Alessandro-Salerno/limine-terminal-port/raw/trunk/source/image.c

format:
	clang-format $(FORMAT) -i

kernel: bin obj intf/Libraries/External/limine.h src/External $(OBJS) link
	@echo Done!

link:
	@echo Linking...
	@$(LD) $(LDFLAGS) -o $(BUILDDIR)/kern $(OBJS)

clean:
	rm -rf obj/
	rm -rf bin/

distclean: clean
	rm -rf src/External
	rm intf/Libraries/External/limine.h

