OSNAME 	= SalernOS
MAKEDIR = make

include $(MAKEDIR)/Make.files
include $(MAKEDIR)/Make.commands
include $(MAKEDIR)/Make.flags
include $(MAKEDIR)/Make.data

include $(INCDIR)/Makefile
include $(KSTDDIR)/Makefile
include $(KLIBDIR)/Makefile
include $(SRCDIR)/Makefile


kernel: $(OBJS) link


link:
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BUILDDIR)/kernel.elf $(OBJS)


setup:
	@mkdir $(BUILDDIR)
	@mkdir $(OBJDIR)
