override CFLAGS += -DHAVE_OPT_UACPI \
					-DOPT_ACPI_IMPLEMENTATION="\"uacpi\"" 

override UACPI_CFILES = $(call rwildcard, src/opt/uacpi, *.c)
override UACPI_ASFILES = $(call rwildcard, src/opt/uacpi, *.S)
override UACPI_NASMFILES = $(call rwildcard, src/opt/uacpi, *.asm)

override UACPI_OBJ = $(addprefix obj/,$(UACPI_CFILES:.c=.c.o) $(UACPI_ASFILES:.S=.S.o) $(UACPI_NASMFILES:.asm=.asm.o))
override UACPI_HEADER_DEPS = $(addprefix obj/,$(UACPI_CFILES:.c=.c.d) $(UACPI_ASFILES:.S=.S.d))

override OBJ += $(UACPI_OBJ)
override UACPI_FLAGS = -DUACPI_OVERRIDE_LIBC \
						-DUACPI_KERNEL_INITIALIZATION \
						-DUACPI_NATIVE_ALLOC_ZEROED 

-include $(UACPI_HEADER_DEPS)

obj/src/opt/uacpi/%.c.o: src/opt/uacpi/%.c
	@echo [CC] $<
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) $(UACPI_FLAGS) -Isrc/opt/uacpi/vendor/include -c $< -o $@
