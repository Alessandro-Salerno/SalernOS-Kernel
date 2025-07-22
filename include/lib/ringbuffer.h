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
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <stddef.h>
#include <stdint.h>

#define KRINGBUFFER_SIZE (ARCH_PAGE_SIZE / 4)

#define KRINGBUFFER_INIT(rb_ptr)                \
    (rb_ptr)->lock        = COM_SPINLOCK_NEW(); \
    (rb_ptr)->write.index = 0;                  \
    (rb_ptr)->read.index  = 0;                  \
    (rb_ptr)->is_eof      = false;              \
    TAILQ_INIT(&(rb_ptr)->write.queue);         \
    TAILQ_INIT(&(rb_ptr)->read.queue)

typedef struct kringbuffer {
    uint8_t        buffer[KRINGBUFFER_SIZE];
    com_spinlock_t lock;
    struct {
        struct com_thread_tailq queue;
        size_t                  index;
    } write;
    struct {
        struct com_thread_tailq queue;
        size_t                  index;
    } read;
    bool is_eof;
} kringbuffer_t;

int kringbuffer_write_nolock(size_t        *bytes_written,
                             kringbuffer_t *rb,
                             void          *buf,
                             size_t         buflen,
                             bool           blocking,
                             void (*callback)(void *),
                             void *cb_arg);
int kringbuffer_write(size_t        *bytes_written,
                      kringbuffer_t *rb,
                      void          *buf,
                      size_t         buflen,
                      bool           blocking,
                      void (*callback)(void *),
                      void *cb_arg);
int kringbuffer_read_nolock(void          *dst,
                            size_t        *bytes_read,
                            kringbuffer_t *rb,
                            size_t         nbytes,
                            bool           blocking,
                            void (*callback)(void *),
                            void *cb_arg);
int kringbuffer_read(void          *dst,
                     size_t        *bytes_read,
                     kringbuffer_t *rb,
                     size_t         nbytes,
                     bool           blocking,
                     void (*callback)(void *),
                     void *cb_arg);
