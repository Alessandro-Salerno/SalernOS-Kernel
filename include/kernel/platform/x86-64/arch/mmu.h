#pragma once

#include <kernel/com/util.h>

#define MMU_KERNELSPACE_START (void *)0xffff800000000000
#define MMU_KERNELSPACE_END   (void *)0xffffffffffffffff
#define MMU_USERSPACE_START   (void *)0x0000000000000000
#define MMU_USERSPACE_END     (void *)0x00007ffffffff000

// We use a sort of guard page between userspace and the non-canonical hole to
// avoid having the system panic on sysret when a system call is issued at the
// edge

#define MMU_ADDR_IS_USER(a) ((void *)(a) < MMU_USERSPACE_END)
#define MMU_RANGE_IS_USER(a, len) \
    (MMU_ADDR_IS_USER(a) &&       \
     (u64)(len) <= (uptr)MMU_USERSPACE_END - (uptr)(void *)(a))
#define MMU_ARRAY_IS_USER(a, n, size)                \
    ((u64)(n) <= (uptr)MMU_USERSPACE_END / (size) && \
     MMU_RANGE_IS_USER(a, (u64)(n) * (size)))

#define MMU_FLAGS_READ    (u64)1
#define MMU_FLAGS_WRITE   (u64)2
#define MMU_FLAGS_USER    (u64)4
#define MMU_FLAGS_PRESENT (u64)1
#define MMU_FLAGS_NOEXEC  ((u64)1 << 63)
#define MMU_FLAGS_GLOBAL  ((u64)1 << 8)

#define MMU_FLAGS_WB 0
#define MMU_FLAGS_WT (1 << 3)
#define MMU_FLAGS_UC (1 << 4)
#define MMU_FLAGS_WC ((1 << 7) | MMU_FLAGS_WT)

// Common code (arch-agnostic) should NOT assume the presence of these flags.
// However, it may use after checking with guards. The kernel is expected to
// provide a fallback (but currently does not)
#define MMU_EXTRA_FLAGS_SHARED  (1 << 10)
#define MMU_EXTRA_FLAGS_PRIVATE (1 << 9) // same as COW
#define MMU_EXTRA_FLAGS_NOCOPY  (1 << 11)

typedef u64 MMUPageTable;
typedef u64 MMUFlags;
