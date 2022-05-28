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


FAMILYDIR     = arch/$(FAMILY)
COMMONDIR     = $(FAMILYDIR)/Common
ARCHDIR       = $(FAMILYDIR)/$(ARCH)

COMMONIMPLDIR = $(COMMONDIR)/Implementation
COMMONINCDIR  = $(COMMONDIR)/Interface
ARCHIMPLDIR   = $(ARCHDIR)/Implementation
ARCHINCDIR    = $(ARCHDIR)/Interface
BOOTDIR       = $(ARCHDIR)/Boot/$(BOOT)

LDS           = $(BOOTDIR)/link.ld

FAMILYCINC    = -I "$(COMMONINCD)"
ARCHCINC      = $(FAMILYCINC) -I "$(ARCHINCDIR)" -I "$(BOOTDIR)"

BOOTSRC       = $(call rwildcard, $(BOOTDIR), *.c)
BOOTASM       = $(call rwildcard, $(BOOTDIR), *.asm)

FAMILYSRC     = $(call rwildcard, $(COMMONIMPLDIR), *.c)
FAMILYASM     = $(call rwildcard, $(COMMONIMPLDIR), *.asm)

ARCHSRC       = $(call rwildcard, $(ARCHIMPLDIR), *.c)
ARCHASM       = $(call rwildcard, $(ARCHIMPLDIR), *.asm)

OBJS         += $(patsubst $(BOOTDIR)/%.c, $(OBJDIR)/$(KFNAME)/C/%.o, $(BOOTSRC))
OBJS         += $(patsubst $(ARCHIMPLDIR)/%.c, $(OBJDIR)/$(KFNAME)/C/%.o, $(ARCHSRC))
OBJS         += $(patsubst $(ARCHIMPLDIR)/%.asm, $(OBJDIR)/$(KFNAME)/Assembly/%.o, $(ARCHASM))
OBJS         += $(patsubst $(BOOTDIR)/%.asm, $(OBJDIR)/$(KFNAME)/Assembly/%.o, $(BOOTASM))


$(OBJDIR)/$(KFNAME)/C/%.o: $(BOOTDIR)/%.c
	@ echo $(shell tput setaf 3) [C] $(shell tput setaf 5) [ARCH] $(shell tput setaf 7) $^
	@ mkdir -p $(@D)
	@ $(CC) $(CFLAGS) $(ARCHCINC) -O3 -c $^ -o $@

$(OBJDIR)/$(KFNAME)/Assembly/%.o: $(BOOTDIR)/%.asm
	@ echo $(shell tput setaf 3) [ASM] $(shell tput setaf 5) [ARCH] $(shell tput setaf 7) $^
	@ mkdir -p $(@D)
	@ $(ASMC) $(ASMFLAGS) $^ -f elf64 -o $@

$(OBJDIR)/$(KFNAME)/C/Interrupts/x86_64-handlers.o: $(ARCHIMPLDIR)/Interrupts/x86_64-handlers.c
	@ echo $(shell tput setaf 3) [C] $(shell tput setaf 5) [ARCH] $(shell tput setaf 7) $^
	@ mkdir -p $(@D)
	@ $(CC) $(CINCLUDE) $(ARCHCINC) -mno-red-zone -mgeneral-regs-only -ffreestanding -c $^ -o $@

$(OBJDIR)/$(KFNAME)/C/%.o: $(ARCHIMPLDIR)/%.c
	@ echo $(shell tput setaf 3) [C] $(shell tput setaf 5) [ARCH] $(shell tput setaf 7) $^
	@ mkdir -p $(@D)
	@ $(CC) $(CFLAGS) $(ARCHCINC) -O3 -c $^ -o $@

$(OBJDIR)/$(KFNAME)/Assembly/%.o: $(ARCHIMPLDIR)/%.asm
	@ echo $(shell tput setaf 3) [ASM] $(shell tput setaf 5) [ARCH] $(shell tput setaf 7) $^
	@ mkdir -p $(@D)
	@ $(ASMC) $(ASMFLAGS) $^ -f elf64 -o $@
