#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <sys/socket.h>

// SYSCALL: connect(int sockfd,  struct sockaddr *addr, socklen_t addr_len);
SYSCALL(syscall_connect) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int sockfd = SYSCALL_ARG(int, 1);
    struct sockaddr *addr = SYSCALL_ARG(void *, 2);
    socklen_t addr_len = SYSCALL_ARG(socklen_t, 3);

    struct sockaddr_storage kaddr;
    if (addr_len < sizeof(sa_family_t) || addr_len > sizeof(kaddr)) {
        return SYSCALL_ERR(EINVAL);
    }
    int copy_ret = memcpy_from_user(&kaddr, addr, addr_len);
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

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

    SocketAddr sys_addr;
    int sock_ret = socket_addr_from_abi((struct sockaddr *)&kaddr, addr_len,
                                        &sys_addr);
    if (sock_ret != 0) {
        ret = SYSCALL_ERR(sock_ret);
        goto end;
    }

    sock_ret = SOCKET_CONNECT(socket, &sys_addr);
    if (sock_ret != 0) ret = SYSCALL_ERR(sock_ret);

end:
    FILE_REL(sockfile);
    return ret;
}
