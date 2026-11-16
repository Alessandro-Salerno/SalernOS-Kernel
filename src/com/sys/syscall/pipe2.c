#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/pipefs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: pipe2(int *fildes, int flags);
SYSCALL(syscall_pipe2) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    int *fildes = SYSCALL_ARG(int *, 1);
    int flags = SYSCALL_ARG(int, 2);

    Proc *curr = cpu_get_thread()->proc;
    int rfd = proc_next_fd(curr);
    int wfd = rfd != -1 ? proc_next_fd(curr) : -1;

    if (wfd == -1) {
        if (rfd != -1) proc_unreserve_fd(curr, rfd);
        return SYSCALL_ERR(EMFILE);
    }

    File *rf = slab_alloc(sizeof(File));
    File *wf = slab_alloc(sizeof(File));
    Vnode *read;
    Vnode *write;
    if (rf == NULL || wf == NULL || !pipefs_new(&read, &write)) {
        slab_free(rf, sizeof(File));
        slab_free(wf, sizeof(File));
        proc_unreserve_fd(curr, rfd);
        proc_unreserve_fd(curr, wfd);
        return SYSCALL_ERR(ENOMEM);
    }

    rf->vnode = read;
    rf->flags = flags & O_NONBLOCK;
    rf->num_ref = 1;
    rf->off = 0;

    wf->vnode = write;
    wf->flags = flags & O_NONBLOCK;
    wf->num_ref = 1;
    wf->off = 0;

    u64 fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    proc_install_fd(curr, rfd, rf, fd_flags);
    proc_install_fd(curr, wfd, wf, fd_flags);

    int kfildes[2] = {rfd, wfd};
    int copy_ret = memcpy_to_user(fildes, kfildes, sizeof(kfildes));
    if (copy_ret != 0) {
        proc_close_file(curr, rfd);
        proc_close_file(curr, wfd);
        return SYSCALL_ERR(copy_ret);
    }

    log_debug("returning fd=(%d, %d), read_vn=%p, write_vn=%p", rfd, wfd, read,
              write);

    return SYSCALL_OK(0);
}
