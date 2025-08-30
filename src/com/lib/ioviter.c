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

#include <arch/info.h>
#include <errno.h>
#include <kernel/platform/mmu.h>
#include <lib/ioviter.h>
#include <lib/ringbuffer.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdbool.h>

// CREDIT: Mathwnd/Astral
// This code was inspired by the Astral Operating System

// TODO: check user access to pages pointed to by the iovecs

// NOTE: names may be a bit confusing: the ioviter is NEVER the subject: you
// read FROM the ioviter into a buffer, write from a ringbuffer INTO the ioviter
// etc. I didn't go crazy, it makes sense in some cases: if you read from the
// file system, you then want to WRITE to the user buffer, and vice versa

static size_t ioviter_size(kioviter_t *ioviter) {
    size_t size = 0;

    for (int i = 0; i < ioviter->iovcnt; i++) {
        size += ioviter->first_iov[i].iov_len;
    }

    return size;
}

void kioviter_init(kioviter_t *ioviter, struct iovec *iov, int iovcnt) {
    ioviter->first_iov  = iov;
    ioviter->iovcnt     = iovcnt;
    ioviter->curr_iov   = iov;
    ioviter->curr_off   = 0;
    ioviter->total_off  = 0;
    ioviter->total_size = ioviter_size(ioviter);
}

size_t kioviter_skip(kioviter_t *ioviter, size_t count) {
    do {
        size_t curr_rem  = ioviter->curr_iov->iov_len - ioviter->curr_off;
        size_t curr_skip = KMIN(curr_rem, count);
        count -= curr_skip;

        ioviter->total_off += curr_skip;
        ioviter->curr_off += curr_skip;

        if (KIOVITER_CURR_FINISHED(ioviter) && !KIOVITER_FINISHED(ioviter)) {
            ioviter->curr_iov++; // ++ pointer arithmetic
            ioviter->curr_off = 0;
        }
    } while (0 != count && !KIOVITER_FINISHED(ioviter));

    return KIOVITER_REMAINING(ioviter);
}

size_t kioviter_goto(kioviter_t *ioviter, size_t off) {
    // Skip ahead fast path, don't need to go back to go forwards!
    if (off > ioviter->total_off) {
        return kioviter_skip(ioviter, off - ioviter->total_off);
    }

    // There might be a way to avoid rewinding here too
    ioviter->curr_iov  = ioviter->first_iov;
    ioviter->curr_off  = 0;
    ioviter->total_off = 0;
    return kioviter_skip(ioviter, off);
}

int kioviter_read(void       *buf,
                  size_t      buflen,
                  size_t     *bytes_read,
                  kioviter_t *ioviter) {
    size_t tot_rem    = KMIN(buflen, KIOVITER_REMAINING(ioviter));
    size_t read_count = 0;

    do {
        size_t curr_rem  = KIOVITER_CURR_REMAINING(ioviter);
        size_t copy_size = KMIN(curr_rem, tot_rem);

        kmemcpy((uint8_t *)buf + read_count,
                (uint8_t *)ioviter->curr_iov->iov_base + ioviter->curr_off,
                copy_size);

        kioviter_skip(ioviter, copy_size);
        read_count += copy_size;
        tot_rem -= copy_size;
    } while (0 != tot_rem && !KIOVITER_FINISHED(ioviter));

    if (NULL != bytes_read) {
        *bytes_read = read_count;
    }

    return 0;
}

int kioviter_write(size_t     *bytes_written,
                   kioviter_t *ioviter,
                   void       *buf,
                   size_t      buflen) {
    size_t tot_rem     = KMIN(buflen, KIOVITER_REMAINING(ioviter));
    size_t write_count = 0;

    do {
        size_t curr_rem  = KIOVITER_CURR_REMAINING(ioviter);
        size_t copy_size = KMIN(curr_rem, tot_rem);

        kmemcpy((uint8_t *)ioviter->curr_iov->iov_base + ioviter->curr_off,
                (uint8_t *)buf + write_count,
                copy_size);

        kioviter_skip(ioviter, copy_size);
        write_count += copy_size;
        tot_rem -= copy_size;
    } while (0 != tot_rem && !KIOVITER_FINISHED(ioviter));

    if (NULL != bytes_written) {
        *bytes_written = write_count;
    }

    return 0;
}

int kioviter_memset(kioviter_t *ioviter, uint8_t value, size_t len) {
    size_t tot_rem = KMIN(len, KIOVITER_REMAINING(ioviter));

    do {
        size_t curr_rem  = KIOVITER_CURR_REMAINING(ioviter);
        size_t copy_size = KMIN(curr_rem, tot_rem);

        kmemset((uint8_t *)ioviter->curr_iov->iov_base + ioviter->curr_off,
                len,
                value);

        kioviter_skip(ioviter, copy_size);
        tot_rem -= copy_size;
    } while (0 != tot_rem && !KIOVITER_FINISHED(ioviter));

    return 0;
}

int kioviter_read_to_ringbuffer(size_t        *bytes_written,
                                kringbuffer_t *rb,
                                size_t         atomic_size,
                                bool           blocking,
                                void (*callback)(void *),
                                void       *cb_arg,
                                void       *hu_arg,
                                kioviter_t *ioviter,
                                size_t      nbytes) {
    kspinlock_acquire(&rb->lock);
    int ret = kioviter_read_to_ringbuffer_nolock(bytes_written,
                                                 rb,
                                                 atomic_size,
                                                 blocking,
                                                 callback,
                                                 cb_arg,
                                                 hu_arg,
                                                 ioviter,
                                                 nbytes);
    kspinlock_release(&rb->lock);
    return ret;
}

int kioviter_read_to_ringbuffer_nolock(size_t        *bytes_read,
                                       kringbuffer_t *rb,
                                       size_t         atomic_size,
                                       bool           blocking,
                                       void (*callback)(void *),
                                       void       *cb_arg,
                                       void       *hu_arg,
                                       kioviter_t *ioviter,
                                       size_t      nbytes) {
    size_t init_off   = ioviter->curr_off;
    size_t tot_rem    = KMIN(nbytes, KIOVITER_REMAINING(ioviter));
    size_t read_count = 0;
    int    ret        = 0;

    do {
        size_t curr_rem   = KIOVITER_CURR_REMAINING(ioviter);
        size_t copy_size  = KMIN(curr_rem, tot_rem);
        size_t curr_count = 0;

        ret = kringbuffer_write_nolock(&curr_count,
                                       rb,
                                       ioviter->curr_iov->iov_base +
                                           ioviter->curr_off,
                                       copy_size,
                                       atomic_size,
                                       blocking,
                                       callback,
                                       cb_arg,
                                       hu_arg);
        if (0 != ret) {
            break;
        }

        kioviter_skip(ioviter, copy_size);
        read_count += curr_count;
        tot_rem -= curr_count;
    } while (0 != tot_rem && !KIOVITER_FINISHED(ioviter));

    if (NULL != bytes_read) {
        *bytes_read = read_count;
    }

    if (0 != ret) {
        kioviter_goto(ioviter, init_off + read_count);
    }

    return ret;
}

int kioviter_write_from_ringbuffer(kioviter_t    *ioviter,
                                   size_t         nbytes,
                                   size_t        *bytes_written,
                                   kringbuffer_t *rb,
                                   size_t         atomic_size,
                                   bool           blocking,
                                   void (*callback)(void *),
                                   void *cb_arg,
                                   void *hu_arg) {
    kspinlock_acquire(&rb->lock);
    int ret = kioviter_write_from_ringbuffer_nolock(ioviter,
                                                    nbytes,
                                                    bytes_written,
                                                    rb,
                                                    atomic_size,
                                                    blocking,
                                                    callback,
                                                    cb_arg,
                                                    hu_arg);
    kspinlock_release(&rb->lock);
    return ret;
}

int kioviter_write_from_ringbuffer_nolock(kioviter_t    *ioviter,
                                          size_t         nbytes,
                                          size_t        *bytes_written,
                                          kringbuffer_t *rb,
                                          size_t         atomic_size,
                                          bool           blocking,
                                          void (*callback)(void *),
                                          void *cb_arg,
                                          void *hu_arg) {
    size_t init_off    = ioviter->curr_off;
    size_t tot_rem     = KMIN(nbytes, KIOVITER_REMAINING(ioviter));
    size_t write_count = 0;
    int    ret         = 0;

    do {
        size_t curr_rem   = KIOVITER_CURR_REMAINING(ioviter);
        size_t copy_size  = KMIN(curr_rem, tot_rem);
        size_t curr_count = 0;

        ret = kringbuffer_read_nolock((uint8_t *)ioviter->curr_iov->iov_base +
                                          ioviter->curr_off,
                                      &curr_count,
                                      rb,
                                      copy_size,
                                      atomic_size,
                                      blocking,
                                      callback,
                                      cb_arg,
                                      hu_arg);
        if (0 != ret) {
            break;
        }

        kioviter_skip(ioviter, copy_size);
        write_count += curr_count;
        tot_rem -= curr_count;
    } while (0 != tot_rem && !KIOVITER_FINISHED(ioviter));

    if (NULL != bytes_written) {
        *bytes_written = write_count;
    }

    if (0 != ret) {
        kioviter_goto(ioviter, init_off + write_count);
    }

    return ret;
}

struct iovec *kioviter_next(kioviter_t *ioviter) {
    if (KIOVITER_FINISHED(ioviter)) {
        return NULL;
    }

    ioviter->total_off += ioviter->curr_iov->iov_len - ioviter->curr_off;
    ioviter->curr_off = 0;
    return ioviter->curr_iov++;
}

int kioviter_next_page(void                **out_phys_page,
                       size_t               *out_page_off,
                       size_t               *out_page_rem,
                       kioviter_t           *ioviter,
                       arch_mmu_pagetable_t *pagetable) {
    size_t    curr_rem  = KIOVITER_CURR_REMAINING(ioviter);
    void     *phys_page = NULL;
    uintptr_t page_off  = 0;
    size_t    page_rem  = 0;
    int       ret       = 0;

    if (0 != curr_rem) {
        void *vaddr = (void *)((uint8_t *)ioviter->curr_iov->iov_base +
                               ioviter->curr_off);
        page_off    = (uintptr_t)vaddr % ARCH_PAGE_SIZE;
        void *paddr = arch_mmu_get_physical(pagetable, vaddr);
        if (NULL == paddr) {
            ret = EFAULT;
            goto end;
        }
        phys_page = (void *)ARCH_PAGE_ROUND(paddr);
        page_rem  = KMIN(ARCH_PAGE_SIZE, curr_rem);
    }

end:
    if (NULL != out_phys_page) {
        *out_phys_page = phys_page;
    }

    if (NULL != out_page_off) {
        *out_page_off = page_off;
    }

    if (NULL != out_page_rem) {
        *out_page_rem = page_rem;
    }

    return ret;
}
