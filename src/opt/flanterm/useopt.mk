override CFLAGS += -DHAVE_OPT_FLANTERM
override CFILES += $(call rwildcard, src/opt/flanterm, *.c)
