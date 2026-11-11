#include <arch/info.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ds/ioviter.h>
#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <limits.h>

// CREDIT: Mathwnd/Astral
// This code was inspired by the Astral Operating System

// NOTE: names may be a bit confusing: the ioviter is NEVER the subject: you
// read FROM the ioviter into a buffer, write from a ringbuffer INTO the ioviter
// etc. I didn't go crazy, it makes sense in some cases: if you read from the
// file system, you then want to WRITE to the user buffer, and vice versa

static usize ioviter_size(IOVIter *ioviter) {
    usize size = 0;

    for (int i = 0; i < ioviter->iovcnt; i++) {
        size += ioviter->first_iov[i].iov_len;
    }

    return size;
}

void ioviter_init(IOVIter *ioviter, struct iovec *iov, int iovcnt) {
    ioviter->first_iov = iov;
    ioviter->iovcnt = iovcnt;
    ioviter->curr_iov = iov;
    ioviter->curr_off = 0;
    ioviter->total_off = 0;
    ioviter->total_size = ioviter_size(ioviter);
}

// kiov must hold CONFIG_IOV_MAX iovecs, so the user array is read only once
int ioviter_init_user(IOVIter *ioviter, struct iovec *kiov,
                      const struct iovec *uiov, int iovcnt) {
    if (iovcnt < 0 || iovcnt > CONFIG_IOV_MAX) return EINVAL;

    int ret = memcpy_from_user(kiov, uiov, iovcnt * sizeof(*kiov));
    if (ret != 0) return ret;

    usize total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (kiov[i].iov_len > SSIZE_MAX - total) return EINVAL;
        if (!MMU_RANGE_IS_USER(kiov[i].iov_base, kiov[i].iov_len)) {
            return EFAULT;
        }
        total += kiov[i].iov_len;
    }

    ioviter_init(ioviter, kiov, iovcnt);
    return 0;
}

usize ioviter_skip(IOVIter *ioviter, usize count) {
    do {
        usize curr_rem = ioviter->curr_iov->iov_len - ioviter->curr_off;
        usize curr_skip = min(curr_rem, count);
        count -= curr_skip;

        ioviter->total_off += curr_skip;
        ioviter->curr_off += curr_skip;

        if (IOVITER_CURR_FINISHED(ioviter) && !IOVITER_FINISHED(ioviter)) {
            ioviter->curr_iov++; // ++ pointer arithmetic
            ioviter->curr_off = 0;
        }
    } while (count != 0 && !IOVITER_FINISHED(ioviter));

    return IOVITER_REMAINING(ioviter);
}

usize ioviter_goto(IOVIter *ioviter, usize off) {
    // Skip ahead fast path, don't need to go back to go forwards!
    if (off > ioviter->total_off) {
        return ioviter_skip(ioviter, off - ioviter->total_off);
    }

    // There might be a way to avoid rewinding here too
    ioviter->curr_iov = ioviter->first_iov;
    ioviter->curr_off = 0;
    ioviter->total_off = 0;
    return ioviter_skip(ioviter, off);
}

int ioviter_read(void *buf, usize buflen, IOVIter *ioviter,
                 usize *out_bytes_read) {
    usize tot_rem = min(buflen, IOVITER_REMAINING(ioviter));
    usize read_count = 0;
    int ret = 0;

    do {
        usize curr_rem = IOVITER_CURR_REMAINING(ioviter);
        usize copy_size = min(curr_rem, tot_rem);

        ret = memcpy_maybe_user(
            (u8 *)buf + read_count,
            (u8 *)ioviter->curr_iov->iov_base + ioviter->curr_off, copy_size);
        if (ret != 0) break;

        ioviter_skip(ioviter, copy_size);
        read_count += copy_size;
        tot_rem -= copy_size;
    } while (tot_rem != 0 && !IOVITER_FINISHED(ioviter));

    if (out_bytes_read != NULL) *out_bytes_read = read_count;

    return read_count > 0 ? 0 : ret;
}

int ioviter_write(IOVIter *ioviter, void *buf, usize buflen,
                  usize *out_bytes_written) {
    usize tot_rem = min(buflen, IOVITER_REMAINING(ioviter));
    usize write_count = 0;
    int ret = 0;

    do {
        usize curr_rem = IOVITER_CURR_REMAINING(ioviter);
        usize copy_size = min(curr_rem, tot_rem);

        ret = memcpy_maybe_user((u8 *)ioviter->curr_iov->iov_base +
                                    ioviter->curr_off,
                                (u8 *)buf + write_count, copy_size);
        if (ret != 0) break;

        ioviter_skip(ioviter, copy_size);
        write_count += copy_size;
        tot_rem -= copy_size;
    } while (tot_rem != 0 && !IOVITER_FINISHED(ioviter));

    if (out_bytes_written != NULL) *out_bytes_written = write_count;

    return write_count > 0 ? 0 : ret;
}

int ioviter_memset(IOVIter *ioviter, u8 value, usize len) {
    usize tot_rem = min(len, IOVITER_REMAINING(ioviter));

    do {
        usize curr_rem = IOVITER_CURR_REMAINING(ioviter);
        usize copy_size = min(curr_rem, tot_rem);

        int ret = memset_maybe_user((u8 *)ioviter->curr_iov->iov_base +
                                        ioviter->curr_off,
                                    value, copy_size);
        if (ret != 0) return ret;

        ioviter_skip(ioviter, copy_size);
        tot_rem -= copy_size;
    } while (tot_rem != 0 && !IOVITER_FINISHED(ioviter));

    return 0;
}

int ioviter_read_to_ringbuffer(Ringbuffer *rb, usize atomic_size, bool blocking,
                               void (*callback)(void *), void *cb_arg,
                               void *hu_arg, IOVIter *ioviter, usize nbytes,
                               usize *out_bytes_written) {
    sync_acq(&rb->condvar);
    int ret = ioviter_read_to_ringbuffer_nolock(
        rb, atomic_size, blocking, callback, cb_arg, hu_arg, ioviter, nbytes,
        out_bytes_written);
    sync_rel(&rb->condvar);
    return ret;
}

int ioviter_read_to_ringbuffer_nolock(Ringbuffer *rb, usize atomic_size,
                                      bool blocking, void (*callback)(void *),
                                      void *cb_arg, void *hu_arg,
                                      IOVIter *ioviter, usize nbytes,
                                      usize *out_bytes_read) {
    usize tot_rem = min(nbytes, IOVITER_REMAINING(ioviter));
    usize read_count = 0;
    int ret = 0;

    do {
        usize curr_rem = IOVITER_CURR_REMAINING(ioviter);
        usize copy_size = min(curr_rem, tot_rem);
        usize curr_count = 0;

        ret = ringbuffer_write_nolock(
            rb, ioviter->curr_iov->iov_base + ioviter->curr_off, copy_size,
            atomic_size, blocking, callback, cb_arg, hu_arg, &curr_count);
        if (ret != 0) break;

        ioviter_skip(ioviter, curr_count);
        read_count += curr_count;
        tot_rem -= curr_count;

        if (curr_count < copy_size) break;
    } while (tot_rem != 0 && !IOVITER_FINISHED(ioviter));

    if (out_bytes_read != NULL) *out_bytes_read = read_count;

    return read_count > 0 ? 0 : ret;
}

int ioviter_write_from_ringbuffer(IOVIter *ioviter, usize nbytes,
                                  Ringbuffer *rb, usize atomic_size,
                                  bool blocking, void (*callback)(void *),
                                  void *cb_arg, void *hu_arg,
                                  usize *out_bytes_written) {
    sync_acq(&rb->condvar);
    int ret = ioviter_write_from_ringbuffer_nolock(
        ioviter, nbytes, rb, atomic_size, blocking, callback, cb_arg, hu_arg,
        out_bytes_written);
    sync_rel(&rb->condvar);
    return ret;
}

int ioviter_write_from_ringbuffer_nolock(IOVIter *ioviter, usize nbytes,
                                         Ringbuffer *rb, usize atomic_size,
                                         bool blocking,
                                         void (*callback)(void *), void *cb_arg,
                                         void *hu_arg,
                                         usize *out_bytes_written) {
    usize tot_rem = min(nbytes, IOVITER_REMAINING(ioviter));
    usize write_count = 0;
    int ret = 0;

    do {
        usize curr_rem = IOVITER_CURR_REMAINING(ioviter);
        usize copy_size = min(curr_rem, tot_rem);
        usize curr_count = 0;

        ret = ringbuffer_read_nolock((u8 *)ioviter->curr_iov->iov_base +
                                         ioviter->curr_off,
                                     rb, copy_size, atomic_size, blocking,
                                     callback, cb_arg, hu_arg, &curr_count);
        if (ret != 0) break;

        ioviter_skip(ioviter, curr_count);
        write_count += curr_count;
        tot_rem -= curr_count;

        if (curr_count < copy_size) break;
    } while (tot_rem != 0 && !IOVITER_FINISHED(ioviter));

    if (out_bytes_written != NULL) *out_bytes_written = write_count;

    return write_count > 0 ? 0 : ret;
}

struct iovec *ioviter_next(IOVIter *ioviter) {
    if (IOVITER_FINISHED(ioviter)) return NULL;

    ioviter->total_off += ioviter->curr_iov->iov_len - ioviter->curr_off;
    ioviter->curr_off = 0;
    return ioviter->curr_iov++;
}

int ioviter_next_page(IOVIter *ioviter, VMMContext *vmm_context,
                      void **out_phys_page, usize *out_page_off,
                      usize *out_page_rem) {
    usize curr_rem = IOVITER_CURR_REMAINING(ioviter);
    void *phys_page = NULL;
    uptr page_off = 0;
    usize page_rem = 0;
    int ret = 0;

    if (curr_rem != 0) {
        void *vaddr = (void *)((u8 *)ioviter->curr_iov->iov_base +
                               ioviter->curr_off);
        page_off = (uptr)vaddr % PAGE_SIZE;
        void *paddr = vmm_get_physical(vmm_context, vaddr);
        if (paddr == NULL) {
            ret = EFAULT;
            goto end;
        }
        phys_page = (void *)PAGE_ROUND(paddr);
        page_rem = min(PAGE_SIZE, curr_rem);
    }

end:
    if (out_phys_page != NULL) *out_phys_page = phys_page;
    if (out_page_off != NULL) *out_page_off = page_off;
    if (out_page_rem != NULL) *out_page_rem = page_rem;

    return ret;
}
