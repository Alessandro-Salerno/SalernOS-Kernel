/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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

#include <arch/barrier.h>
#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/sys/thread.h>
#include <lib/mem.h>
#include <lib/usercopy.h>
#include <lib/util.h>

enum usercopy_op {
    USERCOPY_MEMCPY,
    USERCOPY_MEMMOVE,
    USERCOPY_MEMSET
};

struct usercopy {
    enum usercopy_op op;
    void            *dst;
    void            *src;
    size_t           n;
    uint8_t          val;
};

static void usercopy_do(struct usercopy *uc) {
    switch (uc->op) {
        case USERCOPY_MEMCPY:
            kmemcpy(uc->dst, uc->src, uc->n);
            break;
        case USERCOPY_MEMMOVE:
            kmemmove(uc->dst, uc->src, uc->n);
            break;
        case USERCOPY_MEMSET:
            kmemset(uc->dst, uc->n, uc->val);
            break;
    }
}

static void usercopy_armed(arch_context_t *ctx, void *arg) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(NULL == curr_thread->usercopy_ctx);
    curr_thread->usercopy_ctx = ctx;
    ARCH_BARRIER_GENERIC_WRITE();
    usercopy_do(arg);
    ARCH_BARRIER_GENERIC_WRITE();
    curr_thread->usercopy_ctx = NULL;
}

static int usercopy(struct usercopy *uc, bool user) {
    if (!user) {
        usercopy_do(uc);
        return 0;
    }
    return ARCH_CONTEXT_SAVE_AND_CALL(usercopy_armed, uc);
}

// NOTE: bounding user ranges keeps every access, in either direction, out of
// the non-canonical hole and kernel space
static inline bool maybe_user_ok(const void *ptr, size_t n) {
    return !ARCH_MMU_ADDR_IS_USER(ptr) || ARCH_MMU_RANGE_IS_USER(ptr, n);
}

int kmemcpy_maybe_user(void *dst, const void *src, size_t buffsize) {
    if (!maybe_user_ok(dst, buffsize) || !maybe_user_ok(src, buffsize)) {
        return EFAULT;
    }
    struct usercopy uc = {.op  = USERCOPY_MEMCPY,
                          .dst = dst,
                          .src = (void *)src,
                          .n   = buffsize};
    return usercopy(&uc,
                    ARCH_MMU_ADDR_IS_USER(dst) || ARCH_MMU_ADDR_IS_USER(src));
}

int kmemmove_maybe_user(void *dst, void *src, size_t n) {
    if (!maybe_user_ok(dst, n) || !maybe_user_ok(src, n)) {
        return EFAULT;
    }
    struct usercopy uc = {.op  = USERCOPY_MEMMOVE,
                          .dst = dst,
                          .src = src,
                          .n   = n};
    return usercopy(&uc,
                    ARCH_MMU_ADDR_IS_USER(dst) || ARCH_MMU_ADDR_IS_USER(src));
}

int kmemset_maybe_user(void *buff, size_t buffsize, uint8_t val) {
    if (!maybe_user_ok(buff, buffsize)) {
        return EFAULT;
    }
    struct usercopy uc = {.op  = USERCOPY_MEMSET,
                          .dst = buff,
                          .n   = buffsize,
                          .val = val};
    return usercopy(&uc, ARCH_MMU_ADDR_IS_USER(buff));
}

int kmemcpy_to_user(void *dst, const void *src, size_t buffsize) {
    if (!ARCH_MMU_RANGE_IS_USER(dst, buffsize)) {
        return EFAULT;
    }
    return kmemcpy_maybe_user(dst, src, buffsize);
}

int kmemcpy_from_user(void *dst, const void *src, size_t buffsize) {
    if (!ARCH_MMU_RANGE_IS_USER(src, buffsize)) {
        return EFAULT;
    }
    return kmemcpy_maybe_user(dst, src, buffsize);
}

int kmemset_user(void *buff, size_t buffsize, uint8_t val) {
    if (!ARCH_MMU_RANGE_IS_USER(buff, buffsize)) {
        return EFAULT;
    }
    return kmemset_maybe_user(buff, buffsize, val);
}

bool kusercopy_fixup(arch_context_t *ctx) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    // NOTE: a user copy can only fail while running in the kernel, so a fixup
    // cannot happen on a user context
    if (NULL == curr_thread || NULL == curr_thread->usercopy_ctx ||
        ARCH_CONTEXT_ISUSER(ctx)) {
        return false;
    }
    ARCH_CONTEXT_RESUME_SAVED(ctx, curr_thread->usercopy_ctx, EFAULT);
    curr_thread->usercopy_ctx = NULL;
    return true;
}
