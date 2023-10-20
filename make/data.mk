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


rwildcard = $(foreach d, $(wildcard $(1:=/*)), $(call rwildcard ,$d, $2) $(filter $(subst *, %, $2), $d))

KINC   = $(call rwildcard, $(INCDIR), *.c)
KSTD   = $(call rwildcard, $(KSTDDIR), *.c)
KLIB   = $(call rwildcard, $(KLIBDIR), *.c)
SRC    = $(call rwildcard, $(SRCDIR), *.c)
SRC   += $(call rwildcard, $(SRCDIR), *.cpp)
ASMSRC = $(call rwildcard, $(SRCDIR), *.asm)
FORMAT = $(SRC) $(call rwildcard, ./, *.h)

OBJS   = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/$(KFNAME)/C/%.o, $(SRC))
OBJS  += $(patsubst $(SRCDIR)/%.asm, $(OBJDIR)/$(KFNAME)/Assembly/%.o, $(ASMSRC)) 
OBJS  += $(patsubst $(KSTDDIR)/%.c, $(OBJDIR)/$(LFNAME)/%.o, $(KSTD))
OBJS  += $(patsubst $(KLIBDIR)/%.c, $(OBJDIR)/$(LFNAME)/%.o, $(KLIB))
OBJS  += $(patsubst $(INCDIR)/%.c, $(OBJDIR)/$(LFNAME)/%.o, $(KINC))

DIRS   = $(wildcard $(SRCDIR)/*)
DIRS  += $(wildcard $(KSTDDIR)/*)
DIRS  += $(wildcard $(KLIBDIR)/*)
DIRS  += $(wildcard $(INCDIR)/*)
