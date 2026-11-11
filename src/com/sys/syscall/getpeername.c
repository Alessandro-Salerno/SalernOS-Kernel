#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: getpeername(int sockfd, struct sockaddr *addr, socklen_t
// max_addrlen)
SYSCALL(syscall_getpeername) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int sockfd = SYSCALL_ARG(int, 1);
    struct sockaddr *addr = SYSCALL_ARG(void *, 2);
    socklen_t max_addrlen = SYSCALL_ARG(socklen_t, 3);

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

    SocketAddr peer_addr;
    int err = SOCKET_GETPEERNAME(socket, &peer_addr);
    if (err != 0) {
        ret = SYSCALL_ERR(err);
        goto end;
    }

    struct sockaddr_storage kaddr = {0};
    socklen_t addr_len;
    err = socket_abi_from_addr((struct sockaddr *)&kaddr, &peer_addr,
                               socket->type, max_addrlen, &addr_len);
    if (err == 0) {
        err = memcpy_to_user(addr, &kaddr, min(max_addrlen, sizeof(kaddr)));
    }
    if (err != 0) {
        ret = SYSCALL_ERR(err);
        goto end;
    }

    ret = SYSCALL_OK(addr_len);

end:
    FILE_REL(sockfile);
    return ret;
}
