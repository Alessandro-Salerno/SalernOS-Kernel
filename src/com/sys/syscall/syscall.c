#include <arch/context.h>
#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/syscall.h>

#include <salernos/syscall_str.h>

// NOTE: not static so others can call
Syscall g_syscall_table[CONFIG_SYSCALL_MAX];
static SyscallAux g_syscall_aux_table[CONFIG_SYSCALL_MAX];
static usize g_num_syscalls = 0;

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_SYSCALL
#define LOG_STR_MAX 64

#if CONFIG_LOG_SYSCALL_MODE == CONST_LOG_SYSCALL_BEFORE
static bool log_is_mapped(const void *ptr) {
    Proc *curr_proc = cpu_get_thread()->proc;
    return mmu_get_physical(curr_proc->vmm_context->pagetable, (void *)ptr,
                            NULL, NULL) != NULL;
}
#endif

// String arguments are copied before taking the log lock. Logging is only built
// into debug kernels, so it trusts the user pointers instead of using usercopy.
// In this context, usercopy mainly protects us against muti-threaded processes
// unmapping the string memory under our feet, but we assume that not to be the
// case when debugging the kernel. The yield would be minimal and the effort
// would be considerable.
static void fetch_str_args(char strs[][LOG_STR_MAX], SyscallAux *aux,
                           SyscallArg args[]) {
    for (usize i = 0; i < aux->num_args; i++) {
        const struct syscall_str *ustr = (const void *)args[i];
        if (aux->arg_types[i] != SYSCALL_TYPE_STR) continue;
#if CONFIG_LOG_SYSCALL_MODE == CONST_LOG_SYSCALL_BEFORE
        if (ustr == NULL) {
            strcpy(strs[i], "NULL");
            continue;
        }
        if (!log_is_mapped(ustr) ||
            (ustr->len != 0 && !log_is_mapped(ustr->str))) {
            strcpy(strs[i], "**UNMAPPED**");
            continue;
        }
        const char *str = ustr->str;
        usize len = min(ustr->len, LOG_STR_MAX - 3);
        usize n = 0;
        strs[i][n++] = '"';
        for (usize j = 0; j < len; j++) strs[i][n++] = str[j];
        strs[i][n++] = '"';
        strs[i][n] = '\0';
#else
        (void)ustr;
        strcpy(strs[i], "**UNSAFE**");
#endif
    }
}

static void log_syscall(SyscallAux *aux, SyscallArg args[],
                        char strs[][LOG_STR_MAX]) {
    printf("%s(", aux->name);

    for (usize i = 0; i < aux->num_args; i++) {
        int arg_type = aux->arg_types[i];
        const char *arg_name = aux->arg_names[i];
        printf("%s = ", arg_name);

        switch (arg_type) {
            case SYSCALL_TYPE_INT:
                printf("%d", (int)args[i]);
                break;
            case SYSCALL_TYPE_UINTMAX:
                printf("%zu", (u64)args[i]);
                break;
            case SYSCALL_TYPE_SIZET:
                printf("%zu", (usize)args[i]);
                break;
            case SYSCALL_TYPE_PTR: {
                if ((void *)args[i] != NULL) printf("%p", (void *)args[i]);
                else printf("NULL");
                break;
            }
            case SYSCALL_TYPE_STR:
                printf("%s", strs[i]);
                break;
            case SYSCALL_TYPE_FLAGS:
            case SYSCALL_TYPE_MODE:
                printf("0x%X", (int)args[i]);
                break;
            case SYSCALL_TYPE_LONGFLAGS:
                printf("0x%X", (u64)args[i]);
                break;
            case SYSCALL_TYPE_OFFT:
                printf("%d", (off_t)args[i]);
                break;
            case SYSCALL_TYPE_UINT32:
                printf("%zu", (u32)args[i]);
                break;
            default:
                printf("?");
                break;
        }

        if (i != aux->num_args - 1) printf(", ");
    }

    printf(")");
}
#endif

void syscall_register(u64 number, const char *name, SyscallFn handler,
                      usize num_args, ...) {
    assert(number < CONFIG_SYSCALL_MAX);
    Syscall *syscall = &g_syscall_table[number];
    SyscallAux *aux = &g_syscall_aux_table[number];
    syscall->handler = handler;

    aux->name = name;
    aux->num_args = num_args;
    // Numbers may have gaps, so this is the size of the table, not a count
    g_num_syscalls = max(g_num_syscalls, number + 1);

    if (num_args == 0) return;

    va_list args;
    va_start(args, num_args);
    for (usize i = 0; i < num_args; i++) {
        int arg_type = va_arg(args, int);
        const char *arg_name = va_arg(args, const char *);
        aux->arg_names[i] = arg_name;
        aux->arg_types[i] = arg_type;
    }
    va_end(args);
}

SyscallRet syscall_invoke(u64 number, Context *ctx, SyscallArg arg1,
                          SyscallArg arg2, SyscallArg arg3, SyscallArg arg4,
                          SyscallArg arg5, SyscallArg arg6, uptr invoke_ip) {
    // The number comes from user space
    if (number >= CONFIG_SYSCALL_MAX ||
        g_syscall_table[number].handler == NULL) {
        return SYSCALL_ERR(ENOSYS);
    }
    Syscall *syscall = &g_syscall_table[number];

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_SYSCALL
    SyscallAux *aux = &g_syscall_aux_table[number];
    SyscallArg args[6] = {arg1, arg2, arg3, arg4, arg5, arg6};
    char strs[6][LOG_STR_MAX];
    fetch_str_args(strs, aux, args);
#if CONFIG_LOG_SYSCALL_MODE == CONST_LOG_SYSCALL_BEFORE
    log_lock();
    log_header_nolock("SYSCALL", "\033[33m");
    log_syscall(aux, args, strs);
    printf(" at %p\n", invoke_ip);
    log_reset_color_nolock();
    log_unlock();
#else
    (void)invoke_ip;
#define DO_SYSCALL_LOG_AFTER
#endif
#else
    (void)invoke_ip;
#endif

    ProfilerData profiler_data = profiler_start_syscall(number);
    SyscallRet ret = syscall->handler(ctx, arg1, arg2, arg3, arg4, arg5, arg6);
    profiler_end_syscall(&profiler_data);

#ifdef DO_SYSCALL_LOG_AFTER
    log_lock();
    log_header_nolock("SYSCALL", "\033[33m");
    log_syscall(aux, args, strs);
    printf(" -> {ret = %p, errno = %zu}\n", ret.value, ret.err);
    log_reset_color_nolock();
    log_unlock();
#undef DO_SYSCALL_LOG_AFTER
#endif
    return ret;
}

int syscall_copy_str(char *buf, usize bufsz, const struct syscall_str *ustr,
                     usize *out_len) {
    struct syscall_str str;
    int ret = memcpy_from_user(&str, ustr, sizeof(str));
    if (ret != 0) return ret;
    if (str.len >= bufsz) return ENAMETOOLONG;

    ret = memcpy_from_user(buf, str.str, str.len);
    if (ret != 0) return ret;
    if (memchr(buf, '\0', str.len) != NULL) return EINVAL;

    buf[str.len] = '\0';
    *out_len = str.len;
    return 0;
}

void syscall_get_tables(Syscall **out_syscall_table, SyscallAux **out_aux_table,
                        usize *out_num_syscalls) {
    if (out_syscall_table != NULL) *out_syscall_table = g_syscall_table;
    if (out_aux_table != NULL) *out_aux_table = g_syscall_aux_table;
    if (out_num_syscalls != NULL) *out_num_syscalls = g_num_syscalls;
}

void syscall_init(void) {
    log("initializing common system calls");

    syscall_register(0x00, "kprint", syscall_kprint, 1, SYSCALL_TYPE_PTR,
                     "message");

    syscall_register(0x01, "writev", syscall_writev, 3, SYSCALL_TYPE_INT, "fd",
                     SYSCALL_TYPE_PTR, "iov", SYSCALL_TYPE_INT, "iovcnt");

    syscall_register(0x02, "readv", syscall_readv, 3, SYSCALL_TYPE_INT, "fd",
                     SYSCALL_TYPE_PTR, "iov", SYSCALL_TYPE_INT, "iovcnt");

    syscall_register(0x03, "execve", syscall_execve, 5, SYSCALL_TYPE_STR,
                     "path", SYSCALL_TYPE_PTR, "argv", SYSCALL_TYPE_SIZET,
                     "argc", SYSCALL_TYPE_PTR, "envp", SYSCALL_TYPE_SIZET,
                     "envc");

    syscall_register(0x04, "fork", syscall_fork, 0);

    syscall_register(0x05, "sysinfo", syscall_sysinfo, 1, SYSCALL_TYPE_PTR,
                     "sysinfo_struct");

    syscall_register(0x06, "waitpid", syscall_waitpid, 3, SYSCALL_TYPE_INT,
                     "pid", SYSCALL_TYPE_PTR, "status", SYSCALL_TYPE_FLAGS,
                     "flags");

    syscall_register(0x07, "exit", syscall_exit, 1, SYSCALL_TYPE_INT, "ecode");

    syscall_register(0x08, "ioctl", syscall_ioctl, 3, SYSCALL_TYPE_INT, "fd",
                     SYSCALL_TYPE_LONGFLAGS, "op", SYSCALL_TYPE_PTR, "buf");

    syscall_register(0x09, "openat", syscall_openat, 3, SYSCALL_TYPE_INT,
                     "dir_fd", SYSCALL_TYPE_STR, "path", SYSCALL_TYPE_FLAGS,
                     "flags");

    syscall_register(0x0A, "mmap", syscall_mmap, 6, SYSCALL_TYPE_PTR, "hint",
                     SYSCALL_TYPE_SIZET, "size", SYSCALL_TYPE_FLAGS, "flags",
                     SYSCALL_TYPE_FLAGS, "prot", SYSCALL_TYPE_INT, "fd",
                     SYSCALL_TYPE_OFFT, "off");

    syscall_register(0x0B, "set_tls", syscall_set_tls, 1, SYSCALL_TYPE_PTR,
                     "tls");

    syscall_register(0x0C, "seek", syscall_seek, 3, SYSCALL_TYPE_INT, "fd",
                     SYSCALL_TYPE_OFFT, "off", SYSCALL_TYPE_INT, "whence");

    syscall_register(0x0D, "isatty", syscall_isatty, 1, SYSCALL_TYPE_INT, "fd");

    syscall_register(0x0E, "fstatat", syscall_fstatat, 4, SYSCALL_TYPE_INT,
                     "dir_fd", SYSCALL_TYPE_STR, "path", SYSCALL_TYPE_PTR,
                     "statbuf", SYSCALL_TYPE_FLAGS, "flags");

    syscall_register(0x0F, "ftruncate", syscall_truncate, 2, SYSCALL_TYPE_INT,
                     "fd", SYSCALL_TYPE_OFFT, "size");

    syscall_register(0x10, "pipe2", syscall_pipe2, 2, SYSCALL_TYPE_PTR,
                     "fildes", SYSCALL_TYPE_FLAGS, "flags");

    syscall_register(0x11, "getpid", syscall_getpid, 0);

    syscall_register(0x12, "clone", syscall_clone, 2, SYSCALL_TYPE_PTR,
                     "new_ip", SYSCALL_TYPE_PTR, "new_sp");

    syscall_register(0x13, "exit_thread", syscall_exit_thread, 0);

    syscall_register(0x14, "futex", syscall_futex, 3, SYSCALL_TYPE_PTR,
                     "word_ptr", SYSCALL_TYPE_INT, "op", SYSCALL_TYPE_UINT32,
                     "value");

    syscall_register(0x15, "getppid", syscall_getppid, 0);

    syscall_register(0x16, "getpgid", syscall_getpgid, 1, SYSCALL_TYPE_INT,
                     "pid");

    syscall_register(0x17, "getsid", syscall_getsid, 1, SYSCALL_TYPE_INT,
                     "pid");

    syscall_register(0x18, "setpgid", syscall_setpgid, 2, SYSCALL_TYPE_INT,
                     "pid", SYSCALL_TYPE_INT, "pgid");

    syscall_register(0x19, "setsid", syscall_setsid, 0);

    syscall_register(0x1A, "sigprocmask", syscall_sigprocmask, 3,
                     SYSCALL_TYPE_INT, "how", SYSCALL_TYPE_PTR, "set",
                     SYSCALL_TYPE_PTR, "oset");

    syscall_register(0x1B, "sigpending", syscall_sigpending, 1,
                     SYSCALL_TYPE_PTR, "set");

    // 0x1C used to be sigthreadmask(), it's now free to use
    syscall_register(0x1C, "(unused)", NULL, 0);

    syscall_register(0x1D, "sigaction", syscall_sigaction, 3, SYSCALL_TYPE_INT,
                     "sig", SYSCALL_TYPE_PTR, "act", SYSCALL_TYPE_PTR, "oact");

    syscall_register(0x1E, "kill", syscall_kill, 2, SYSCALL_TYPE_INT, "pid",
                     SYSCALL_TYPE_INT, "sig");

    syscall_register(0x1F, "kill_thread", syscall_kill_thread, 2,
                     SYSCALL_TYPE_INT, "tid", SYSCALL_TYPE_INT, "sig");

    syscall_register(0x20, "sigreturn", syscall_sigreturn, 0);

    syscall_register(0x21, "dup3", syscall_dup3, 3, SYSCALL_TYPE_INT, "old_fd",
                     SYSCALL_TYPE_INT, "new_fd", SYSCALL_TYPE_FLAGS, "flags");

    syscall_register(0x22, "getcwd", syscall_getcwd, 2, SYSCALL_TYPE_PTR, "buf",
                     SYSCALL_TYPE_SIZET, "buflen");

    syscall_register(0x23, "fcntl", syscall_fcntl, 3, SYSCALL_TYPE_INT, "fd",
                     SYSCALL_TYPE_FLAGS, "op", SYSCALL_TYPE_INT, "arg1");

    syscall_register(0x24, "close", syscall_close, 1, SYSCALL_TYPE_INT, "fd");

    syscall_register(0x25, "readdir", syscall_readdir, 3, SYSCALL_TYPE_INT,
                     "fd", SYSCALL_TYPE_PTR, "buf", SYSCALL_TYPE_SIZET,
                     "buflen");

    syscall_register(0x26, "chdir", syscall_chdir, 1, SYSCALL_TYPE_STR, "path");

    syscall_register(0x27, "clock_get", syscall_clock_get, 2, SYSCALL_TYPE_INT,
                     "clock", SYSCALL_TYPE_PTR, "timespec");

    syscall_register(0x28, "faccessat", syscall_faccessat, 2, SYSCALL_TYPE_INT,
                     "dir_fd", SYSCALL_TYPE_STR, "path");

    syscall_register(0x29, "ppoll", syscall_poll, 3, SYSCALL_TYPE_PTR, "fds",
                     SYSCALL_TYPE_INT, "nfds", SYSCALL_TYPE_PTR, "timespec");

    syscall_register(0x2A, "readlinkat", syscall_readlinkat, 4,
                     SYSCALL_TYPE_INT, "dir_fd", SYSCALL_TYPE_STR, "path",
                     SYSCALL_TYPE_PTR, "buf", SYSCALL_TYPE_SIZET, "buflen");

    syscall_register(0x2B, "symlinkat", syscall_symlinkat, 3, SYSCALL_TYPE_STR,
                     "filepath", SYSCALL_TYPE_INT, "dir_fd", SYSCALL_TYPE_STR,
                     "symlink");

    syscall_register(0x2C, "unlinkat", syscall_unlinkat, 3, SYSCALL_TYPE_INT,
                     "dir_fd", SYSCALL_TYPE_STR, "path", SYSCALL_TYPE_FLAGS,
                     "flags");

    syscall_register(0x2D, "socket", syscall_socket, 3, SYSCALL_TYPE_INT,
                     "domain", SYSCALL_TYPE_INT, "type", SYSCALL_TYPE_INT,
                     "protocol");

    syscall_register(0x2E, "bind", syscall_bind, 3, SYSCALL_TYPE_INT, "sockfd",
                     SYSCALL_TYPE_PTR, "addr", SYSCALL_TYPE_SIZET, "addr_len");

    syscall_register(0x2F, "listen", syscall_listen, 2, SYSCALL_TYPE_INT,
                     "sockfd", SYSCALL_TYPE_SIZET, "max_connections");

    syscall_register(0x30, "accept", syscall_accept, 3, SYSCALL_TYPE_INT,
                     "sockfd", SYSCALL_TYPE_PTR, "addr", SYSCALL_TYPE_PTR,
                     "addrlen_ptr");

    syscall_register(0x31, "connect", syscall_connect, 3, SYSCALL_TYPE_INT,
                     "sockfd", SYSCALL_TYPE_PTR, "addr", SYSCALL_TYPE_SIZET,
                     "addr_len");

    syscall_register(0x32, "sendmsg", syscall_sendmsg, 3, SYSCALL_TYPE_INT,
                     "sockfd", SYSCALL_TYPE_PTR, "msg", SYSCALL_TYPE_FLAGS,
                     "flags");

    syscall_register(0x33, "recvmsg", syscall_recvmsg, 3, SYSCALL_TYPE_INT,
                     "sockfd", SYSCALL_TYPE_PTR, "msg", SYSCALL_TYPE_FLAGS,
                     "flags");

    syscall_register(0x34, "setitimer", syscall_setitimer, 3, SYSCALL_TYPE_INT,
                     "which", SYSCALL_TYPE_PTR, "new_val", SYSCALL_TYPE_PTR,
                     "old_val");

    syscall_register(0x35, "mkdirat", syscall_mkdirat, 3, SYSCALL_TYPE_INT,
                     "dir_fd", SYSCALL_TYPE_STR, "path", SYSCALL_TYPE_MODE,
                     "mode");

    syscall_register(0x36, "getpeername", syscall_getpeername, 3,
                     SYSCALL_TYPE_INT, "sockfd", SYSCALL_TYPE_PTR, "addr",
                     SYSCALL_TYPE_SIZET, "max_addrlen");

    syscall_register(0x37, "munmap", syscall_munmap, 2, SYSCALL_TYPE_PTR,
                     "addr", SYSCALL_TYPE_SIZET, "len");

    syscall_register(0x38, "linkat", syscall_linkat, 5, SYSCALL_TYPE_INT,
                     "old_dir_fd", SYSCALL_TYPE_STR, "oldpath",
                     SYSCALL_TYPE_INT, "new_dir_fd", SYSCALL_TYPE_STR,
                     "newpath", SYSCALL_TYPE_FLAGS, "flags");

    syscall_register(0x39, "renameat", syscall_renameat, 4, SYSCALL_TYPE_INT,
                     "old_dir_fd", SYSCALL_TYPE_STR, "oldpath",
                     SYSCALL_TYPE_INT, "new_dir_fd", SYSCALL_TYPE_STR,
                     "newpath");

    syscall_register(0x3A, "mount", syscall_mount, 5, SYSCALL_TYPE_STR,
                     "source", SYSCALL_TYPE_STR, "target", SYSCALL_TYPE_STR,
                     "fstype", SYSCALL_TYPE_LONGFLAGS, "flags",
                     SYSCALL_TYPE_PTR, "data");

    syscall_register(0x3B, "umount2", syscall_umount2, 2, SYSCALL_TYPE_STR,
                     "target", SYSCALL_TYPE_FLAGS, "flags");
}
