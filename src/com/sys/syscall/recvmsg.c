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

// SYSCALL: recvmsg(int sockfd, struct msghdr *msg, int flags)
SYSCALL(syscall_recvmsg) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int sockfd = SYSCALL_ARG(int, 1);
    struct msghdr *msg = SYSCALL_ARG(void *, 2);
    int flags = SYSCALL_ARG(int, 3);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    struct msghdr kmsg;
    int copy_ret = memcpy_from_user(&kmsg, msg, sizeof(kmsg));
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

    IOVIter ioviter;
    copy_ret = ioviter_init_user(&ioviter, curr_thread->scratch, kmsg.msg_iov,
                                 kmsg.msg_iovlen);
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

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

    SocketDesc desc = {.ioviter = &ioviter,
                       .count = ioviter.total_size,
                       .count_done = 0,
                       .flags = sockfile->flags,
                       .msgflags = flags,
                       .addr = NULL};

    int sock_ret = SOCKET_RECV(socket, &desc);
    if (sock_ret != 0) {
        ret = SYSCALL_ERR(sock_ret);
        goto end;
    }

    kmsg.msg_controllen = 0;
    kmsg.msg_namelen = 0;
    kmsg.msg_flags = 0;
    copy_ret = memcpy_to_user(msg, &kmsg, sizeof(kmsg));
    if (copy_ret != 0) {
        ret = SYSCALL_ERR(copy_ret);
        goto end;
    }
    ret = SYSCALL_OK(desc.count_done);

end:
    FILE_REL(sockfile);
    return ret;
}
