#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: fchdir(int fd)
SYSCALL(syscall_fchdir) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    int fd = SYSCALL_ARG(int, 1);
    Proc *curr_proc = cpu_get_thread()->proc;

    File *file = proc_get_file(curr_proc, fd);
    if (file == NULL) return SYSCALL_ERR(EBADF);

    int err = 0;
    if (file->vnode->type == VNODE_TYPE_DIR)
        proc_set_cwd(curr_proc, file->vnode);
    else err = ENOTDIR;

    FILE_REL(file);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
