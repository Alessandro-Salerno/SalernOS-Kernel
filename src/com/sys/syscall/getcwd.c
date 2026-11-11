#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>
#include <stdio.h>

// SYSCALL: getcwd(char *buf, usize size)
SYSCALL(syscall_getcwd) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    char *buf = SYSCALL_ARG(char *, 1);
    usize size = SYSCALL_ARG(usize, 2);

    if (size < 1) return SYSCALL_ERR(ERANGE);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;
    Vnode *cwd = proc_get_cwd(curr_proc);

    // A path that does not fit the user's buffer is ERANGE, one that does not
    // fit ours is ENAMETOOLONG
    char kbuf[CONFIG_PATH_MAX];
    usize ksize = min(size, sizeof(kbuf));
    usize start = 0;
    int err = vfs_get_path(kbuf, ksize, cwd, curr_proc->root, &start);
    vfs_vnode_rel(cwd);
    if (err == ERANGE && ksize != size) err = ENAMETOOLONG;
    if (err == 0) err = memcpy_to_user(buf, &kbuf[start], ksize - start);
    if (err != 0) return SYSCALL_ERR(err);

    return SYSCALL_OK((uptr)buf);
}
