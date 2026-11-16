#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/ds/hashmap.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/sync.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <limits.h>
#include <stdatomic.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

struct Futex {
    Waitlist waiters;
};

static Mutex g_futex_lock;
static Hashmap g_futex_map;

void syscall_futex_init(void) {
    log("initializing futex");
    verify(HASHMAP_INIT(&g_futex_map) == 0);
    mutex_init(&g_futex_lock);
}

// SYSCALL: futex(u32 *word_ptr, int op, u32 val)
SYSCALL(syscall_futex) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    u32 *word_ptr = SYSCALL_ARG(u32 *, 1);
    int op = SYSCALL_ARG(int, 2);
    u32 value = SYSCALL_ARG(u32, 3);

    if (!MMU_RANGE_IS_USER(word_ptr, sizeof(*word_ptr))) {
        return SYSCALL_ERR(EFAULT);
    }
    if ((uptr)word_ptr % sizeof(*word_ptr) != 0) return SYSCALL_ERR(EINVAL);

    Proc *curr_proc = cpu_get_thread()->proc;
    void *phys = NULL;
    int phys_ret = vmm_get_physical(curr_proc->vmm_context, word_ptr, &phys);
    if (phys_ret != 0) return SYSCALL_ERR(phys_ret);

    u32 temp = value;

    switch (op) {
        case FUTEX_WAIT:
            mutex_acq(&g_futex_lock);
            if (__atomic_compare_exchange_n(
                    word_ptr, // cast away const (we don't change value anyway)
                    &temp,    // expected value
                    value,    // new value (same as expected, so no change)
                    false,    // don't allow spurious failures
                    __ATOMIC_ACQUIRE, // memory order for success
                    __ATOMIC_RELAXED  // memory order for failure
                    )) {
                struct Futex *futex;

                int get_ret = HASHMAP_GET(&futex, &g_futex_map, &phys);

                if (get_ret == ENOENT) {
                    struct Futex *default_futex = slab_alloc(
                        sizeof(struct Futex));
                    int put_ret = ENOMEM;
                    if (default_futex != NULL) {
                        waitlist_init(&default_futex->waiters);
                        put_ret = HASHMAP_PUT(&g_futex_map, &phys,
                                              default_futex);
                    }
                    if (put_ret != 0) {
                        mutex_rel(&g_futex_lock);
                        slab_free(default_futex, sizeof(struct Futex));
                        return SYSCALL_ERR(put_ret);
                    }
                    futex = default_futex;
                } else if (get_ret != 0) {
                    mutex_rel(&g_futex_lock);
                    return SYSCALL_ERR(get_ret);
                }

                mutex_wait(&g_futex_lock, &futex->waiters, 0, true);
                mutex_rel(&g_futex_lock);

                if (signal_check() != SIGNAL_NONE) return SYSCALL_ERR(EINTR);
            } else {
                mutex_rel(&g_futex_lock);
                return SYSCALL_ERR(EAGAIN);
            }
            break;

        case FUTEX_WAKE: {
            struct Futex *futex;

            mutex_acq(&g_futex_lock);
            int get_ret = HASHMAP_GET(&futex, &g_futex_map, &phys);
            mutex_rel(&g_futex_lock);

            if (get_ret != 0) return SYSCALL_OK(0);

            if (value >= INT_MAX) sched_notify_all(&futex->waiters);
            else sched_notify_n(&futex->waiters, value);

            return SYSCALL_OK(value);
        }

        default:
            return SYSCALL_ERR(ENOSYS);
    }

    return SYSCALL_OK(0);
}
