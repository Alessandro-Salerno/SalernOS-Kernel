/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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
#include <poll.h>
#include <stdatomic.h>

struct unix_socket {
    // Shared by server and client sockets
    com_socket_t        socket;
    kringbuffer_t       rb;
    com_socket_addr_t   binding_path;
    struct unix_socket *peer;

    // Used only by server side
    struct unix_socket_binding *srv_binding;
    TAILQ_HEAD(, unix_socket) srv_acceptq;
    TAILQ_HEAD(, unix_socket) srv_connections;
    struct com_thread_tailq srv_acceptwait;
    size_t                  srv_maxconns;

    // Used only by client side
    TAILQ_ENTRY(unix_socket) cln_connent;
};

struct unix_socket_binding {
    struct unix_socket *server;
    com_vnode_t        *vnode;
    kspinlock_t         lock;
};

// Foward declaration, defined below
static com_socket_ops_t UnixSocketOps;

static struct unix_socket *unix_socket_alloc(void) {
    struct unix_socket *sock = com_mm_slab_alloc(sizeof(struct unix_socket));
    sock->socket.ops         = &UnixSocketOps;
    size_t rb_pages          = CONFIG_UNIX_SOCK_RB_SIZE / ARCH_PAGE_SIZE + 1;
    void  *rb_buff = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_many(rb_pages));
    KRINGBUFFER_INIT_CUSTOM(&sock->rb, rb_buff, CONFIG_UNIX_SOCK_RB_SIZE);
    sock->rb.fallback_hu_arg = sock;
    TAILQ_INIT(&sock->srv_acceptq);
    TAILQ_INIT(&sock->srv_acceptwait);
    LIST_INIT(&sock->socket.pollhead.polled_list);
    return sock;
}

static void unix_socket_poll_callback(void *arg) {
    com_socket_t *sock = arg;

    kspinlock_acquire(&sock->pollhead.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &sock->pollhead.polled_list, polled_list, _) {
        kspinlock_acquire(&polled->poller->lock);
        com_sys_sched_notify_all(&polled->poller->waiters);
        kspinlock_release(&polled->poller->lock);
    }
    kspinlock_release(&sock->pollhead.lock);
}

static int unix_socket_bind(com_socket_t *socket, com_socket_addr_t *addr) {
    struct unix_socket *unix_socket = (void *)socket;
    com_proc_t         *curr_proc   = ARCH_CPU_GET_THREAD()->proc;
    int                 ret         = 0;
    kspinlock_acquire(&unix_socket->socket.lock);

    struct unix_socket_binding *binding = com_mm_slab_alloc(
        sizeof(struct unix_socket_binding));
    com_socket_addr_t bind_path;
    bind_path.local.pathlen = addr->local.pathlen;
    kmemcpy(bind_path.local.path, addr->local.path, addr->local.pathlen);

    com_vnode_t *socket_vn = NULL;
    ret                    = com_fs_vfs_create_any(&socket_vn,
                                addr->local.path,
                                addr->local.pathlen,
                                curr_proc->root,
                                atomic_load(&curr_proc->cwd),
                                0,
                                com_fs_vfs_mksocket);
    if (0 != ret) {
        goto end;
    }

    binding->server           = unix_socket;
    binding->vnode            = socket_vn;
    binding->lock             = KSPINLOCK_NEW();
    socket_vn->binding        = binding;
    unix_socket->srv_binding  = binding;
    unix_socket->binding_path = bind_path;
    unix_socket->socket.state = E_COM_SOCKET_STATE_BOUND;

end:
    kspinlock_release(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_send(com_socket_t *socket, com_socket_desc_t *desc) {
    struct unix_socket *unix_socket = (void *)socket;
    kspinlock_acquire(&unix_socket->socket.lock);

    if (E_COM_SOCKET_STATE_CONNECTED != unix_socket->socket.state ||
        NULL == unix_socket->peer) {
        kspinlock_release(&unix_socket->socket.lock);
        return ENOTCONN;
    }

    kspinlock_release(&unix_socket->socket.lock);
    return kioviter_read_to_ringbuffer(&desc->count_done,
                                       &unix_socket->peer->rb,
                                       KRINGBUFFER_NOATOMIC,
                                       !(O_NONBLOCK & desc->flags),
                                       unix_socket_poll_callback,
                                       unix_socket->peer,
                                       NULL,
                                       desc->ioviter,
                                       desc->count);
}

static int unix_socket_recv(com_socket_t *socket, com_socket_desc_t *desc) {
    struct unix_socket *unix_socket = (void *)socket;
    kspinlock_acquire(&unix_socket->socket.lock);

    if (E_COM_SOCKET_STATE_CONNECTED != unix_socket->socket.state) {
        kspinlock_release(&unix_socket->socket.lock);
        return ENOTCONN;
    }

    kspinlock_release(&unix_socket->socket.lock);
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
    com_vnode_t        *server_vn = NULL;
    int                 ret       = 0;
    kspinlock_acquire(&client->socket.lock);

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
                            atomic_load(&curr_proc->cwd),
                            true);
    if (0 != ret) {
        goto end;
    }

    if (E_COM_VNODE_TYPE_SOCKET != server_vn->type) {
        ret = ENOTSOCK;
        goto end;
    }

    struct unix_socket_binding *binding = server_vn->binding;
    if (NULL == binding) {
        ret = ECONNREFUSED;
        goto end;
    }

    kspinlock_acquire(&binding->lock);
    struct unix_socket *server = binding->server;
    kspinlock_release(&binding->lock);

    if (NULL == server) {
        ret = ECONNREFUSED;
        goto end;
    }

    kspinlock_acquire(&server->socket.lock);
    if (E_COM_SOCKET_STATE_LISTENING != server->socket.state) {
        ret = ECONNREFUSED;
        kspinlock_release(&server->socket.lock);
        goto end;
    }

    // We create a "peer" socket due to the flow send() and recv() will have:
    // when the client does send() it sends to the server
    // When the server does send() it sends to the client
    // However, send() and recv() don't know if the socket is a server or a
    // client
    struct unix_socket *server_peer = unix_socket_alloc();
    server_peer->peer               = client;
    client->peer                    = server_peer;
    server_peer->socket.state       = E_COM_SOCKET_STATE_CONNECTED;
    client->socket.state            = E_COM_SOCKET_STATE_CONNECTED;
    TAILQ_INSERT_TAIL(&server->srv_acceptq, server_peer, cln_connent);

    // The server may do accept() and be on the waitlist or may be polling and
    // thus be on the poll queue, but cannot do both at the same time. So we
    // call both, since only one will have effect
    com_sys_sched_notify(&server->srv_acceptwait);
    kspinlock_release(&server->socket.lock);
    unix_socket_poll_callback(server);

end:
    kspinlock_release(&client->socket.lock);
    COM_FS_VFS_VNODE_RELEASE(server_vn);
    return ret;
}

static int unix_socket_listen(com_socket_t *socket, size_t max_connections) {
    struct unix_socket *unix_socket = (void *)socket;
    int                 ret         = 0;
    kspinlock_acquire(&unix_socket->socket.lock);

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
    kspinlock_release(&unix_socket->socket.lock);
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
    kspinlock_acquire(&server->socket.lock);

    if (E_COM_SOCKET_STATE_LISTENING != server->socket.state) {
        ret = EINVAL;
        goto end;
    }

    new_client = TAILQ_FIRST(&server->srv_acceptq);
    if (NULL != new_client) {
        goto end;
    }

    if (O_NONBLOCK & flags) {
        ret = EAGAIN;
        goto end;
    }

    // Here we know no client did connect() and we have to block
    while (NULL == new_client) {
        com_sys_sched_wait(&server->srv_acceptwait, &server->socket.lock);
        new_client = TAILQ_FIRST(&server->srv_acceptq);
    }

    // We can now remove the incoming connection from the queue, subsequent
    // calls to accept() will return the same socket
    TAILQ_REMOVE_HEAD(&server->srv_acceptq, cln_connent);

end:
    if (NULL != out_peer) {
        *out_peer = (void *)new_client;
    }

    kspinlock_release(&server->socket.lock);
    return ret;
}

static int unix_socket_getname(com_socket_addr_t *out, com_socket_t *socket) {
    struct unix_socket *unix_socket = (void *)socket;
    int                 ret         = 0;
    kspinlock_acquire(&unix_socket->socket.lock);

    if (NULL == unix_socket->srv_binding) {
        out->local.path[0] = 0;
        out->local.pathlen = 0;
        goto end;
    }

    out->local.pathlen = unix_socket->binding_path.local.pathlen;
    kmemcpy(out->local.path,
            unix_socket->binding_path.local.path,
            unix_socket->binding_path.local.pathlen);

end:
    kspinlock_release(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_getpeername(com_socket_addr_t *out,
                                   com_socket_t      *socket) {
    (void)socket;
    out->local.path[0] = 0;
    out->local.pathlen = 0;
    return 0;
}

static int
unix_socket_poll(short *revents, com_socket_t *socket, short events) {
    (void)events;

    struct unix_socket *unix_socket = (void *)socket;
    short               out         = 0;
    kspinlock_acquire(&unix_socket->socket.lock);

    if (!TAILQ_EMPTY(&unix_socket->srv_acceptq)) {
        out |= POLLIN;
    }
    if (KRINGBUFFER_AVAIL_READ(&unix_socket->rb) > 0) {
        out |= POLLIN;
    }
    if (KRINGBUFFER_AVAIL_WRITE(&unix_socket->rb) > 0) {
        out |= POLLOUT;
    }

    kspinlock_release(&unix_socket->socket.lock);
    *revents = out;
    return 0;
}

static int unix_socket_destroy(com_socket_t *socket) {
    struct unix_socket *unix_socket = (void *)socket;
    void  *phys_buff   = (void *)ARCH_HHDM_TO_PHYS(unix_socket->rb.buffer);
    size_t rbuff_pages = unix_socket->rb.buffer_size / ARCH_PAGE_SIZE + 1;
    com_mm_pmm_free_many(phys_buff, rbuff_pages);
    com_mm_slab_free(unix_socket, sizeof(struct unix_socket));
    return 0;
}

static int unix_socket_ioctl(com_socket_t *socket, uintmax_t op, void *buf) {
    struct unix_socket *unix_socket = (void *)socket;

    if (FIONREAD == op) {
        *(int *)buf = KRINGBUFFER_AVAIL_READ(&unix_socket->rb);
        return 0;
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
