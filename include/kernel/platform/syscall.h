#pragma once

#include <arch/context.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALLS

SYSCALL(syscall_set_tls);
