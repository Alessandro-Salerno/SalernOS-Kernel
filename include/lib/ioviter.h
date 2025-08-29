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

#include <arch/mmu.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>

// NOTE: Including lib/ringbuffer.h creates a circular dependency
struct kringbuffer;

#define KIOVITER_FINISHED(ioviter) \
    ((ioviter)->total_off == (ioviter)->total_size)
#define KIOVITER_CURR_FINISHED(ioviter) \
    ((ioviter)->curr_off == (ioviter)->curr_iov->iov_len)
#define KIOVITER_REMAINING(ioviter) \
    ((ioviter)->total_size - (ioviter)->total_off)
#define KIOVITER_CURR_REMAINING(ioviter) \
    ((ioviter)->curr_iov->iov_len - (ioviter)->curr_off)

typedef struct kioviter {
    struct iovec *first_iov;
    int           iovcnt;

    struct iovec *curr_iov;
    size_t        curr_off;

    size_t total_off;
    size_t total_size;
} kioviter_t;

void          kioviter_init(kioviter_t *ioviter, struct iovec *iov, int iovcnt);
size_t        kioviter_skip(kioviter_t *ioviter, size_t count);
size_t        kioviter_goto(kioviter_t *ioviter, size_t off);
int           kioviter_read(void       *buf,
                            size_t      buflen,
                            size_t     *bytes_read,
                            kioviter_t *ioviter);
int           kioviter_write(size_t     *bytes_written,
                             kioviter_t *ioviter,
                             void       *buf,
                             size_t      buflen);
int           kioviter_memset(kioviter_t *ioviter, uint8_t value, size_t len);
int           kioviter_read_to_ringbuffer(size_t             *bytes_read,
                                          struct kringbuffer *rb,
                                          bool                blocking,
                                          void (*callback)(void *),
                                          void       *cb_arg,
                                          void       *hu_arg,
                                          kioviter_t *ioviter,
                                          size_t      nbytes);
int           kioviter_read_to_ringbuffer_nolock(size_t             *bytes_read,
                                                 struct kringbuffer *rb,
                                                 bool                blocking,
                                                 void (*callback)(void *),
                                                 void       *cb_arg,
                                                 void       *hu_arg,
                                                 kioviter_t *ioviter,
                                                 size_t      nbytes);
int           kioviter_write_from_ringbuffer(kioviter_t         *ioviter,
                                             size_t              nbytes,
                                             size_t             *bytes_written,
                                             struct kringbuffer *rb,
                                             bool                blocking,
                                             void (*callback)(void *),
                                             void *cb_arg,
                                             void *hu_arg);
int           kioviter_write_from_ringbuffer_nolock(kioviter_t         *ioviter,
                                                    size_t              nbytes,
                                                    size_t             *bytes_written,
                                                    struct kringbuffer *rb,
                                                    bool                blocking,
                                                    void (*callback)(void *),
                                                    void *cb_arg,
                                                    void *hu_arg);
struct iovec *kioviter_next(kioviter_t *ioviter);
int           kioviter_next_page(void                **out_phys_page,
                                 size_t               *out_page_off,
                                 size_t               *out_page_rem,
                                 kioviter_t           *ioviter,
                                 arch_mmu_pagetable_t *pagetable);
