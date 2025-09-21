override CFLAGS += -DHAVE_OPT_ACPI
override CFILES += $(call rwildcard, src/opt/acpi, *.c)
