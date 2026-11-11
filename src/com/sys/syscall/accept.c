#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <sys/socket.h>

// SYSCALL: accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen_ptr)
SYSCALL(syscall_accept) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int sockfd = SYSCALL_ARG(int, 1);
    struct sockaddr *addr = SYSCALL_ARG(void *, 2);
    socklen_t *addr_len = SYSCALL_ARG(void *, 3);

    // TODO: handle addr output
    (void)addr;
    (void)addr_len;

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    SyscallRet ret = SYSCALL_BASE_OK();

    File *sockfile = proc_get_file(curr_proc, sockfd);
    if (sockfile == NULL) {
        ret = SYSCALL_ERR(EBADF);
        goto end;
    }

    if (sockfile->vnode == NULL || sockfile->vnode->type != VNODE_TYPE_SOCKET) {
        ret = SYSCALL_ERR(ENOTSOCK);
        goto end;
    }

    SocketVnode *server_vn = (void *)sockfile->vnode;
    assert(server_vn->socket != NULL);
    Socket *server_socket = server_vn->socket;

    // Reserved first, so that an accepted connection is never dropped for
    // lack of a descriptor
    int new_fd = proc_next_fd(curr_proc);
    if (new_fd == -1) {
        ret = SYSCALL_ERR(EMFILE);
        goto end;
    }

    SocketAddr new_client_addr;
    Socket *new_client;
    int sock_ret = SOCKET_ACCEPT(server_socket, sockfile->flags, &new_client,
                                 &new_client_addr);
    if (sock_ret != 0) {
        proc_unreserve_fd(curr_proc, new_fd);
        ret = SYSCALL_ERR(sock_ret);
        goto end;
    }

    Vnode *client_vn = sockfs_new();
    SocketVnode *sockfs_vn = (void *)client_vn;
    sockfs_vn->socket = new_client;

    File *new_file = slab_alloc(sizeof(File));
    new_file->vnode = client_vn;
    new_file->flags = 0;
    new_file->num_ref = 1;
    new_file->off = 0;
    proc_install_fd(curr_proc, new_fd, new_file, 0);

    ret = SYSCALL_OK(new_fd);

end:
    FILE_REL(sockfile);
    return ret;
}
