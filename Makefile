include Make.defaults

rwildcard = $(foreach d, $(wildcard $(1:=/*)), $(call rwildcard, $d, $2) $(filter $(subst *, %, $2), $d))

KSTD      = $(call rwildcard, $(KSTDDIR), *.c)
KLIB      = $(call rwildcard, $(KLIBDIR), *.c)
SRC       = $(call rwildcard, $(SRCDIR), *.c)
ASMSRC.   = $(call rwildcard, $(SRCDIR), *.asm)

OBJS      = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC))
OBJS     += $(patsubst $(SRCDIR)/%.asm, $(OBJDIR)/%_asm.o, $(ASMSRC)) 
OBJS     += $(patsubst $(KSTDDIR)/%.c, $(OBJDIR)/%_std.o, $(KSTD))
OBJS     += $(patsubst $(KLIBDIR)/%.c, $(OBJDIR)/%_lib.o, $(KLIB))

DIRS      = $(wildcard $(SRCDIR)/*)
DIRS     += $(wildcard $(KSTDDIR)/*)
DIRS     += $(wildcard $(KLIBDIR)/*)


kernel: $(OBJS) link

$(OBJDIR)/%_std.o: $(KSTDDIR)/%.c
	@ echo !==== COMPILING STD  $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@

$(OBJDIR)/%_lib.o: $(KLIBDIR)/%.c
	@ echo !==== COMPILING KERNEL LIBRARY  $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@

$(OBJDIR)/Interrupts/handlers.o: $(SRCDIR)/Interrupts/handlers.c
	@ echo !==== COMPILING INTERRUPTS UNOPTIMIZED $^
	@ mkdir -p $(@D)
	$(CC) $(CINCLUDE) -mno-red-zone -mgeneral-regs-only -ffreestanding -c $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@
	
$(OBJDIR)/%_asm.o: $(SRCDIR)/%.asm
	@ echo !==== COMPILING ASM $^
	@ mkdir -p $(@D)
	$(ASMC) $(ASMFLAGS) $^ -f elf64 -o $@


link:
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BUILDDIR)/kernel.elf $(OBJS)


setup:
	@mkdir $(BUILDDIR)
	@mkdir $(OBJDIR)
