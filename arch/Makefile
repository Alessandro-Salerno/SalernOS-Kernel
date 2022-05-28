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

$(OBJDIR)/$(KFNAME)/C/%.o: $(ARCHIMPLDIR)/%.c
	@ echo $(shell tput setaf 3) [C] $(shell tput setaf 5) [ARCH] $(shell tput setaf 7) $^
	@ mkdir -p $(@D)
	@ $(CC) $(CFLAGS) $(ARCHCINC) -O3 -c $^ -o $@

$(OBJDIR)/$(KFNAME)/Assembly/%.o: $(ARCHIMPLDIR)/%.asm
	@ echo $(shell tput setaf 3) [ASM] $(shell tput setaf 5) [ARCH] $(shell tput setaf 7) $^
	@ mkdir -p $(@D)
	@ $(ASMC) $(ASMFLAGS) $^ -f elf64 -o $@
