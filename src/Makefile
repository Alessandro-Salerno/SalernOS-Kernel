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


$(OBJDIR)/$(KFNAME)/C/Interrupts/handlers.o: $(SRCDIR)/Interrupts/handlers.c
	@ echo !==== COMPILING INTERRUPTS UNOPTIMIZED $^
	@ mkdir -p $(@D)
	@$(CC) $(CFLAGS) -mgeneral-regs-only -c $^ -o $@

$(OBJDIR)/$(KFNAME)/C/%.o: $(SRCDIR)/%.c
	@ echo [C] $^
	@ mkdir -p $(@D)
	@$(CC) $(CFLAGS) -c $^ -o $@

$(OBJDIR)/$(KFNAME)/C++/%.o: $(SRCDIR)/%.cpp
	@ echo [C++] $^
	@ mkdir -p $(@D)
	@$(CC) $(CPPFLAGS) -c $^ -o $@

$(OBJDIR)/$(KFNAME)/Assembly/%.o: $(SRCDIR)/%.asm
	@ echo [ASM] $^
	@ mkdir -p $(@D)
	@$(ASMC) $(ASMFLAGS) $^ -f elf64 -o $@
