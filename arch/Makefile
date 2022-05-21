FAMILYDIR = arch/$(FAMILY)
ARCHDIR   = $(FAMILYDIR)/$(ARCH)
BOOTDIR   = $(ARCHDIR)/Boot/$(BOOT)
LDS       = $(BOOTDIR)/link.ld

BOOTSRC   = $(call rwildcard, $(BOOTDIR), *.c)
OBJS     += $(patsubst $(BOOTDIR)/%.c, $(OBJDIR)/$(KFNAME)/C/%.o, $(BOOTSRC))


$(OBJDIR)/$(KFNAME)/C/%.o: $(BOOTDIR)/%.c
	echo !==== COMPILING BOOT-SPECIFIC $^
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@
