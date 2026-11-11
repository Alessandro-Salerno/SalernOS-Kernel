#pragma once

#include <arch/mmu.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/util.h>
#include <sys/uio.h>

// NOTE: Including lib/ringbuffer.h creates a circular dependency
struct Ringbuffer;

#define IOVITER_FINISHED(ioviter) \
    ((ioviter)->total_off == (ioviter)->total_size)
#define IOVITER_CURR_FINISHED(ioviter) \
    ((ioviter)->curr_off == (ioviter)->curr_iov->iov_len)
#define IOVITER_REMAINING(ioviter) \
    ((ioviter)->total_size - (ioviter)->total_off)
#define IOVITER_CURR_REMAINING(ioviter) \
    ((ioviter)->curr_iov->iov_len - (ioviter)->curr_off)

typedef struct IOVIter {
    struct iovec *first_iov;
    int iovcnt;

    struct iovec *curr_iov;
    usize curr_off;

    usize total_off;
    usize total_size;
} IOVIter;

void ioviter_init(IOVIter *ioviter, struct iovec *iov, int iovcnt);
int ioviter_init_user(IOVIter *ioviter, struct iovec *kiov,
                      const struct iovec *uiov, int iovcnt);
usize ioviter_skip(IOVIter *ioviter, usize count);
usize ioviter_goto(IOVIter *ioviter, usize off);
int ioviter_read(void *buf, usize buflen, IOVIter *ioviter,
                 usize *out_bytes_read);
int ioviter_write(IOVIter *ioviter, void *buf, usize buflen,
                  usize *out_bytes_written);
int ioviter_memset(IOVIter *ioviter, u8 value, usize len);
int ioviter_read_to_ringbuffer(struct Ringbuffer *rb, usize atomic_size,
                               bool blocking, void (*callback)(void *),
                               void *cb_arg, void *hu_arg, IOVIter *ioviter,
                               usize nbytes, usize *bytes_read);
int ioviter_read_to_ringbuffer_nolock(struct Ringbuffer *rb, usize atomic_size,
                                      bool blocking, void (*callback)(void *),
                                      void *cb_arg, void *hu_arg,
                                      IOVIter *ioviter, usize nbytes,
                                      usize *out_bytes_read);
int ioviter_write_from_ringbuffer(IOVIter *ioviter, usize nbytes,
                                  struct Ringbuffer *rb, usize atomic_size,
                                  bool blocking, void (*callback)(void *),
                                  void *cb_arg, void *hu_arg,
                                  usize *out_bytes_written);
int ioviter_write_from_ringbuffer_nolock(IOVIter *ioviter, usize nbytes,
                                         struct Ringbuffer *rb,
                                         usize atomic_size, bool blocking,
                                         void (*callback)(void *), void *cb_arg,
                                         void *hu_arg,
                                         usize *out_bytes_written);
struct iovec *ioviter_next(IOVIter *ioviter);
int ioviter_next_page(IOVIter *ioviter, VMMContext *vmm_context,
                      void **out_phys_page, usize *out_page_off,
                      usize *out_page_rem);
