#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: socket(int domain, int type, int protocol)
SYSCALL(syscall_socket) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int domain = SYSCALL_ARG(int, 1);
    int type = SYSCALL_ARG(int, 2);
    int protocol = SYSCALL_ARG(int, 3);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    SocketType sys_type = socket_type_from_abi(domain, type, protocol);
    if (sys_type == SOCKET_TYPE_INVAL) return SYSCALL_ERR(EAFNOSUPPORT);

    int fd = proc_next_fd(curr_proc);
    if (fd == -1) return SYSCALL_ERR(EMFILE);

    Socket *socket = socket_new(sys_type);
    Vnode *socket_vn = sockfs_new();
    SocketVnode *sockfs_vn = (void *)socket_vn;
    sockfs_vn->socket = socket;

    File *file = slab_alloc(sizeof(File));
    file->vnode = socket_vn;
    file->flags = 0;
    file->num_ref = 1;
    file->off = 0;

    // Convert OSKC_* flags into O_* flags
    if (type & SOCK_NONBLOCK) file->flags |= O_NONBLOCK;
    if (type & SOCK_CLOEXEC) file->flags |= O_CLOEXEC;

    proc_install_fd(curr_proc, fd, file, 0);
    return SYSCALL_OK(fd);
}
