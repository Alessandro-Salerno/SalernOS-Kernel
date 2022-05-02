FAMILYDIR = arch/$(FAMILY)
ARCHDIR   = $(FAMILYDIR)/$(ARCH)
BOOTDIR   = $(ARCHDIR)/$(BOOT)
LDS       = $(BOOTDIR)/link.ld

BOOTSRC   = $(call rwildcard, $(BOOTDIR), *.c)


$(OBJDIR)/$(KFNAME)/C/%.o: $(BOOTSRC)/%.c
	@ echo !==== COMPILING BOOT-SPECIFIC $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@
