#include <arch/barrier.h>
#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>

enum UsercopyOp {
    USERCOPY_MEMCPY,
    USERCOPY_MEMMOVE,
    USERCOPY_MEMSET
};

struct Usercopy {
    enum UsercopyOp op;
    void *dst;
    void *src;
    usize n;
    u8 val;
};

static void usercopy_do(struct Usercopy *uc) {
    switch (uc->op) {
        case USERCOPY_MEMCPY:
            memcpy(uc->dst, uc->src, uc->n);
            break;
        case USERCOPY_MEMMOVE:
            memmove(uc->dst, uc->src, uc->n);
            break;
        case USERCOPY_MEMSET:
            memset(uc->dst, uc->val, uc->n);
            break;
    }
}

static void usercopy_armed(Context *ctx, void *arg) {
    Thread *curr_thread = cpu_get_thread();
    assert(curr_thread->usercopy_ctx == NULL);
    curr_thread->usercopy_ctx = ctx;
    barrier_generic_write();
    usercopy_do(arg);
    barrier_generic_write();
    curr_thread->usercopy_ctx = NULL;
}

static int usercopy(struct Usercopy *uc, bool user) {
    if (!user) {
        usercopy_do(uc);
        return 0;
    }
    return ctx_save_and_call(usercopy_armed, uc);
}

// NOTE: bounding user ranges keeps every access, in either direction, out of
// the non-canonical hole and kernel space
static inline bool maybe_user_ok(const void *ptr, usize n) {
    return !MMU_ADDR_IS_USER(ptr) || MMU_RANGE_IS_USER(ptr, n);
}

int memcpy_maybe_user(void *dst, const void *src, usize buffsize) {
    if (!maybe_user_ok(dst, buffsize) || !maybe_user_ok(src, buffsize)) {
        return EFAULT;
    }
    struct Usercopy uc = {.op = USERCOPY_MEMCPY,
                          .dst = dst,
                          .src = (void *)src,
                          .n = buffsize};
    return usercopy(&uc, MMU_ADDR_IS_USER(dst) || MMU_ADDR_IS_USER(src));
}

int memmove_maybe_user(void *dst, void *src, usize n) {
    if (!maybe_user_ok(dst, n) || !maybe_user_ok(src, n)) return EFAULT;
    struct Usercopy uc = {.op = USERCOPY_MEMMOVE,
                          .dst = dst,
                          .src = src,
                          .n = n};
    return usercopy(&uc, MMU_ADDR_IS_USER(dst) || MMU_ADDR_IS_USER(src));
}

int memset_maybe_user(void *buff, u8 val, usize buffsize) {
    if (!maybe_user_ok(buff, buffsize)) return EFAULT;
    struct Usercopy uc = {.op = USERCOPY_MEMSET,
                          .dst = buff,
                          .n = buffsize,
                          .val = val};
    return usercopy(&uc, MMU_ADDR_IS_USER(buff));
}

int memcpy_to_user(void *dst, const void *src, usize buffsize) {
    if (!MMU_RANGE_IS_USER(dst, buffsize)) return EFAULT;
    return memcpy_maybe_user(dst, src, buffsize);
}

int memcpy_from_user(void *dst, const void *src, usize buffsize) {
    if (!MMU_RANGE_IS_USER(src, buffsize)) return EFAULT;
    return memcpy_maybe_user(dst, src, buffsize);
}

int memset_user(void *buff, u8 val, usize buffsize) {
    if (!MMU_RANGE_IS_USER(buff, buffsize)) return EFAULT;
    return memset_maybe_user(buff, val, buffsize);
}

bool usercopy_fixup(Context *ctx) {
    Thread *curr_thread = cpu_get_thread();
    // NOTE: a user copy can only fail while running in the kernel, so a fixup
    // cannot happen on a user context
    if (curr_thread == NULL || curr_thread->usercopy_ctx == NULL ||
        context_is_user(ctx)) {
        return false;
    }
    CONTEXT_RESUME_SAVED(ctx, curr_thread->usercopy_ctx, EFAULT);
    curr_thread->usercopy_ctx = NULL;
    return true;
}
