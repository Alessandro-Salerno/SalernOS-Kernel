#include <kernel/com/crt/string.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/ipc/socket/unix.h>
#include <kernel/com/util.h>
#include <sys/socket.h>
#include <sys/un.h>

Socket *socket_new(SocketType type) {
    switch (type) {
        case SOCKET_TYPE_UNIX: {
            Socket *sun = socket_unix_new();
            sun->type = type;
            return sun;
        }
        default:
            break;
    }

    return NULL;
}

int socket_addr_from_abi(struct sockaddr *abi_addr, socklen_t abi_addr_len,
                         SocketAddr *out_addr) {
    switch (abi_addr->sa_family) {
        case AF_UNIX: {
            struct sockaddr_un *unaddr = (void *)abi_addr;
            usize max_len = abi_addr_len -
                offsetof(struct sockaddr_un, sun_path);
            if (abi_addr_len < offsetof(struct sockaddr_un, sun_path) ||
                max_len > sizeof(unaddr->sun_path)) {
                return EINVAL;
            }
            char *nul = memchr(unaddr->sun_path, 0, max_len);
            out_addr->local.pathlen = nul != NULL
                ? (usize)(nul - unaddr->sun_path)
                : max_len;
            memcpy(out_addr->local.path, unaddr->sun_path,
                   out_addr->local.pathlen);
            return 0;
        }

        default:
            break;
    }

    return EAFNOSUPPORT;
}

int socket_abi_from_addr(struct sockaddr *abi_addr, SocketAddr *addr,
                         SocketType socktype, socklen_t max_abi_addr_len,
                         socklen_t *out_abi_addr_len) {
    switch (socktype) {
        case SOCKET_TYPE_UNIX: {
            (void)max_abi_addr_len;
            struct sockaddr_un *unaddr = (void *)abi_addr;
            unaddr->sun_family = AF_UNIX;
            usize copy_size = min(addr->local.pathlen,
                                  sizeof(unaddr->sun_path) - 1);
            memcpy(unaddr->sun_path, addr->local.path, copy_size);
            unaddr->sun_path[copy_size] = 0;
            // Unnamed sockets only have the family, the others include the NUL
            *out_abi_addr_len = offsetof(struct sockaddr_un, sun_path);
            if (copy_size != 0) *out_abi_addr_len += copy_size + 1;
            return 0;
        }

        default:
            assert(false);
            break;
    }

    return EAFNOSUPPORT;
}

SocketType socket_type_from_abi(int domain, int type, int protocol) {
    (void)type;
    (void)protocol;

    switch (domain) {
        case AF_UNIX:
            return SOCKET_TYPE_UNIX;
        default:
            break;
    }

    return SOCKET_TYPE_INVAL;
}
