OSNAME 	= SalernOS
MAKEDIR = make

include $(MAKEDIR)/Make.files
include $(MAKEDIR)/Make.commands
include $(MAKEDIR)/Make.flags

include $(KSTDDIR)/Make.rules
include $(KLIBDIR)/Make.rules
include $(SRCDIR)/Make.rules


rwildcard = $(foreach d, $(wildcard $(1:=/*)), $(call rwildcard ,$d, $2) $(filter $(subst *, %, $2), $d))

KSTD   = $(call rwildcard, $(KSTDDIR), *.c)
KLIB   = $(call rwildcard, $(KLIBDIR), *.c)
SRC    = $(call rwildcard, $(SRCDIR), *.c)
ASMSRC = $(call rwildcard, $(SRCDIR), *.asm)

OBJS   = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC))
OBJS  += $(patsubst $(SRCDIR)/%.asm, $(OBJDIR)/%_asm.o, $(ASMSRC)) 
OBJS  += $(patsubst $(KSTDDIR)/%.c, $(OBJDIR)/%_std.o, $(KSTD))
OBJS  += $(patsubst $(KLIBDIR)/%.c, $(OBJDIR)/%_lib.o, $(KLIB))

DIRS   = $(wildcard $(SRCDIR)/*)
DIRS  += $(wildcard $(KSTDDIR)/*)
DIRS  += $(wildcard $(KLIBDIR)/*)


kernel: $(OBJS) link


link:
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BUILDDIR)/kernel.elf $(OBJS)


setup:
	@mkdir $(BUILDDIR)
	@mkdir $(OBJDIR)
