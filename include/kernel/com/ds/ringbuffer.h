#pragma once

#include <arch/info.h>
#include <kernel/com/sync/sync.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>

// NOTE: do not change this or Pty will be too large
#define RINGBUFFER_SIZE (PAGE_SIZE / 4)

#define RINGBUFFER_OP_READ  0
#define RINGBUFFER_OP_WRITE 1

// Operations with atomic_size = 1 require only one byte to proceed. The
// remaining bytes will be read/writteen after each wait (if blocking)
#define RINGBUFFER_NOATOMIC 1

typedef struct Ringbuffer {
    struct {
        // Default buffer
        u8 buffer[RINGBUFFER_SIZE];
    } internal;

    u8 *buffer;
    usize buffer_size;
    Sync condvar;

    struct {
        Waitlist queue;
        usize index;
    } write;
    struct {
        Waitlist queue;
        usize index;
    } read;
    bool is_eof;

    int (*check_hangup)(usize curr_nbytes, struct Ringbuffer *rb, int op,
                        void *arg, usize *out_new_nbytes,
                        bool *out_force_return);
    // If passed NULL as hu_arg, calls to check_hangup will use this instead.
    // This double system is present for flexibility: a Tty function may pass a
    // NULL hu_arg to a Pty, but individual hu_arg are useful in case other
    // usecases arise
    void *fallback_hu_arg;
} Ringbuffer;

int ringbuffer_write_nolock(Ringbuffer *rb, void *buf, usize buflen,
                            usize atomic_size, bool blocking,
                            void (*callback)(void *), void *cb_arg,
                            void *hu_arg, usize *out_bytes_written);
int ringbuffer_write(Ringbuffer *rb, void *buf, usize buflen, usize atomic_size,
                     bool blocking, void (*callback)(void *), void *cb_arg,
                     void *hu_arg, usize *out_bytes_written);
int ringbuffer_read_nolock(void *dst, Ringbuffer *rb, usize nbytes,
                           usize atomic_size, bool blocking,
                           void (*callback)(void *), void *cb_arg, void *hu_arg,
                           usize *out_bytes_read);
int ringbuffer_read(void *dst, Ringbuffer *rb, usize nbytes, usize atomic_size,
                    bool blocking, void (*callback)(void *), void *cb_arg,
                    void *hu_arg, usize *out_bytes_read);
short ringbuffer_poll(Ringbuffer *rb);
usize ringbuffer_avail_read(Ringbuffer *rb);
void ringbuffer_wake_all(Ringbuffer *rb);

static inline usize ringbuffer_avail_read_nolock(const Ringbuffer *rb) {
    return rb->write.index - rb->read.index;
}

static inline usize ringbuffer_avail_write_nolock(const Ringbuffer *rb) {
    return rb->buffer_size - ringbuffer_avail_read_nolock(rb);
}

void ringbuffer_init_custom(Ringbuffer *rb, void *buffer, usize buffer_size);
void ringbuffer_init(Ringbuffer *rb);
