#pragma once

#include <arch/context.h>
#include <arch/syscall.h>
#include <kernel/com/util.h>

#define SYSCALL(name)                                                 \
    SyscallRet name(Context *__syscall_ctx, SyscallArg __syscall_a1,  \
                    SyscallArg __syscall_a2, SyscallArg __syscall_a3, \
                    SyscallArg __syscall_a4, SyscallArg __syscall_a5, \
                    SyscallArg __syscall_a6)
#define SYSCALL_CONTEXT()                  (__syscall_ctx)
#define SYSCALL_ARG(type, num)             ((type)(__syscall_a##num))
#define SYSCALL_UNUSED_CONTEXT()           (void)(SYSCALL_CONTEXT())
#define SYSCALL_UNUSED_START(first_unused) __SYSCALL_UNUSED_##first_unused

#define __SYSCALL_UNUSED_6 (void)(__syscall_a6)
#define __SYSCALL_UNUSED_5 \
    __SYSCALL_UNUSED_6;    \
    (void)(__syscall_a5)
#define __SYSCALL_UNUSED_4 \
    __SYSCALL_UNUSED_5;    \
    (void)(__syscall_a4)
#define __SYSCALL_UNUSED_3 \
    __SYSCALL_UNUSED_4;    \
    (void)(__syscall_a3)
#define __SYSCALL_UNUSED_2 \
    __SYSCALL_UNUSED_3;    \
    (void)(__syscall_a2)
#define __SYSCALL_UNUSED_1 \
    __SYSCALL_UNUSED_2;    \
    (void)(__syscall_a1)
#define __SYSCALL_UNUSED_0    \
    SYSCALL_UNUSED_CONTEXT(); \
    __SYSCALL_UNUSED_1

#define SYSCALL_OK(v) \
    (SyscallRet) { .value = (u64)(v), .err = 0, .discarded = false }
#define SYSCALL_ERR(e) \
    (SyscallRet) { .value = -1, .err = (e), .discarded = false }
#define SYSCALL_DISCARD() \
    (SyscallRet) { .discarded = true }

#define SYSCALL_BASE_OK()  {0, 0, false};
#define SYSCALL_BASE_ERR() {-1, 0, false}

#define SYSCALL_TYPE_INT       0
#define SYSCALL_TYPE_UINTMAX   1
#define SYSCALL_TYPE_SIZET     2
#define SYSCALL_TYPE_PTR       3
#define SYSCALL_TYPE_STR       4
#define SYSCALL_TYPE_FLAGS     5
#define SYSCALL_TYPE_LONGFLAGS 6
#define SYSCALL_TYPE_OFFT      7
#define SYSCALL_TYPE_UINT32    8
#define SYSCALL_TYPE_MODE      9

typedef struct {
    u64 value;
    u64 err;
    bool discarded; // If true, ret.value and ret.err shall not be stored in
                    // return value registers by the arch-specific system call
                    // dispatcher
} SyscallRet;

typedef SyscallRet (*SyscallFn)(Context *ctx, SyscallArg a1, SyscallArg a2,
                                SyscallArg a3, SyscallArg a4, SyscallArg a5,
                                SyscallArg a6);

typedef struct SyscallAux {
    const char *name;
    usize num_args;
    const char *arg_names[6];
    int arg_types[6];
} SyscallAux;

typedef struct Syscall {
    SyscallFn handler;
} Syscall;

void syscall_register(u64 number, const char *name, SyscallFn handler,
                      usize num_args, ...);
SyscallRet syscall_invoke(u64 number, Context *ctx, SyscallArg arg1,
                          SyscallArg arg2, SyscallArg arg3, SyscallArg arg4,
                          SyscallArg a5, SyscallArg a6, uptr invoke_ip);
void syscall_get_tables(Syscall **out_syscall_table, SyscallAux **out_aux_table,
                        usize *out_num_syscalls);
void syscall_init(void);

// SYSCALLS

SYSCALL(syscall_kprint);
SYSCALL(syscall_writev);
SYSCALL(syscall_readv);
SYSCALL(syscall_execve);
SYSCALL(syscall_fork);
SYSCALL(syscall_sysinfo);
SYSCALL(syscall_waitpid);
SYSCALL(syscall_exit);
SYSCALL(syscall_ioctl);
SYSCALL(syscall_openat);
SYSCALL(syscall_mmap);
SYSCALL(syscall_seek);
SYSCALL(syscall_isatty);
SYSCALL(syscall_fstatat);
SYSCALL(syscall_truncate);
SYSCALL(syscall_pipe2);
SYSCALL(syscall_getpid);
SYSCALL(syscall_clone);
SYSCALL(syscall_exit_thread);
SYSCALL(syscall_futex);
SYSCALL(syscall_getppid);
SYSCALL(syscall_getpgid);
SYSCALL(syscall_getsid);
SYSCALL(syscall_setpgid);
SYSCALL(syscall_setsid);
SYSCALL(syscall_sigprocmask);
SYSCALL(syscall_sigpending);
SYSCALL(syscall_sigaction);
SYSCALL(syscall_kill);
SYSCALL(syscall_kill_thread);
SYSCALL(syscall_sigreturn);
SYSCALL(syscall_dup3);
SYSCALL(syscall_getcwd);
SYSCALL(syscall_fcntl);
SYSCALL(syscall_close);
SYSCALL(syscall_readdir);
SYSCALL(syscall_chdir);
SYSCALL(syscall_clock_get);
SYSCALL(syscall_faccessat);
SYSCALL(syscall_poll);
SYSCALL(syscall_readlinkat);
SYSCALL(syscall_symlinkat);
SYSCALL(syscall_unlinkat);
SYSCALL(syscall_socket);
SYSCALL(syscall_bind);
SYSCALL(syscall_listen);
SYSCALL(syscall_accept);
SYSCALL(syscall_connect);
SYSCALL(syscall_sendmsg);
SYSCALL(syscall_recvmsg);
SYSCALL(syscall_setitimer);
SYSCALL(syscall_mkdirat);
SYSCALL(syscall_getpeername);
SYSCALL(syscall_munmap);
SYSCALL(syscall_linkat);
SYSCALL(syscall_renameat);
SYSCALL(syscall_mount);
SYSCALL(syscall_umount2);
SYSCALL(syscall_sysprofile);

// SYSCALL UTILITIES

struct syscall_str;

int syscall_copy_str(char *buf, usize bufsz, const struct syscall_str *ustr,
                     usize *out_len);
void syscall_futex_init(void);
