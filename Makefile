OSNAME 	= SalernOS
MAKEDIR = make

include $(MAKEDIR)/Make.files
include $(MAKEDIR)/Make.commands
include $(MAKEDIR)/Make.flags
include $(MAKEDIR)/Make.data

include $(KSTDDIR)/Make.rules
include $(KLIBDIR)/Make.rules
include $(SRCDIR)/Make.rules


kernel: $(OBJS) link


link:
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BUILDDIR)/kernel.elf $(OBJS)


setup:
	@mkdir $(BUILDDIR)
	@mkdir $(OBJDIR)
