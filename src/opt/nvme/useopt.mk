override CFLAGS += -DHAVE_OPT_NVME
override CFILES += $(call rwildcard, src/opt/nvme, *.c)
