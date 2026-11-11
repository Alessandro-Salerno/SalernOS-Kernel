#include <errno.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/util.h>

static VnodeOps g_sockfs_node_ops = {.readv = sockfs_readv,
                                     .writev = sockfs_writev,
                                     .ioctl = sockfs_ioctl,
                                     .poll_head = sockfs_poll_head,
                                     .poll = sockfs_poll,
                                     .inactive = sockfs_inactive,
                                     .seekable = sockfs_seekable};

int sockfs_readv(IOVIter *ioviter, Vnode *node, u64 off, u64 flags,
                 usize *out_bytes_read) {
    (void)off;
    SocketVnode *sockfs_vn = (void *)node;
    SocketDesc sock_desc = {.addr = NULL,
                            .ioviter = ioviter,
                            .count = ioviter->total_size,
                            .flags = flags,
                            .count_done = 0};

    int ret = SOCKET_RECV(sockfs_vn->socket, &sock_desc);
    *out_bytes_read = sock_desc.count_done;
    return ret;
}

int sockfs_writev(Vnode *node, IOVIter *ioviter, u64 off, u64 flags,
                  usize *out_bytes_written) {
    (void)off;
    SocketVnode *sockfs_vn = (void *)node;
    SocketDesc sock_desc = {.addr = NULL,
                            .ioviter = ioviter,
                            .count = ioviter->total_size,
                            .flags = flags,
                            .count_done = 0};

    int ret = SOCKET_SEND(sockfs_vn->socket, &sock_desc);
    *out_bytes_written = sock_desc.count_done;
    return ret;
}

int sockfs_ioctl(Vnode *node, u64 op, void *buf) {
    SocketVnode *sockfs_vn = (void *)node;
    Socket *socket = sockfs_vn->socket;
    return SOCKET_IOCTL(socket, op, buf);
}

int sockfs_poll_head(Vnode *node, struct PollHead **out_poll_head) {
    SocketVnode *sockfs_vn = (void *)node;
    return SOCKET_POLLHEAD(sockfs_vn->socket, out_poll_head);
}

int sockfs_poll(Vnode *node, short events, short *out_revents) {
    SocketVnode *sockfs_vn = (void *)node;
    return SOCKET_POLL(sockfs_vn->socket, events, out_revents);
}

// Socket vnodes are only referenced by their files and operations in progress
void sockfs_inactive(Vnode *node) {
    SocketVnode *sockfs_vn = (void *)node;
    verify(SOCKET_DESTROY(sockfs_vn->socket) == 0);
    slab_free(sockfs_vn, sizeof(SocketVnode));
}

int sockfs_seekable(Vnode *node) {
    (void)node;
    return ESPIPE;
}

// OTHER FUNCTIONS

Vnode *sockfs_new(void) {
    SocketVnode *sockfs_vn = slab_alloc(sizeof(SocketVnode));
    vfs_vnode_init(&sockfs_vn->vnode, NULL, VNODE_TYPE_SOCKET,
                   &g_sockfs_node_ops, NULL);
    return &sockfs_vn->vnode;
}
