# SalernOS Kernel
# Copyright (C) 2021 - 2023 Alessandro Salerno

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


OSNAME=SalernOS
MAKEDIR=make

.PHONY: all
all: setup
	@make -j10 compile
	@make kernel

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

intf/libraries/extern/limine.h:
	@curl -Lo intf/libraries/extern/limine.h https://github.com/limine-bootloader/limine/raw/trunk/limine.h
src/extern:
	@mkdir -p src/extern
	@git clone https://github.com/mintsuki/flanterm src/extern/flanterm

format:
	find ./ -iname *.h -o -iname *.c | xargs clang-format -i

setup: bin obj intf/libraries/extern/limine.h src/extern

compile: setup $(OBJS)
kernel: compile link
	@echo Done!

link:
	@echo Linking...
	@$(LD) $(LDFLAGS) -o $(BUILDDIR)/kern $(OBJS)

clean:
	rm -rf obj/
	rm -rf bin/

distclean: clean
	rm -rf src/extern
	rm intf/libraries/extern/limine.h
