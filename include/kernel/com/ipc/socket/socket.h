#pragma once

#include <errno.h>
#include <kernel/com/ds/ioviter.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/util.h>
#include <sys/socket.h>

#define __SOCKET_OP(sock, op, ...) \
    (((sock)->ops->op != NULL) ? (sock)->ops->op(__VA_ARGS__) : ENOSYS)

#define SOCKET_BIND(sock, addr)       __SOCKET_OP(sock, bind, sock, addr)
#define SOCKET_SEND(sock, desc)       __SOCKET_OP(sock, send, sock, desc)
#define SOCKET_RECV(sock, desc)       __SOCKET_OP(sock, recv, sock, desc)
#define SOCKET_CONNECT(sock, addr)    __SOCKET_OP(sock, connect, sock, addr)
#define SOCKET_LISTEN(sock, max_conn) __SOCKET_OP(sock, listen, sock, max_conn)
#define SOCKET_ACCEPT(server, flags, out_peer, out_addr) \
    __SOCKET_OP(server, accept, server, flags, out_peer, out_addr)
#define SOCKET_GETNAME(sock, out)     __SOCKET_OP(sock, getname, sock, out)
#define SOCKET_GETPEERNAME(sock, out) __SOCKET_OP(sock, getpeername, sock, out)
#define SOCKET_POLLHEAD(sock, out)    (*(out) = &(sock)->pollhead, 0)
#define SOCKET_POLL(sock, events, out_revents) \
    __SOCKET_OP(sock, poll, sock, events, out_revents)
#define SOCKET_DESTROY(sock)        __SOCKET_OP(sock, destroy, sock)
#define SOCKET_IOCTL(sock, op, buf) __SOCKET_OP(sock, ioctl, sock, op, buf)

typedef enum SocketType {
    SOCKET_TYPE_INVAL,
    SOCKET_TYPE_UNIX
} SocketType;

typedef enum SocketState {
    SOCKET_STATE_UNBOUND,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_LISTENING
} SocketState;

typedef union SocketAddr {
    struct {
        char path[CONFIG_PATH_MAX];
        usize pathlen;
    } local;
} SocketAddr;

typedef struct Socket {
    SocketType type;
    struct SocketOps *ops;
    Mutex lock;
    SocketState state;
    PollHead pollhead;
} Socket;

typedef struct SocketDesc {
    SocketAddr *addr;
    IOVIter *ioviter;
    usize count;
    u64 flags;
    int msgflags;
    usize count_done;
} SocketDesc;

typedef struct SocketOps {
    int (*bind)(Socket *socket, SocketAddr *addr);
    int (*send)(Socket *socket, SocketDesc *desc);
    int (*recv)(Socket *socket, SocketDesc *desc);
    int (*connect)(Socket *socket, SocketAddr *addr);
    int (*listen)(Socket *socket, usize max_connections);
    int (*accept)(Socket *server, u64 flags, Socket **out_peer,
                  SocketAddr *out_addr);
    int (*getname)(Socket *socket, SocketAddr *out_addr);
    int (*getpeername)(Socket *socket, SocketAddr *out_addr);
    int (*poll)(Socket *socket, short events, short *out_revents);
    int (*destroy)(Socket *socket);
    int (*ioctl)(Socket *socket, u64 op, void *buf);
} SocketOps;

Socket *socket_new(SocketType type);
int socket_addr_from_abi(struct sockaddr *abi_addr, socklen_t abi_addr_len,
                         SocketAddr *out_addr);
int socket_abi_from_addr(struct sockaddr *abi_addr, SocketAddr *addr,
                         SocketType socktype, socklen_t max_abi_addr_len,
                         socklen_t *out_abi_addr_len);
SocketType socket_type_from_abi(int domain, int type, int protocol);
