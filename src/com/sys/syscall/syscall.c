/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/syscall.h>
#include <stdint.h>

// NOTE: not static so others can call
volatile com_syscall_t SyscallTable[CONFIG_SYSCALL_MAX];

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_SYSCALL
static void log_syscall(volatile com_syscall_t *syscall,
                        arch_syscall_arg_t      args[]) {
    kprintf("%s(", syscall->name);

    for (size_t i = 0; i < syscall->num_args; i++) {
        int         arg_type = syscall->arg_types[i];
        const char *arg_name = syscall->arg_names[i];
        kprintf("%s = ", arg_name);

        com_proc_t *curr_proc = ARCH_CPU_GET_THREAD()->proc;

        switch (arg_type) {
            case COM_SYS_SYSCALL_TYPE_INT:
                kprintf("%d", (int)args[i]);
                break;
            case COM_SYS_SYSCALL_TYPE_UINTMAX:
                kprintf("%u", (uintmax_t)args[i]);
                break;
            case COM_SYS_SYSCALL_TYPE_SIZET:
                kprintf("%u", (size_t)args[i]);
                break;
            case COM_SYS_SYSCALL_TYPE_PTR: {
                if (NULL != (void *)args[i]) {
                    kprintf("%p", (void *)args[i]);
                } else {
                    kprintf("NULL");
                }
                break;
            }
            case COM_SYS_SYSCALL_TYPE_STR:
                if (NULL != (void *)args[i]) {
                    // TODO: poor man's usercopy, if the string is longer than
                    // ARCH_PAGE_SIZE this may still crash
                    if (NULL != arch_mmu_get_physical(curr_proc->page_table,
                                                      (void *)args[i])) {
                        kprintf("\"%s\"", (void *)args[i]);
                    } else {
                        kprintf("**UNMAPPED**");
                    }
                } else {
                    kprintf("NULL");
                }
                break;
            case COM_SYS_SYSCALL_TYPE_FLAGS:
                kprintf("0x%x", (int)args[i]);
                break;
            case COM_SYS_SYSCALL_TYPE_LONGFLAGS:
                kprintf("0x%x", (uintmax_t)args[i]);
                break;
            case COM_SYS_SYSCALL_TYPE_OFFT:
                kprintf("%d", (off_t)args[i]);
                break;
            case COM_SYS_SYSCALL_TYPE_UINT32:
                kprintf("%u", (uint32_t)args[i]);
                break;
            default:
                kprintf("?");
                break;
        }

        if (i != syscall->num_args - 1) {
            kprintf(", ");
        }
    }

    kprintf(")");
}
#endif

void com_sys_syscall_register(uintmax_t          number,
                              const char        *name,
                              com_intf_syscall_t handler,
                              size_t             num_args,
                              ...) {
    KASSERT(number < CONFIG_SYSCALL_MAX);
    volatile com_syscall_t *syscall = &SyscallTable[number];
    syscall->name                   = name;
    syscall->handler                = handler;
    syscall->num_args               = num_args;

    if (0 == num_args) {
        return;
    }

    va_list args;
    va_start(args, num_args);
    for (size_t i = 0; i < num_args; i++) {
        int         arg_type  = va_arg(args, int);
        const char *arg_name  = va_arg(args, const char *);
        syscall->arg_names[i] = arg_name;
        syscall->arg_types[i] = arg_type;
    }
    va_end(args);
}

com_syscall_ret_t com_sys_syscall_invoke(uintmax_t          number,
                                         arch_context_t    *ctx,
                                         arch_syscall_arg_t arg1,
                                         arch_syscall_arg_t arg2,
                                         arch_syscall_arg_t arg3,
                                         arch_syscall_arg_t arg4,
                                         uintptr_t          invoke_ip) {
    KASSERT(number < CONFIG_SYSCALL_MAX);
    volatile com_syscall_t *syscall = &SyscallTable[number];

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_SYSCALL
    arch_syscall_arg_t args[4] = {arg1, arg2, arg3, arg4};
#if CONFIG_LOG_SYSCALL_MODE == CONST_LOG_SYSCALL_BEFORE
    com_io_log_lock();
    kinitlog("SYSCALL", "\033[33m");
    log_syscall(syscall, args);
    kprintf(" at %p\n", invoke_ip);
    com_io_log_unlock();
#else
    (void)invoke_ip;
#define DO_SYSCALL_LOG_AFTER
#endif
#else
    (void)invoke_ip;
#endif

    com_syscall_ret_t ret = syscall->handler(ctx, arg1, arg2, arg3, arg4);
#ifdef DO_SYSCALL_LOG_AFTER
    com_io_log_lock();
    kinitlog("SYSCALL", "\033[33m");
    log_syscall(syscall, args);
    kprintf(" -> {ret = %u, errno = %u}\n", ret.value, ret.err);
    com_io_log_unlock();
#undef DO_SYSCALL_LOG_AFTER
#endif
    return ret;
}

void com_sys_syscall_init(void) {
    KLOG("initializing common system calls");

    com_sys_syscall_register(0x00,
                             "kprint",
                             com_sys_syscall_kprint,
                             1,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "message");

    com_sys_syscall_register(0x01,
                             "write",
                             com_sys_syscall_write,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "buf",
                             COM_SYS_SYSCALL_TYPE_SIZET,
                             "buflen");

    com_sys_syscall_register(0x02,
                             "read",
                             com_sys_syscall_read,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "buf",
                             COM_SYS_SYSCALL_TYPE_SIZET,
                             "buflen");

    com_sys_syscall_register(0x03,
                             "execve",
                             com_sys_syscall_execve,
                             3,
                             COM_SYS_SYSCALL_TYPE_STR,
                             "path",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "argv",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "envp");

    com_sys_syscall_register(0x04, "fork", com_sys_syscall_fork, 0);

    com_sys_syscall_register(0x05,
                             "sysinfo",
                             com_sys_syscall_sysinfo,
                             1,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "sysinfo_struct");

    com_sys_syscall_register(0x06,
                             "waitpid",
                             com_sys_syscall_waitpid,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "pid",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "status",
                             COM_SYS_SYSCALL_TYPE_FLAGS,
                             "flags");

    com_sys_syscall_register(0x07,
                             "exit",
                             com_sys_syscall_exit,
                             1,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "ecode");

    com_sys_syscall_register(0x08,
                             "ioctl",
                             com_sys_syscall_ioctl,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd",
                             COM_SYS_SYSCALL_TYPE_LONGFLAGS,
                             "op",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "buf");

    com_sys_syscall_register(0x09,
                             "openat",
                             com_sys_syscall_openat,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "dir_fd",
                             COM_SYS_SYSCALL_TYPE_STR,
                             "path",
                             COM_SYS_SYSCALL_TYPE_FLAGS,
                             "flags");

    com_sys_syscall_register(0x0A,
                             "mmap",
                             com_sys_syscall_mmap,
                             4,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "hint",
                             COM_SYS_SYSCALL_TYPE_SIZET,
                             "size",
                             COM_SYS_SYSCALL_TYPE_LONGFLAGS,
                             "flags",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd");

    com_sys_syscall_register(0x0B,
                             "set_tls",
                             arch_syscall_set_tls,
                             1,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "tls");

    com_sys_syscall_register(0x0C,
                             "seek",
                             com_sys_syscall_seek,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd",
                             COM_SYS_SYSCALL_TYPE_OFFT,
                             "off",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "whence");

    com_sys_syscall_register(0x0D,
                             "isatty",
                             com_sys_syscall_isatty,
                             1,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd");

    com_sys_syscall_register(0x0E,
                             "fstatat",
                             com_sys_syscall_fstatat,
                             4,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "dir_fd",
                             COM_SYS_SYSCALL_TYPE_STR,
                             "path",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "statbuf",
                             COM_SYS_SYSCALL_TYPE_FLAGS,
                             "flags");

    com_sys_syscall_register(0x0F,
                             "ftruncate",
                             com_sys_syscall_truncate,
                             2,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd",
                             COM_SYS_SYSCALL_TYPE_OFFT,
                             "size");

    com_sys_syscall_register(0x10,
                             "pipe",
                             com_sys_syscall_pipe,
                             1,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "fildes");

    com_sys_syscall_register(0x11, "getpid", com_sys_syscall_getpid, 0);

    com_sys_syscall_register(0x12,
                             "clone",
                             com_sys_syscall_clone,
                             2,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "new_ip",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "new_sp");

    com_sys_syscall_register(
        0x13, "exit_thread", com_sys_syscall_exit_thread, 0);

    com_sys_syscall_register(0x14,
                             "futex",
                             com_sys_syscall_futex,
                             3,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "word_ptr",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "op",
                             COM_SYS_SYSCALL_TYPE_UINT32,
                             "value");

    com_sys_syscall_register(0x15, "getppid", com_sys_syscall_getppid, 0);

    com_sys_syscall_register(0x16,
                             "getpgid",
                             com_sys_syscall_getpgid,
                             1,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "pid");

    com_sys_syscall_register(0x17,
                             "getsid",
                             com_sys_syscall_getsid,
                             1,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "pid");

    com_sys_syscall_register(0x18,
                             "setpgid",
                             com_sys_syscall_setpgid,
                             2,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "pid",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "pgid");

    com_sys_syscall_register(0x19, "setsid", com_sys_syscall_setsid, 0);

    com_sys_syscall_register(0x1A,
                             "sigprocmask",
                             com_sys_syscall_sigprocmask,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "how",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "set",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "oset");

    com_sys_syscall_register(0x1B,
                             "sigpending",
                             com_sys_syscall_sigpending,
                             1,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "set");

    com_sys_syscall_register(0x1C,
                             "sigthreadmask",
                             com_sys_syscall_ssigthreadmask,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "how",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "set",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "oset");

    com_sys_syscall_register(0x1D,
                             "sigaction",
                             com_sys_syscall_sigaction,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "sig",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "act",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "oact");

    com_sys_syscall_register(0x1E,
                             "kill",
                             com_sys_syscall_kill,
                             2,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "pid",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "sig");

    com_sys_syscall_register(0x1F,
                             "kill_thread",
                             com_sys_syscall_kill_thread,
                             2,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "tid",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "sig");

    com_sys_syscall_register(0x20, "sigreturn", com_sys_syscall_sigreturn, 0);

    com_sys_syscall_register(0x21,
                             "dup3",
                             com_sys_syscall_dup3,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "old_fd",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "new_fd",
                             COM_SYS_SYSCALL_TYPE_FLAGS,
                             "flags");

    com_sys_syscall_register(0x22,
                             "getcwd",
                             com_sys_syscall_getcwd,
                             2,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "buf",
                             COM_SYS_SYSCALL_TYPE_SIZET,
                             "buflen");

    com_sys_syscall_register(0x23,
                             "fcntl",
                             com_sys_syscall_fcntl,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd",
                             COM_SYS_SYSCALL_TYPE_FLAGS,
                             "op",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "arg1");

    com_sys_syscall_register(0x24,
                             "close",
                             com_sys_syscall_close,
                             1,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd");

    com_sys_syscall_register(0x25,
                             "readdir",
                             com_sys_syscall_readdir,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "fd",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "buf",
                             COM_SYS_SYSCALL_TYPE_SIZET,
                             "buflen");

    com_sys_syscall_register(0x26,
                             "chdir",
                             com_sys_syscall_chdir,
                             1,
                             COM_SYS_SYSCALL_TYPE_STR,
                             "path");

    com_sys_syscall_register(0x27,
                             "clock_get",
                             com_sys_syscall_clock_get,
                             2,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "clock",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "timespec");

    com_sys_syscall_register(0x28,
                             "faccessat",
                             com_sys_syscall_faccessat,
                             2,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "dir_fd",
                             COM_SYS_SYSCALL_TYPE_STR,
                             "path");

    com_sys_syscall_register(0x29,
                             "ppoll",
                             com_sys_syscall_poll,
                             3,
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "fds",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "nfds",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "timespec");

    com_sys_syscall_register(0x2A,
                             "readlinkat",
                             com_sys_syscall_readlinkat,
                             4,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "dir_fd",
                             COM_SYS_SYSCALL_TYPE_STR,
                             "path",
                             COM_SYS_SYSCALL_TYPE_PTR,
                             "buf",
                             COM_SYS_SYSCALL_TYPE_SIZET,
                             "buflen");

    com_sys_syscall_register(0x2B,
                             "symlinkat",
                             com_sys_syscall_symlinkat,
                             3,
                             COM_SYS_SYSCALL_TYPE_STR,
                             "filepath",
                             COM_SYS_SYSCALL_TYPE_INT,
                             "dir_fd",
                             COM_SYS_SYSCALL_TYPE_STR,
                             "symlink");

    com_sys_syscall_register(0x2C,
                             "unlinkat",
                             com_sys_syscall_unlinkat,
                             3,
                             COM_SYS_SYSCALL_TYPE_INT,
                             "dir_fd",
                             COM_SYS_SYSCALL_TYPE_STR,
                             "path",
                             COM_SYS_SYSCALL_TYPE_FLAGS,
                             "flags");

    com_sys_interrupt_register(0x80, arch_syscall_handle, NULL);
}
