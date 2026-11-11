#include <asm/ioctls.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/ipc/socket/unix.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/sync.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <poll.h>
#include <stdatomic.h>

struct UnixSocket {
    // Shared by server and client sockets
    Socket socket;
    Ringbuffer rb;
    SocketAddr binding_path;
    struct UnixSocket *peer;
    usize num_ref; // owner, peer pointer, and senders to this

    // Used only by server side. srv_vnode is bound to the socket
    Vnode *srv_vnode;
    TAILQ_HEAD(, UnixSocket) srv_acceptq;
    TAILQ_HEAD(, UnixSocket) srv_connections;
    Waitlist srv_acceptwait;
    usize srv_maxconns;

    // Used only by client side
    TAILQ_ENTRY(UnixSocket) cln_connent;
};

// Foward declaration, defined below
static SocketOps g_unix_socket_ops;

static int unix_socket_rb_check_hangup(usize curr_nbytes, Ringbuffer *rb,
                                       int op, void *arg, usize *out_new_nbytes,
                                       bool *out_force_return) {
    (void)curr_nbytes;
    struct UnixSocket *unix_socket = arg;
    assert(unix_socket != NULL);
    Thread *curr_thread = cpu_get_thread();

    if (op == RINGBUFFER_OP_WRITE) {
        if (unix_socket->peer == NULL) {
            signal_send_to_thread(curr_thread, SIGPIPE, curr_thread->proc);
            return EPIPE;
        }
    } else if (op == RINGBUFFER_OP_READ) {
        if (unix_socket->peer == NULL &&
            ringbuffer_avail_read_nolock(rb) == 0) {
            *out_new_nbytes = 0;
            *out_force_return = true;
        }
    }

    return 0;
}

static struct UnixSocket *unix_socket_alloc(void) {
    struct UnixSocket *sock = slab_alloc(sizeof(struct UnixSocket));
    sock->socket.ops = &g_unix_socket_ops;
    sock->socket.type = SOCKET_TYPE_UNIX;
    usize rb_pages = CONFIG_UNIX_SOCK_RB_SIZE / PAGE_SIZE + 1;
    void *rb_buff = (void *)PHYS_TO_HHDM(pmm_allocn(rb_pages));
    ringbuffer_init_custom(&sock->rb, rb_buff, CONFIG_UNIX_SOCK_RB_SIZE);
    sync_init_mutex(&sock->rb.condvar);
    sock->rb.fallback_hu_arg = sock;
    sock->rb.check_hangup = unix_socket_rb_check_hangup;
    TAILQ_INIT(&sock->srv_acceptq);
    waitlist_init(&sock->srv_acceptwait);
    LIST_INIT(&sock->socket.pollhead.polled_list);
    mutex_init(&sock->socket.lock);
    sock->num_ref = 1;
    return sock;
}

static void unix_socket_rel(struct UnixSocket *sock) {
    if (__atomic_add_fetch(&sock->num_ref, -1, __ATOMIC_ACQ_REL) != 0) return;

    void *phys_buff = (void *)HHDM_TO_PHYS(sock->rb.buffer);
    usize rbuff_pages = sock->rb.buffer_size / PAGE_SIZE + 1;
    pmm_freen(phys_buff, rbuff_pages);
    slab_free(sock, sizeof(struct UnixSocket));
}

static void unix_socket_poll_callback(void *arg) {
    Socket *sock = arg;

    spinlock_acq(&sock->pollhead.lock);
    Polled *polled, *_;
    LIST_FOREACH_SAFE(polled, &sock->pollhead.polled_list, polled_list, _) {
        poll_notify(polled->poller);
    }
    spinlock_rel(&sock->pollhead.lock);
}

// LOCKING: the only place a socket lock is held before a binding lock. connect
// takes them in the other order, but only once the binding exists
static int unix_socket_bind(Socket *socket, SocketAddr *addr) {
    struct UnixSocket *unix_socket = (void *)socket;
    Proc *curr_proc = cpu_get_thread()->proc;
    Vnode *cwd = proc_get_cwd(curr_proc);
    Vnode *socket_vn = NULL;
    int ret = 0;
    mutex_acq(&unix_socket->socket.lock);

    if (unix_socket->socket.state != SOCKET_STATE_UNBOUND) {
        ret = EINVAL;
        goto end;
    }

    ret = vfs_create_path(addr->local.path, addr->local.pathlen,
                          curr_proc->root, cwd, &VFS_ATTR(VNODE_TYPE_SOCKET),
                          VFS_CREATE_EXCL, &socket_vn);
    if (ret == EEXIST) ret = EADDRINUSE;
    if (ret != 0) goto end;

    ret = vfs_bind(socket_vn, unix_socket);
    if (ret != 0) goto end;

    unix_socket->srv_vnode = socket_vn;
    unix_socket->binding_path.local.pathlen = addr->local.pathlen;
    memcpy(unix_socket->binding_path.local.path, addr->local.path,
           addr->local.pathlen);
    unix_socket->socket.state = SOCKET_STATE_BOUND;

end:
    mutex_rel(&unix_socket->socket.lock);

    // The binding owns its own reference
    vfs_vnode_rel(socket_vn);
    vfs_vnode_rel(cwd);
    return ret;
}

static int unix_socket_send(Socket *socket, SocketDesc *desc) {
    struct UnixSocket *unix_socket = (void *)socket;
    mutex_acq(&unix_socket->socket.lock);

    if (unix_socket->socket.state != SOCKET_STATE_CONNECTED ||
        unix_socket->peer == NULL) {
        mutex_rel(&unix_socket->socket.lock);
        return ENOTCONN;
    }

    // The peer may close while this is in its ring
    struct UnixSocket *peer = unix_socket->peer;
    __atomic_add_fetch(&peer->num_ref, 1, __ATOMIC_RELAXED);
    mutex_rel(&unix_socket->socket.lock);
    int ret = ioviter_read_to_ringbuffer(
        &peer->rb, RINGBUFFER_NOATOMIC, !(desc->flags & O_NONBLOCK),
        unix_socket_poll_callback, peer, NULL, desc->ioviter, desc->count,
        &desc->count_done);
    unix_socket_rel(peer);
    return ret;
}

static int unix_socket_recv(Socket *socket, SocketDesc *desc) {
    struct UnixSocket *unix_socket = (void *)socket;
    mutex_acq(&unix_socket->socket.lock);

    if (unix_socket->socket.state != SOCKET_STATE_CONNECTED) {
        mutex_rel(&unix_socket->socket.lock);
        return ENOTCONN;
    }

    mutex_rel(&unix_socket->socket.lock);
    return ioviter_write_from_ringbuffer(
        desc->ioviter, desc->count, &unix_socket->rb, RINGBUFFER_NOATOMIC,
        !(desc->flags & O_NONBLOCK), unix_socket_poll_callback, unix_socket,
        NULL, &desc->count_done);
}

static int unix_socket_connect(Socket *socket, SocketAddr *addr) {
    struct UnixSocket *client = (void *)socket;
    Proc *curr_proc = cpu_get_thread()->proc;
    Vnode *cwd = proc_get_cwd(curr_proc);
    Vnode *server_vn = NULL;
    int ret = 0;
    mutex_acq(&client->socket.lock);

    if (client->socket.state == SOCKET_STATE_CONNECTED) {
        ret = EISCONN;
        goto end;
    }

    if (client->socket.state != SOCKET_STATE_UNBOUND) {
        ret = EOPNOTSUPP;
        goto end;
    }

    ret = vfs_lookup(addr->local.path, addr->local.pathlen, curr_proc->root,
                     cwd, VFS_LOOKUP_FOLLOW, &server_vn);
    if (ret != 0) goto end;

    if (server_vn->type != VNODE_TYPE_SOCKET) {
        ret = ENOTSOCK;
        goto end;
    }

    // destroy unbinds first, so the server lives until the binding is released
    struct UnixSocket *server = vfs_binding_acq(server_vn);
    if (server == NULL) {
        vfs_binding_rel(server_vn);
        ret = ECONNREFUSED;
        goto end;
    }

    mutex_acq(&server->socket.lock);
    if (server->socket.state != SOCKET_STATE_LISTENING) {
        ret = ECONNREFUSED;
        mutex_rel(&server->socket.lock);
        vfs_binding_rel(server_vn);
        goto end;
    }

    // We create a "peer" socket due to the flow send() and recv() will have:
    // when the client does send() it sends to the server
    // When the server does send() it sends to the client
    // However, send() and recv() don't know if the socket is a server or a
    // client
    struct UnixSocket *server_peer = unix_socket_alloc();
    __atomic_add_fetch(&server_peer->num_ref, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&client->num_ref, 1, __ATOMIC_RELAXED);
    server_peer->peer = client;
    client->peer = server_peer;
    server_peer->socket.state = SOCKET_STATE_CONNECTED;
    server_peer->binding_path = server->binding_path;
    client->socket.state = SOCKET_STATE_CONNECTED;
    TAILQ_INSERT_TAIL(&server->srv_acceptq, server_peer, cln_connent);

    // The server may do accept() and be on the waitlist or may be polling and
    // thus be on the poll queue, but cannot do both at the same time. So we
    // call both, since only one will have effect
    sched_notify(&server->srv_acceptwait);
    mutex_rel(&server->socket.lock);
    unix_socket_poll_callback(server);
    vfs_binding_rel(server_vn);

end:
    mutex_rel(&client->socket.lock);
    vfs_vnode_rel(server_vn);
    vfs_vnode_rel(cwd);
    return ret;
}

static int unix_socket_listen(Socket *socket, usize max_connections) {
    struct UnixSocket *unix_socket = (void *)socket;
    int ret = 0;
    mutex_acq(&unix_socket->socket.lock);

    // Calling listen twice is a no-op
    if (unix_socket->socket.state == SOCKET_STATE_LISTENING) goto end;

    if (unix_socket->socket.state != SOCKET_STATE_BOUND) {
        ret = EOPNOTSUPP;
        goto end;
    }

    unix_socket->socket.state = SOCKET_STATE_LISTENING;
    unix_socket->srv_maxconns = max_connections;

end:
    mutex_rel(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_accept(Socket *socket, u64 flags, Socket **out_peer,
                              SocketAddr *out_addr) {
    (void)out_addr;
    struct UnixSocket *server = (void *)socket;
    int ret = 0;
    struct UnixSocket *new_client = NULL;
    mutex_acq(&server->socket.lock);

    if (server->socket.state != SOCKET_STATE_LISTENING) {
        ret = EINVAL;
        goto end;
    }

    new_client = TAILQ_FIRST(&server->srv_acceptq);
    if (new_client != NULL) goto client_found;

    if (flags & O_NONBLOCK) {
        ret = EAGAIN;
        goto end;
    }

    // Here we know no client did connect() and we have to block
    while (new_client == NULL) {
        mutex_wait(&server->socket.lock, &server->srv_acceptwait, 0, true);
        if (signal_check() != SIGNAL_NONE) {
            ret = EINTR;
            goto end;
        }
        new_client = TAILQ_FIRST(&server->srv_acceptq);
    }

client_found:
    // We can now remove the incoming connection from the queue, subsequent
    // calls to accept() will return the same socket
    TAILQ_REMOVE_HEAD(&server->srv_acceptq, cln_connent);

end:
    if (out_peer != NULL) *out_peer = (void *)new_client;

    mutex_rel(&server->socket.lock);
    return ret;
}

static int unix_socket_getname(Socket *socket, SocketAddr *out_addr) {
    struct UnixSocket *unix_socket = (void *)socket;
    int ret = 0;
    mutex_acq(&unix_socket->socket.lock);

    if (unix_socket->srv_vnode == NULL) {
        out_addr->local.path[0] = 0;
        out_addr->local.pathlen = 0;
        goto end;
    }

    out_addr->local.pathlen = unix_socket->binding_path.local.pathlen;
    memcpy(out_addr->local.path, unix_socket->binding_path.local.path,
           unix_socket->binding_path.local.pathlen);

end:
    mutex_rel(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_getpeername(Socket *socket, SocketAddr *out_addr) {
    struct UnixSocket *unix_socket = (void *)socket;
    int ret = 0;
    mutex_acq(&unix_socket->socket.lock);

    if (unix_socket->socket.state != SOCKET_STATE_CONNECTED ||
        unix_socket->peer == NULL) {
        out_addr->local.path[0] = 0;
        out_addr->local.pathlen = 0;
        goto end;
    }

    out_addr->local.pathlen = unix_socket->peer->binding_path.local.pathlen;
    memcpy(out_addr->local.path, unix_socket->peer->binding_path.local.path,
           unix_socket->peer->binding_path.local.pathlen);

end:
    mutex_rel(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_poll(Socket *socket, short events, short *out_revents) {
    (void)events;

    struct UnixSocket *unix_socket = (void *)socket;
    short out = ringbuffer_poll(&unix_socket->rb);
    mutex_acq(&unix_socket->socket.lock);

    if (!TAILQ_EMPTY(&unix_socket->srv_acceptq)) out |= POLLIN;
    if (unix_socket->socket.state == SOCKET_STATE_CONNECTED &&
        unix_socket->peer == NULL) {
        out |= POLLIN | POLLHUP;
    }

    mutex_rel(&unix_socket->socket.lock);
    *out_revents = out;
    return 0;
}

static int unix_socket_destroy(Socket *socket) {
    struct UnixSocket *unix_socket = (void *)socket;

    // After this, no connect can reach the socket (see connect)
    if (unix_socket->srv_vnode != NULL) {
        vfs_unbind(unix_socket->srv_vnode, unix_socket);
        unix_socket->srv_vnode = NULL;
    }

    // Normally, a socket is associated to a file descriptor, and dropping that
    // file descriptor drops the socket. However, since these are yet to
    // establish one, we drop them instead. Recursion is bounded because the
    // sockets waiting to be accepted by a server should have no accept queue of
    // their own, so this pass does not repeat recursively
    struct UnixSocket *queued, *_;
    TAILQ_FOREACH_SAFE(queued, &unix_socket->srv_acceptq, cln_connent, _) {
        unix_socket_destroy(&queued->socket);
    }

    mutex_acq(&unix_socket->socket.lock);
    struct UnixSocket *peer = unix_socket->peer;
    unix_socket->peer = NULL;
    mutex_rel(&unix_socket->socket.lock);

    ringbuffer_wake_all(&unix_socket->rb);

    unix_socket_poll_callback(unix_socket);

    // Both ends may close at once: clearing a peer pointer takes its reference
    if (peer != NULL) {
        mutex_acq(&peer->socket.lock);
        bool cleared = unix_socket == peer->peer;
        if (cleared) peer->peer = NULL;
        mutex_rel(&peer->socket.lock);

        if (cleared) unix_socket_rel(unix_socket);

        ringbuffer_wake_all(&peer->rb);

        unix_socket_poll_callback(&peer->socket);
        unix_socket_rel(peer);
    }

    unix_socket_rel(unix_socket);
    return 0;
}

static int unix_socket_ioctl(Socket *socket, u64 op, void *buf) {
    struct UnixSocket *unix_socket = (void *)socket;

    if (op == FIONREAD) {
        int avail = ringbuffer_avail_read(&unix_socket->rb);
        return memcpy_to_user(buf, &avail, sizeof(avail));
    }

    return ENODEV;
}

static SocketOps g_unix_socket_ops = {
    .bind = unix_socket_bind,
    .send = unix_socket_send,
    .recv = unix_socket_recv,
    .connect = unix_socket_connect,
    .listen = unix_socket_listen,
    .accept = unix_socket_accept,
    .getname = unix_socket_getname,
    .getpeername = unix_socket_getpeername,
    .ioctl = unix_socket_ioctl,
    .poll = unix_socket_poll,
    .destroy = unix_socket_destroy,
};

// NOTE: Not pasting it directly because I wdislike casts and don't want to cast
// to unix_socket when I call this above
Socket *socket_unix_new(void) { return (void *)unix_socket_alloc(); }
