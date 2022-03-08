$(OBJDIR)/%_lib.o: $(KLIBDIR)/%.c
	@ echo !==== COMPILING KERNEL LIBRARY  $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@
