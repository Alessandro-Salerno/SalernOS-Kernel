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
