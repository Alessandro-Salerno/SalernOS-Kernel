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

#pragma once

#include <arch/info.h>
#include <kernel/com/sys/thread.h>
#include <lib/spinlock.h>
#include <stddef.h>
#include <stdint.h>

// NOTE: do not change this or com_pty_t will be too large
#define KRINGBUFFER_SIZE (ARCH_PAGE_SIZE / 4)
#define KRINGBUFFER_AVAIL_READ(rbptr) \
    ((rbptr)->write.index - (rbptr)->read.index)
#define KRINGBUFFER_AVAIL_WRITE(rbptr) \
    ((rbptr)->buffer_size - KRINGBUFFER_AVAIL_READ(rbptr))

#define KRINGBUFFER_OP_READ  0
#define KRINGBUFFER_OP_WRITE 1

// Operations with atomic_size = 1 require only one byte to proceed. The
// remaining bytes will be read/writteen after each wait (if blocking)
#define KRINGBUFFER_NOATOMIC 1

#define KRINGBUFFER_INIT(rb_ptr)                           \
    (rb_ptr)->lock            = KSPINLOCK_NEW();           \
    (rb_ptr)->write.index     = 0;                         \
    (rb_ptr)->read.index      = 0;                         \
    (rb_ptr)->is_eof          = false;                     \
    (rb_ptr)->check_hangup    = NULL;                      \
    (rb_ptr)->fallback_hu_arg = NULL;                      \
    (rb_ptr)->buffer          = (rb_ptr)->internal.buffer; \
    (rb_ptr)->buffer_size     = KRINGBUFFER_SIZE;          \
    TAILQ_INIT(&(rb_ptr)->write.queue);                    \
    TAILQ_INIT(&(rb_ptr)->read.queue)

typedef struct kringbuffer {
    struct {
        // Default buffer
        uint8_t buffer[KRINGBUFFER_SIZE];
    } internal;

    uint8_t    *buffer;
    size_t      buffer_size;
    kspinlock_t lock;

    struct {
        struct com_thread_tailq queue;
        size_t                  index;
    } write;
    struct {
        struct com_thread_tailq queue;
        size_t                  index;
    } read;
    bool is_eof;

    int (*check_hangup)(size_t             *new_nbytes,
                        size_t              curr_nbytes,
                        bool               *force_return,
                        struct kringbuffer *rb,
                        int                 op,
                        void               *arg);
    // If passed NULL as hu_arg, calls to check_hangup will use this instead.
    // This double system is present for flexibility: a TTY function may pass a
    // NULL hu_arg to a PTY, but individual hu_arg are useful in case other
    // usecases arise
    void *fallback_hu_arg;
} kringbuffer_t;

int kringbuffer_write_nolock(size_t        *bytes_written,
                             kringbuffer_t *rb,
                             void          *buf,
                             size_t         buflen,
                             size_t         atomic_size,
                             bool           blocking,
                             void (*callback)(void *),
                             void *cb_arg,
                             void *hu_arg);
int kringbuffer_write(size_t        *bytes_written,
                      kringbuffer_t *rb,
                      void          *buf,
                      size_t         buflen,
                      size_t         atomic_size,
                      bool           blocking,
                      void (*callback)(void *),
                      void *cb_arg,
                      void *hu_arg);
int kringbuffer_read_nolock(void          *dst,
                            size_t        *bytes_read,
                            kringbuffer_t *rb,
                            size_t         nbytes,
                            size_t         atomic_size,
                            bool           blocking,
                            void (*callback)(void *),
                            void *cb_arg,
                            void *hu_arg);
int kringbuffer_read(void          *dst,
                     size_t        *bytes_read,
                     kringbuffer_t *rb,
                     size_t         nbytes,
                     size_t         atomic_size,
                     bool           blocking,
                     void (*callback)(void *),
                     void *cb_arg,
                     void *hu_arg);
