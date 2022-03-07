$(OBJDIR)/%_std.o: $(KSTDDIR)/%.c
	@ echo !==== COMPILING STD  $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@
