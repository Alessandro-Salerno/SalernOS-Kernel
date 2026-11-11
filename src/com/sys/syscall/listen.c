#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: listen(int sockfd, usize max_connections)
SYSCALL(syscall_listen) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    int sockfd = SYSCALL_ARG(int, 1);
    usize max_connesctions = SYSCALL_ARG(usize, 2);

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

    SocketVnode *socket_vn = (void *)sockfile->vnode;
    assert(socket_vn->socket != NULL);
    Socket *socket = socket_vn->socket;

    int sock_ret = SOCKET_LISTEN(socket, max_connesctions);
    if (sock_ret != 0) {
        ret = SYSCALL_ERR(sock_ret);
        goto end;
    }

end:
    FILE_REL(sockfile);
    return ret;
}
