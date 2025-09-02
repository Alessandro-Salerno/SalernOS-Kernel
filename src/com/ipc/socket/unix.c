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

#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/ipc/socket/unix.h>
#include <kernel/com/sys/thread.h>
#include <lib/mutex.h>
#include <lib/ringbuffer.h>
#include <lib/str.h>
#include <stdatomic.h>

struct unix_socket {
    // Shared by server and client sockets
    com_socket_t      socket;
    kringbuffer_t     rb;
    com_socket_addr_t binding_path;

    // Used only by server side
    struct unix_socket_binding *srv_binding;
    TAILQ_HEAD(, unix_socket) srv_acceptq;
    TAILQ_HEAD(, unix_socket_connection) srv_connections;
    size_t srv_maxconns;

    // Used only by client side
    TAILQ_ENTRY(unix_socket) cln_acceptent;
    struct unix_socket_connection *cln_connection;
};

struct unix_socket_connection {
    struct unix_socket *server;
    struct unix_socket *client;
    kmutex_t            mutex;
    TAILQ_ENTRY(unix_socket_connection) connections_entry;
};

struct unix_socket_binding {
    struct unix_socket *server;
    com_vnode_t        *vnode;
    kmutex_t            mutex;
};

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

    binding->server = unix_socket;
    binding->vnode  = socket_vn;
    KMUTEX_INIT(&binding->mutex);
    socket_vn->binding        = binding;
    unix_socket->srv_binding  = binding;
    unix_socket->binding_path = bind_path;
    unix_socket->socket.state = E_COM_SOCKET_STATE_BOUND;

end:
    kspinlock_release(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_send(com_socket_t *socket, com_socket_desc_t *desc) {
}

static int unix_socket_recv(com_socket_t *socket, com_socket_desc_t *desc) {
}

static int unix_socket_connect(com_socket_t *socket, com_socket_addr_t *addr) {
    struct unix_socket *unix_socket = (void *)socket;
    com_proc_t         *curr_proc   = ARCH_CPU_GET_THREAD()->proc;
    com_vnode_t        *socket_vn   = NULL;
    int                 ret         = 0;
    kspinlock_acquire(&unix_socket->socket.lock);

    if (E_COM_SOCKET_STATE_CONNECTED == unix_socket->socket.state) {
        ret = EISCONN;
        goto end;
    }

    if (E_COM_SOCKET_STATE_UNBOUND != unix_socket->socket.state) {
        ret = EOPNOTSUPP;
        goto end;
    }

    ret = com_fs_vfs_lookup(&socket_vn,
                            addr->local.path,
                            addr->local.pathlen,
                            curr_proc->root,
                            atomic_load(&curr_proc->cwd),
                            true);

    // TODO: continue here

end:
    kspinlock_release(&unix_socket->socket.lock);
    COM_FS_VFS_VNODE_RELEASE(socket_vn);
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
    TAILQ_INIT(&unix_socket->srv_connections);

end:
    kspinlock_release(&unix_socket->socket.lock);
    return ret;
}

static int unix_socket_accept(com_socket_t      *server,
                              com_socket_t      *client,
                              com_socket_addr_t *addr,
                              uintmax_t          flags) {
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
    out->local.path[0] = 0;
    out->local.pathlen = 0;
    return 0;
}

static int
unix_socket_poll(short *revents, com_socket_t *socket, short events) {
}

static int unix_socket_destroy(com_socket_t *socket) {
}

static com_socket_ops_t UnixSocketOps = {
    .bind        = unix_socket_bind,
    .send        = unix_socket_send,
    .recv        = unix_socket_recv,
    .connect     = unix_socket_connect,
    .listen      = unix_socket_listen,
    .accept      = unix_socket_accept,
    .getname     = unix_socket_getname,
    .getpeername = unix_socket_getpeername};
