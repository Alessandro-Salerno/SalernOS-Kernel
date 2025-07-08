# Nuke built-in rules and variables.
MAKEFLAGS += -rR
.SUFFIXES:

PLATFORM ?= x86-64
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard ,$d, $2) $(filter $(subst *, %, $2),$d))

# This is the name that our final executable will have.
# Change as needed.
override OUTPUT := vmsalernos

# User controllable C compiler command.
CC := cc

# User controllable linker command.
LD := ld

# User controllable C flags.
# // TODO: Change to -O2, using no optimizations for debugging
CFLAGS := -g -O0 -pipe

# User controllable C preprocessor flags. We set none by default.
CPPFLAGS :=

# User controllable nasm flags.
NASMFLAGS := -felf64 -g

# User controllable linker flags. We set none by default.
LDFLAGS :=

# Internal C flags that should not be changed by the user.
override CFLAGS += \
		-I include/ \
		-I include/kernel/ \
		-I include/kernel/platform/$(PLATFORM) \
		-I include/vendor/ \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -m64 \
    -march=x86-64 \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel

# Internal C preprocessor flags that should not be changed by the user.
override CPPFLAGS := \
    $(CPPFLAGS) \
    -MMD \
    -MP

# Internal nasm flags that should not be changed by the user.
override NASMFLAGS += \
    -f elf64

# Internal linker flags that should not be changed by the user.
override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    -T src/platform/$(PLATFORM)/linker.ld

# Use "find" to glob all *.c, *.S, and *.asm files in the tree and obtain the
# object and header dependency file names.

override CFILES = $(call rwildcard, src/com, *c) $(call rwildcard, src/platform/$(PLATFORM), *.c)
override ASFILES = $(call rwildcard, src/platform/$(PLATFORM), *.S)
override NASMFILES = $(call rwildcard, src/platform/$(PLATFORM), *.asm)
override OBJ = $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
override HEADER_DEPS = $(addprefix obj/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

# Default target.
.PHONY: all
all: bin/$(OUTPUT)

# Link rules for the final executable.
bin/$(OUTPUT): GNUmakefile src/platform/$(PLATFORM)/linker.ld $(OBJ)
	@echo [LD] Linking...
	@mkdir -p "$$(dirname $@)"
	@$(LD) $(OBJ) $(LDFLAGS) -o $@

# Include header dependencies.
-include $(HEADER_DEPS)

# Compilation rules for *.c files.
obj/src/%.c.o: src/%.c GNUmakefile
	@echo [CC] $<
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Compilation rules for *.S files.
obj/src/%.S.o: src/%.S GNUmakefile
	@echo [AS] $<
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Compilation rules for *.asm (nasm) files.
obj/src/%.asm.o: src/%.asm GNUmakefile
	@echo [NS] $<
	@mkdir -p "$$(dirname $@)"
	@nasm $(NASMFLAGS) $< -o $@

# Remove object files and the final executable.
.PHONY: clean
clean:
	rm -rf bin obj

