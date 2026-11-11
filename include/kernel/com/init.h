#pragma once

#include <kernel/com/io/term.h>

void init_splash(void);
void init_memory(void);
void init_filesystem(void);
void init_tty(TermBackend (*new_backend)(void));
void init_devices(void);
void init_pid1(void);
