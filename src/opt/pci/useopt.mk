override CFLAGS += -DHAVE_OPT_PCI
override CFILES += $(call rwildcard, src/opt/pci, *.c)
