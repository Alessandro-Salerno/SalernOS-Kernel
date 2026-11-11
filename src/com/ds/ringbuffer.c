#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <poll.h>

// CREDIT: vloxei64/ke
// This is my reinterpretation of his code.

#define CHECK_HANGUP_GENERIC(curr_nbytes, rb, op, arg, out_new_nbytes)       \
    {                                                                        \
        bool force_return = false;                                           \
        int hup_ret = check_hangup(curr_nbytes, rb, op, arg, out_new_nbytes, \
                                   &force_return);                           \
        if (hup_ret != 0 || force_return) {                                  \
            return hup_ret;                                                  \
        }                                                                    \
    }

static int check_hangup(usize curr_nbytes, Ringbuffer *rb, int op, void *arg,
                        usize *out_new_nbytes, bool *out_force_return) {
    if (rb->check_hangup == NULL) return 0;

    if (arg == NULL) arg = rb->fallback_hu_arg;

    return rb->check_hangup(curr_nbytes, rb, op, arg, out_new_nbytes,
                            out_force_return);
}

// A user fault under a spinlock panics, so spinlock rings only take kernel
// memory and their callers bounce user data through the stack
static int rb_memcpy(Ringbuffer *rb, void *dst, const void *src, usize n) {
    if (!(rb->condvar.flags & SYNC_FLAGS_MUTEX)) {
        assert(!MMU_ADDR_IS_USER(dst) && !MMU_ADDR_IS_USER(src));
        memcpy(dst, src, n);
        return 0;
    }

    return memcpy_maybe_user(dst, src, n);
}

// The ring wraps around, so a copy may take two pieces
static int rb_copy_in(Ringbuffer *rb, const void *buf, usize n) {
    usize idxoff = mod_pow2(rb->write.index, rb->buffer_size);
    usize left = rb->buffer_size - idxoff;
    int ret = rb_memcpy(rb, &rb->buffer[idxoff], buf, min(n, left));
    if (ret == 0 && n > left) {
        ret = rb_memcpy(rb, rb->buffer, (const u8 *)buf + left, n - left);
    }
    return ret;
}

static int rb_copy_out(void *dst, Ringbuffer *rb, usize n) {
    usize idxoff = mod_pow2(rb->read.index, rb->buffer_size);
    usize left = rb->buffer_size - idxoff;
    int ret = rb_memcpy(rb, dst, &rb->buffer[idxoff], min(n, left));
    if (ret == 0 && n > left) {
        ret = rb_memcpy(rb, (u8 *)dst + left, rb->buffer, n - left);
    }
    return ret;
}

static int write_blocking(Ringbuffer *rb, void *buf, usize *buflen,
                          usize atomic_size, void (*callback)(void *),
                          void *cb_arg, void *hu_arg) {
    usize to_write = *buflen;
    usize bytes_written = 0;

    while (to_write > 0) {
        CHECK_HANGUP_GENERIC(0, rb, RINGBUFFER_OP_WRITE, hu_arg, buflen);
        while (ringbuffer_avail_write_nolock(rb) < atomic_size) {
            sync_wait(&rb->condvar, &rb->write.queue);

            int sig = signal_check();
            if (sig != SIGNAL_NONE) {
                if (bytes_written == 0) {
                    *buflen = 0;
                    return EINTR;
                }

                *buflen = bytes_written;
                return 0;
            }

            CHECK_HANGUP_GENERIC(0, rb, RINGBUFFER_OP_WRITE, hu_arg, buflen);
        }

        usize can_write = min(
            to_write,
            ringbuffer_avail_write_nolock(rb)); // Available space (till
                                                // end + wrap around)
        int copy_ret = rb_copy_in(rb, buf, can_write);
        if (copy_ret != 0) {
            *buflen = bytes_written;
            return bytes_written == 0 ? copy_ret : 0;
        }

        rb->write.index += can_write;
        // future iterations will be able to access the  with indices starting
        // at 0 even if it is not  "real" start of the buffer
        buf = (u8 *)buf + can_write;
        to_write -= can_write;
        bytes_written += can_write;

        sync_notify(&rb->condvar, &rb->read.queue);

        if (callback != NULL) callback(cb_arg);
    }

    *buflen = bytes_written;
    return 0;
}

static int write_nonblocking(Ringbuffer *rb, void *buf, usize *buflen,
                             usize atomic_size, void (*callback)(void *),
                             void *cb_arg, void *hu_arg) {
    CHECK_HANGUP_GENERIC(0, rb, RINGBUFFER_OP_WRITE, hu_arg, buflen);

    usize space = ringbuffer_avail_write_nolock(rb);
    usize can_write = min(*buflen, space);

    if (can_write < atomic_size) return EAGAIN;

    int copy_ret = rb_copy_in(rb, buf, can_write);
    if (copy_ret != 0) {
        *buflen = 0;
        return copy_ret;
    }

    rb->write.index += can_write;
    sched_notify(&rb->read.queue);

    if (callback != NULL) callback(cb_arg);

    *buflen = can_write;
    return 0;
}

int ringbuffer_write_nolock(Ringbuffer *rb, void *buf, usize buflen,
                            usize atomic_size, bool blocking,
                            void (*callback)(void *), void *cb_arg,
                            void *hu_arg, usize *out_bytes_written) {
    int ret;
    assert(rb->buffer_size != 0);
    assert(atomic_size < rb->buffer_size);

    if (blocking) {
        ret = write_blocking(rb, buf, &buflen, atomic_size, callback, cb_arg,
                             hu_arg);
    } else {
        ret = write_nonblocking(rb, buf, &buflen, atomic_size, callback, cb_arg,
                                hu_arg);
    }

    if (out_bytes_written != NULL) *out_bytes_written = buflen;

    return ret;
}

int ringbuffer_write(Ringbuffer *rb, void *buf, usize buflen, usize atomic_size,
                     bool blocking, void (*callback)(void *), void *cb_arg,
                     void *hu_arg, usize *out_bytes_written) {
    sync_acq(&rb->condvar);
    int ret = ringbuffer_write_nolock(rb, buf, buflen, atomic_size, blocking,
                                      callback, cb_arg, hu_arg,
                                      out_bytes_written);
    sync_rel(&rb->condvar);
    return ret;
}

// TODO: the problem here, according to the author, is that if you get multiple
// lines (say in a caninical mode tty while the application is waiting), you end
// up reading all the lines even if you should read only one (by definition of
// line-buffered). The solution is to use memchr to find the first \n (though,
// applied to the ringbuffer with wrap around) and read at most the number of
// bytes between the current index and the first \n (if it exists). Though, for
// abstraction sake, this should be optional. Might be better to handle this in
// the caller, and expose a "kringbuffer_memchr" function here
int ringbuffer_read_nolock(void *dst, Ringbuffer *rb, usize nbytes,
                           usize atomic_size, bool blocking,
                           void (*callback)(void *), void *cb_arg, void *hu_arg,
                           usize *out_bytes_read) {
    CHECK_HANGUP_GENERIC(nbytes, rb, RINGBUFFER_OP_READ, hu_arg, &nbytes);
    int ret = 0;
    assert(atomic_size < rb->buffer_size);

    while (ringbuffer_avail_read_nolock(rb) < atomic_size && !rb->is_eof) {
        if (!blocking) {
            ret = EAGAIN;
            nbytes = 0;
            goto end;
        }

        sync_wait(&rb->condvar, &rb->read.queue);
        CHECK_HANGUP_GENERIC(nbytes, rb, RINGBUFFER_OP_READ, hu_arg, &nbytes);

        if (rb->is_eof) break;

        int sig = signal_check();
        if (sig != SIGNAL_NONE) {
            nbytes = 0;
            ret = EINTR;
            goto end;
        }
    }

    if (rb->is_eof) {
        rb->is_eof = false;
        nbytes = 0;
        goto end;
    }

    usize space = ringbuffer_avail_read_nolock(rb);
    usize can_read = min(nbytes, space);

    // The data stays in the ring if it cannot be delivered
    ret = rb_copy_out(dst, rb, can_read);
    if (ret != 0) {
        nbytes = 0;
        goto end;
    }

    rb->read.index += can_read;
    nbytes = can_read;

    sync_notify(&rb->condvar, &rb->write.queue);

    if (callback != NULL) callback(cb_arg);

end:
    if (out_bytes_read != NULL) *out_bytes_read = nbytes;

    return ret;
}

int ringbuffer_read(void *dst, Ringbuffer *rb, usize nbytes, usize atomic_size,
                    bool blocking, void (*callback)(void *), void *cb_arg,
                    void *hu_arg, usize *out_bytes_read) {
    sync_acq(&rb->condvar);
    int ret = ringbuffer_read_nolock(dst, rb, nbytes, atomic_size, blocking,
                                     callback, cb_arg, hu_arg, out_bytes_read);
    sync_rel(&rb->condvar);
    return ret;
}

short ringbuffer_poll(Ringbuffer *rb) {
    short out = 0;
    sync_acq(&rb->condvar);
    if (ringbuffer_avail_read_nolock(rb) > 0) out |= POLLIN;
    if (ringbuffer_avail_write_nolock(rb) > 0) out |= POLLOUT;
    sync_rel(&rb->condvar);
    return out;
}

usize ringbuffer_avail_read(Ringbuffer *rb) {
    sync_acq(&rb->condvar);
    usize avail = ringbuffer_avail_read_nolock(rb);
    sync_rel(&rb->condvar);
    return avail;
}

void ringbuffer_wake_all(Ringbuffer *rb) {
    sync_acq(&rb->condvar);
    sched_notify_all(&rb->read.queue);
    sched_notify_all(&rb->write.queue);
    sync_rel(&rb->condvar);
}

void ringbuffer_init_custom(Ringbuffer *rb, void *buffer, usize buffer_size) {
    rb->write.index = 0;
    rb->read.index = 0;
    rb->is_eof = false;
    rb->check_hangup = NULL;
    rb->fallback_hu_arg = NULL;
    rb->buffer = buffer;
    rb->buffer_size = buffer_size;
    sync_init_spinlock(&rb->condvar);
    waitlist_init(&rb->write.queue);
    waitlist_init(&rb->read.queue);
}

void ringbuffer_init(Ringbuffer *rb) {
    ringbuffer_init_custom(rb, rb->internal.buffer, RINGBUFFER_SIZE);
}
