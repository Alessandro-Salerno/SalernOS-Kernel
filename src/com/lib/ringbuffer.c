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

#include <errno.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/signal.h>
#include <lib/mem.h>
#include <lib/ringbuffer.h>
#include <lib/util.h>
#include <stdint.h>

// CREDIT: vloxei64/ke
// This is my reinterpretation of his code.

static int write_blocking(kringbuffer_t *rb,
                          void          *buf,
                          size_t        *buflen,
                          void (*callback)(void *),
                          void *cb_arg) {
    size_t to_write      = *buflen;
    size_t bytes_written = 0;

    while (to_write > 0) {
        size_t space = KRINGBUFFER_SIZE - (rb->write.index - rb->read.index);

        while (0 == space) {
            com_sys_sched_wait(&rb->write.queue, &rb->lock);

            int sig = com_sys_signal_check();

            if (COM_SYS_SIGNAL_NONE != sig) {
                if (0 == bytes_written) {
                    *buflen = 0;
                    return EINTR;
                }

                *buflen = bytes_written;
                return 0;
            }

            space = KRINGBUFFER_SIZE - (rb->write.index - rb->read.index);
        }

        size_t can_write =
            KMIN(to_write, space); // Available space (till end + wrap around)
        size_t idxoff = rb->write.index % KRINGBUFFER_SIZE;
        size_t left   = KRINGBUFFER_SIZE -
                      idxoff; // Positions left till th end of the array

        kmemcpy(&rb->buffer[idxoff], buf, KMIN(can_write, left));
        if (can_write > left) {
            kmemcpy(rb->buffer, &((uint8_t *)buf)[left], can_write - left);
        }

        rb->write.index += can_write;
        // future iterations will be able to access the  with indices starting
        // at 0 even if it is not  "real" start of the buffer
        buf = (uint8_t *)buf + can_write;
        to_write -= can_write;
        bytes_written += can_write;

        com_sys_sched_notify(&rb->read.queue);

        if (NULL != callback) {
            callback(cb_arg);
        }

        ARCH_CPU_SELF_IPI();
    }

    return 0;
}

static int write_nonblocking(kringbuffer_t *rb,
                             void          *buf,
                             size_t        *buflen,
                             void (*callback)(void *),
                             void *cb_arg) {
    size_t space = KRINGBUFFER_SIZE - (rb->write.index - rb->read.index);

    size_t can_write = KMIN(*buflen, space);
    size_t idxoff    = rb->write.index % KRINGBUFFER_SIZE;
    size_t left =
        KRINGBUFFER_SIZE - idxoff; // Positions left till th end of the array

    kmemcpy(&rb->buffer[idxoff], buf, KMIN(can_write, left));
    if (can_write > left) {
        kmemcpy(rb->buffer, &((uint8_t *)buf)[left], can_write - left);
    }

    rb->write.index += can_write;
    com_sys_sched_notify(&rb->read.queue);

    if (NULL != callback) {
        callback(cb_arg);
    }

    ARCH_CPU_SELF_IPI();
    return 0;
}

int kringbuffer_write_nolock(size_t        *bytes_written,
                             kringbuffer_t *rb,
                             void          *buf,
                             size_t         buflen,
                             bool           blocking,
                             void (*callback)(void *),
                             void *cb_arg) {
    int ret;

    if (blocking) {
        ret = write_blocking(rb, buf, &buflen, callback, cb_arg);
    } else {
        ret = write_nonblocking(rb, buf, &buflen, callback, cb_arg);
    }

    if (NULL != bytes_written) {
        *bytes_written = buflen;
    }

    return ret;
}

int kringbuffer_write(size_t        *bytes_written,
                      kringbuffer_t *rb,
                      void          *buf,
                      size_t         buflen,
                      bool           blocking,
                      void (*callback)(void *),
                      void *cb_arg) {
    com_spinlock_acquire(&rb->lock);
    int ret = kringbuffer_write_nolock(
        bytes_written, rb, buf, buflen, blocking, callback, cb_arg);
    com_spinlock_release(&rb->lock);
    return ret;
}

// TODO: the problem here, according to the author, is that if you get multiple
// lines (say in a caninical mode tty while the application is waiting), you end
// up reading all the lines even if you should read only one (by definition of
// line-buffered). The solution is to use kmemchr to find the first \n (though,
// applied to the ringbuffer with wrap around) and read at most the number of
// bytes between the current index and the first \n (if it exists). Though, for
// abstraction sake, this should be optional. Might be better to handle this in
// the caller, and expose a "kringbuffer_memchr" function here
int kringbuffer_read_nolock(void          *dst,
                            size_t        *bytes_read,
                            kringbuffer_t *rb,
                            size_t         nbytes,
                            bool           blocking,
                            void (*callback)(void *),
                            void *cb_arg) {
    int ret = 0;

    while (rb->write.index == rb->read.index || rb->is_eof) {
        if (!blocking) {
            ret    = EAGAIN;
            nbytes = 0;
            goto end;
        }

        com_sys_sched_wait(&rb->read.queue, &rb->lock);

        if (rb->is_eof) {
            rb->is_eof = false;
            nbytes     = 0;
            goto end;
        }

        int sig = com_sys_signal_check();
        if (COM_SYS_SIGNAL_NONE != sig) {
            nbytes = 0;
            ret    = EINTR;
            goto end;
        }
    }

    size_t space    = rb->write.index - rb->read.index;
    size_t can_read = KMIN(nbytes, space);
    size_t idxoff   = rb->read.index % KRINGBUFFER_SIZE;
    size_t left     = KRINGBUFFER_SIZE - idxoff;

    kmemcpy(dst, &rb->buffer[idxoff], KMIN(can_read, left));
    if (can_read > left) {
        kmemcpy(&((uint8_t *)dst)[left], rb->buffer, can_read - left);
    }

    rb->read.index += can_read;
    nbytes = can_read;

    com_sys_sched_notify(&rb->write.queue);

    if (NULL != callback) {
        callback(cb_arg);
    }

end:
    if (NULL != bytes_read) {
        *bytes_read = nbytes;
    }

    return ret;
}

int kringbuffer_read(void          *dst,
                     size_t        *bytes_read,
                     kringbuffer_t *rb,
                     size_t         nbytes,
                     bool           blocking,
                     void (*callback)(void *),
                     void *cb_arg) {
    com_spinlock_acquire(&rb->lock);
    int ret = kringbuffer_read_nolock(
        dst, bytes_read, rb, nbytes, blocking, callback, cb_arg);
    com_spinlock_release(&rb->lock);
    return ret;
}
