$(OBJDIR)/$(LFNAME)/%.o: $(INCDIR)/%.c
	@ echo !==== COMPILING KERNEL INCLUDE  $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -O3 -c $^ -o $@
