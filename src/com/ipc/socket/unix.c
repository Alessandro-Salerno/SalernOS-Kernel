/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <asm/ioctls.h>
#include <fcntl.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/ipc/socket/unix.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/ringbuffer.h>
#include <lib/str.h>
#include <lib/sync.h>
#include <lib/usercopy.h>
#include <poll.h>
#include <stdatomic.h>

struct unix_socket {
    // Shared by server and client sockets
    com_socket_t        socket;
    kringbuffer_t       rb;
    com_socket_addr_t   binding_path;
    struct unix_socket *peer;
    size_t              num_ref; // owner, peer pointer, and senders to this

    // Used only by server side. srv_vnode is bound to the socket
    com_vnode_t *srv_vnode;
    TAILQ_HEAD(, unix_socket) srv_acceptq;
    TAILQ_HEAD(, unix_socket) srv_connections;
    com_waitlist_t srv_acceptwait;
    size_t         srv_maxconns;

    // Used only by client side
    TAILQ_ENTRY(unix_socket) cln_connent;
};

// Foward declaration, defined below
static com_socket_ops_t UnixSocketOps;

static int unix_socket_rb_check_hangup(size_t        *new_nbytes,
                                       size_t         curr_nbytes,
                                       bool          *force_return,
                                       kringbuffer_t *rb,
                                       int            op,
                                       void          *arg) {
    (void)curr_nbytes;
    struct unix_socket *unix_socket = arg;
    KASSERT(NULL != unix_socket);
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    if (KRINGBUFFER_OP_WRITE == op) {
        if (NULL == unix_socket->peer) {
            com_ipc_signal_send_to_thread(curr_thread,
                                          SIGPIPE,
                                          curr_thread->proc);
            return EPIPE;
        }
    } else if (KRINGBUFFER_OP_READ == op) {
        if (NULL == unix_socket->peer && 0 == KRINGBUFFER_AVAIL_READ(rb)) {
            *new_nbytes   = 0;
            *force_return = true;
        }
    }

    return 0;
}

static struct unix_socket *unix_socket_alloc(void) {
    struct unix_socket *sock = com_mm_slab_alloc(sizeof(struct unix_socket));
    sock->socket.ops         = &UnixSocketOps;
    sock->socket.type        = E_COM_SOCKET_TYPE_UNIX;
    size_t rb_pages          = CONFIG_UNIX_SOCK_RB_SIZE / ARCH_PAGE_SIZE + 1;
    void  *rb_buff = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_many(rb_pages));
    KRINGBUFFER_INIT_CUSTOM(&sock->rb, rb_buff, CONFIG_UNIX_SOCK_RB_SIZE);
    KSYNC_INIT_MUTEX(&sock->rb.condvar);
    sock->rb.fallback_hu_arg = sock;
    sock->rb.check_hangup    = unix_socket_rb_check_hangup;
    TAILQ_INIT(&sock->srv_acceptq);
    COM_SYS_THREAD_WAITLIST_INIT(&sock->srv_acceptwait);
    LIST_INIT(&sock->socket.pollhead.polled_list);
    KSYNC_INIT_MUTEX(&sock->socket.lock);
    sock->num_ref = 1;
    return sock;
}

static void unix_socket_release(struct unix_socket *sock) {
    if (0 != __atomic_add_fetch(&sock->num_ref, -1, __ATOMIC_ACQ_REL)) {
        return;
    }

    void  *phys_buff   = (void *)ARCH_HHDM_TO_PHYS(sock->rb.buffer);
    size_t rbuff_pages = sock->rb.buffer_size / ARCH_PAGE_SIZE + 1;
    com_mm_pmm_free_many(phys_buff, rbuff_pages);
    com_mm_slab_free(sock, sizeof(struct unix_socket));
}

static void unix_socket_poll_callback(void *arg) {
    com_socket_t *sock = arg;

    kspinlock_acquire(&sock->pollhead.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &sock->pollhead.polled_list, polled_list, _) {
        com_fs_poll_notify(polled->poller);
    }
    kspinlock_release(&sock->pollhead.lock);
}

// LOCKING: the only place a socket lock is held before a binding lock. connect
// takes them in the other order, but only once the binding exists
static int unix_socket_bind(com_socket_t *socket, com_socket_addr_t *addr) {
    struct unix_socket *unix_socket = (void *)socket;
    com_proc_t         *curr_proc   = ARCH_CPU_GET_THREAD()->proc;
    com_vnode_t        *cwd         = com_sys_proc_get_cwd(curr_proc);
    com_vnode_t        *socket_vn   = NULL;
    int                 ret         = 0;
    ksync_acquire(&unix_socket->socket.lock);

    if (E_COM_SOCKET_STATE_UNBOUND != unix_socket->socket.state) {
        ret = EINVAL;
        goto end;
    }

    ret = com_fs_vfs_create_path(&socket_vn,
                                 addr->local.path,
                                 addr->local.pathlen,
                                 curr_proc->root,
                                 cwd,
                                 &COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_SOCKET),
                                 COM_FS_VFS_CREATE_EXCL);
    if (EEXIST == ret) {
        ret = EADDRINUSE;
    }
    if (0 != ret) {
        goto end;
    }

    ret = com_fs_vfs_bind(socket_vn, unix_socket);
    if (0 != ret) {
        goto end;
    }

    unix_socket->srv_vnode                  = socket_vn;
    unix_socket->binding_path.local.pathlen = addr->local.pathlen;
    kmemcpy(unix_socket->binding_path.local.path,
            addr->local.path,
            addr->local.pathlen);
    unix_socket->socket.state = E_COM_SOCKET_STATE_BOUND;

end:
    ksync_release(&unix_socket->socket.lock);

    // The binding owns its own reference
    COM_FS_VFS_VNODE_RELEASE(socket_vn);
    COM_FS_VFS_VNODE_RELEASE(cwd);
    return ret;
}

static int unix_socket_send(com_socket_t *socket, com_socket_desc_t *desc) {
    struct unix_socket *unix_socket = (void *)socket;
    ksync_acquire(&unix_socket->socket.lock);

    if (E_COM_SOCKET_STATE_CONNECTED != unix_socket->socket.state ||
        NULL == unix_socket->peer) {
        ksync_release(&unix_socket->socket.lock);
        return ENOTCONN;
    }

    // The peer may close while this is in its ring
    struct unix_socket *peer = unix_socket->peer;
    __atomic_add_fetch(&peer->num_ref, 1, __ATOMIC_RELAXED);
    ksync_release(&unix_socket->socket.lock);
    int ret = kioviter_read_to_ringbuffer(&desc->count_done,
                                          &peer->rb,
                                          KRINGBUFFER_NOATOMIC,
                                          !(O_NONBLOCK & desc->flags),
                                          unix_socket_poll_callback,
                                          peer,
                                          NULL,
                                          desc->ioviter,
                                          desc->count);
    unix_socket_release(peer);
    return ret;
}

static int unix_socket_recv(com_socket_t *socket, com_socket_desc_t *desc) {
    struct unix_socket *unix_socket = (void *)socket;
    ksync_acquire(&unix_socket->socket.lock);

    if (E_COM_SOCKET_STATE_CONNECTED != unix_socket->socket.state) {
        ksync_release(&unix_socket->socket.lock);
        return ENOTCONN;
    }

    ksync_release(&unix_socket->socket.lock);
    return kioviter_write_from_ringbuffer(desc->ioviter,
                                          desc->count,
                                          &desc->count_done,
                                          &unix_socket->rb,
                                          KRINGBUFFER_NOATOMIC,
                                          !(O_NONBLOCK & desc->flags),
                                          unix_socket_poll_callback,
                                          unix_socket,
                                          NULL);
}

static int unix_socket_connect(com_socket_t *socket, com_socket_addr_t *addr) {
    struct unix_socket *client    = (void *)socket;
    com_proc_t         *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    com_vnode_t        *cwd       = com_sys_proc_get_cwd(curr_proc);
    com_vnode_t        *server_vn = NULL;
    int                 ret       = 0;
    ksync_acquire(&client->socket.lock);

    if (E_COM_SOCKET_STATE_CONNECTED == client->socket.state) {
        ret = EISCONN;
        goto end;
    }

    if (E_COM_SOCKET_STATE_UNBOUND != client->socket.state) {
        ret = EOPNOTSUPP;
        goto end;
    }

    ret = com_fs_vfs_lookup(&server_vn,
                            addr->local.path,
                            addr->local.pathlen,
                            curr_proc->root,
                            cwd,
                            COM_FS_VFS_LOOKUP_FOLLOW);
    if (0 != ret) {
        goto end;
    }

    if (E_COM_VNODE_TYPE_SOCKET != server_vn->type) {
        ret = ENOTSOCK;
        goto end;
    }

    // destroy unbinds first, so the server lives until the binding is released
    struct unix_socket *server = com_fs_vfs_binding_acquire(server_vn);
    if (NULL == server) {
        com_fs_vfs_binding_release(server_vn);
        ret = ECONNREFUSED;
        goto end;
    }

    ksync_acquire(&server->socket.lock);
    if (E_COM_SOCKET_STATE_LISTENING != server->socket.state) {
        ret = ECONNREFUSED;
        ksync_release(&server->socket.lock);
        com_fs_vfs_binding_release(server_vn);
        goto end;
    }

    // We create a "peer" socket due to the flow send() and recv() will have:
    // when the client does send() it sends to the server
    // When the server does send() it sends to the client
    // However, send() and recv() don't know if the socket is a server or a
    // client
    struct unix_socket *server_peer = unix_socket_alloc();
    __atomic_add_fetch(&server_peer->num_ref, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&client->num_ref, 1, __ATOMIC_RELAXED);
    server_peer->peer         = client;
    client->peer              = server_peer;
    server_peer->socket.state = E_COM_SOCKET_STATE_CONNECTED;
    server_peer->binding_path = server->binding_path;
    client->socket.state      = E_COM_SOCKET_STATE_CONNECTED;
    TAILQ_INSERT_TAIL(&server->srv_acceptq, server_peer, cln_connent);

    // The server may do accept() and be on the waitlist or may be polling and
    // thus be on the poll queue, but cannot do both at the same time. So we
    // call both, since only one will have effect
    ksync_notify(&server->socket.lock, &server->srv_acceptwait);
    ksync_release(&server->socket.lock);
    unix_socket_poll_callback(server);
    com_fs_vfs_binding_release(server_vn);

end:
    ksync_release(&client->socket.lock);
    COM_FS_VFS_VNODE_RELEASE(server_vn);
    COM_FS_VFS_VNODE_RELEASE(cwd);
    return ret;
}

static int unix_socket_listen(com_socket_t *socket, size_t max_connections) {
    struct unix_socket *unix_socket = (void *)socket;
    int                 ret         = 0;
    ksync_acquire(&unix_socket->socket.lock);

    // Calling listen twice is a no-op
    if (E_COM_SOCKET_STATE_LISTENING == unix_socket->socket.state) {
        goto end;
    }

    if (E_COM_SOCKET_STATE_BOUND != unix_socket->socket.state) {
        ret = EOPNOTSUPP;
        goto end;
    }

    unix_socket->socket.state = E_COM_SOCKET_STATE_LISTENING;
    unix_socket->srv_maxconns = max_connections;

end:
    ksync_release(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_accept(com_socket_t     **out_peer,
                              com_socket_addr_t *out_addr,
                              com_socket_t      *socket,
                              uintmax_t          flags) {
    (void)out_addr;
    struct unix_socket *server     = (void *)socket;
    int                 ret        = 0;
    struct unix_socket *new_client = NULL;
    ksync_acquire(&server->socket.lock);

    if (E_COM_SOCKET_STATE_LISTENING != server->socket.state) {
        ret = EINVAL;
        goto end;
    }

    new_client = TAILQ_FIRST(&server->srv_acceptq);
    if (NULL != new_client) {
        goto client_found;
    }

    if (O_NONBLOCK & flags) {
        ret = EAGAIN;
        goto end;
    }

    // Here we know no client did connect() and we have to block
    while (NULL == new_client) {
        ksync_wait(&server->socket.lock, &server->srv_acceptwait);
        if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
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
    if (NULL != out_peer) {
        *out_peer = (void *)new_client;
    }

    ksync_release(&server->socket.lock);
    return ret;
}

static int unix_socket_getname(com_socket_addr_t *out, com_socket_t *socket) {
    struct unix_socket *unix_socket = (void *)socket;
    int                 ret         = 0;
    ksync_acquire(&unix_socket->socket.lock);

    if (NULL == unix_socket->srv_vnode) {
        out->local.path[0] = 0;
        out->local.pathlen = 0;
        goto end;
    }

    out->local.pathlen = unix_socket->binding_path.local.pathlen;
    kmemcpy(out->local.path,
            unix_socket->binding_path.local.path,
            unix_socket->binding_path.local.pathlen);

end:
    ksync_release(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_getpeername(com_socket_addr_t *out,
                                   com_socket_t      *socket) {
    struct unix_socket *unix_socket = (void *)socket;
    int                 ret         = 0;
    ksync_acquire(&unix_socket->socket.lock);

    if (E_COM_SOCKET_STATE_CONNECTED != unix_socket->socket.state ||
        NULL == unix_socket->peer) {
        out->local.path[0] = 0;
        out->local.pathlen = 0;
        goto end;
    }

    out->local.pathlen = unix_socket->peer->binding_path.local.pathlen;
    kmemcpy(out->local.path,
            unix_socket->peer->binding_path.local.path,
            unix_socket->peer->binding_path.local.pathlen);

end:
    ksync_release(&unix_socket->socket.lock);
    return ret;
}

static int
unix_socket_poll(short *revents, com_socket_t *socket, short events) {
    (void)events;

    struct unix_socket *unix_socket = (void *)socket;
    short               out         = kringbuffer_poll(&unix_socket->rb);
    ksync_acquire(&unix_socket->socket.lock);

    if (!TAILQ_EMPTY(&unix_socket->srv_acceptq)) {
        out |= POLLIN;
    }
    if (E_COM_SOCKET_STATE_CONNECTED == unix_socket->socket.state &&
        NULL == unix_socket->peer) {
        out |= POLLIN | POLLHUP;
    }

    ksync_release(&unix_socket->socket.lock);
    *revents = out;
    return 0;
}

static int unix_socket_destroy(com_socket_t *socket) {
    struct unix_socket *unix_socket = (void *)socket;

    // After this, no connect can reach the socket (see connect)
    if (NULL != unix_socket->srv_vnode) {
        com_fs_vfs_unbind(unix_socket->srv_vnode, unix_socket);
        unix_socket->srv_vnode = NULL;
    }

    // Normally, a socket is associated to a file descriptor, and dropping that
    // file descriptor drops the socket. However, since these are yet to
    // establish one, we drop them instead. Recursion is bounded because the
    // sockets waiting to be accepted by a server should have no accept queue of
    // their own, so this pass does not repeat recursively
    struct unix_socket *queued, *_;
    TAILQ_FOREACH_SAFE(queued, &unix_socket->srv_acceptq, cln_connent, _) {
        unix_socket_destroy(&queued->socket);
    }

    ksync_acquire(&unix_socket->socket.lock);
    struct unix_socket *peer = unix_socket->peer;
    unix_socket->peer        = NULL;
    ksync_release(&unix_socket->socket.lock);

    kringbuffer_wake_all(&unix_socket->rb);

    unix_socket_poll_callback(unix_socket);

    // Both ends may close at once: clearing a peer pointer takes its reference
    if (NULL != peer) {
        ksync_acquire(&peer->socket.lock);
        bool cleared = unix_socket == peer->peer;
        if (cleared) {
            peer->peer = NULL;
        }
        ksync_release(&peer->socket.lock);

        if (cleared) {
            unix_socket_release(unix_socket);
        }

        kringbuffer_wake_all(&peer->rb);

        unix_socket_poll_callback(&peer->socket);
        unix_socket_release(peer);
    }

    unix_socket_release(unix_socket);
    return 0;
}

static int unix_socket_ioctl(com_socket_t *socket, uintmax_t op, void *buf) {
    struct unix_socket *unix_socket = (void *)socket;

    if (FIONREAD == op) {
        int avail = kringbuffer_avail_read(&unix_socket->rb);
        return kmemcpy_to_user(buf, &avail, sizeof(avail));
    }

    return ENODEV;
}

static com_socket_ops_t UnixSocketOps = {
    .bind        = unix_socket_bind,
    .send        = unix_socket_send,
    .recv        = unix_socket_recv,
    .connect     = unix_socket_connect,
    .listen      = unix_socket_listen,
    .accept      = unix_socket_accept,
    .getname     = unix_socket_getname,
    .getpeername = unix_socket_getpeername,
    .ioctl       = unix_socket_ioctl,
    .poll        = unix_socket_poll,
    .destroy     = unix_socket_destroy,
};

// NOTE: Not pasting it directly because I wdislike casts and don't want to cast
// to unix_socket when I call this above
com_socket_t *com_ipc_socket_unix_new(void) {
    return (void *)unix_socket_alloc();
}
