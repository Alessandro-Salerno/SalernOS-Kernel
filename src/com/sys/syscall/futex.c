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

#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <lib/hashmap.h>
#include <lib/sync.h>
#include <lib/util.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

struct futex {
    com_waitlist_t waiters;
};

static kmutex_t   FutexLock;
static khashmap_t FutexMap;

void com_sys_syscall_futex_init(void) {
    KLOG("initializing futex");
    KHASHMAP_INIT(&FutexMap);
    KMUTEX_INIT(&FutexLock);
}

// SYSCALL: futex(uint32_t *word_ptr, int op, uint32_t val)
COM_SYS_SYSCALL(com_sys_syscall_futex) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    // TODO: validate and pre-fault this to avoid issues with CoW zero page
    uint32_t *word_ptr = COM_SYS_SYSCALL_ARG(uint32_t *, 1);
    int       op       = COM_SYS_SYSCALL_ARG(int, 2);
    uint32_t  value    = COM_SYS_SYSCALL_ARG(uint32_t, 3);

    com_proc_t *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    uintptr_t phys = (uintptr_t)com_mm_vmm_get_physical(curr_proc->vmm_context,
                                                        word_ptr);

    uint32_t temp = value;

    switch (op) {
        case FUTEX_WAIT:
            if (__atomic_compare_exchange_n(
                    word_ptr, // cast away const (we don't change value anyway)
                    &temp,    // expected value
                    value,    // new value (same as expected, so no change)
                    false,    // don't allow spurious failures
                    __ATOMIC_ACQUIRE, // memory order for success
                    __ATOMIC_RELAXED  // memory order for failure
                    )) {
                struct futex *futex;

                kmutex_acquire(&FutexLock);
                int get_ret = KHASHMAP_GET(&futex, &FutexMap, &phys);

                if (ENOENT == get_ret) {
                    struct futex *default_futex = com_mm_slab_alloc(
                        sizeof(struct futex));
                    COM_SYS_THREAD_WAITLIST_INIT(&default_futex->waiters);

                    KASSERT_CALL(0,
                                 ==,
                                 KHASHMAP_PUT(&FutexMap, &phys, default_futex));
                    futex = default_futex;
                } else if (0 != get_ret) {
                    kmutex_release(&FutexLock);
                    return COM_SYS_SYSCALL_ERR(get_ret);
                }

                kmutex_release(&FutexLock);
                com_sys_sched_wait_nodrop(&futex->waiters);

                if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
                    return COM_SYS_SYSCALL_ERR(EINTR);
                }
            } else {
                return COM_SYS_SYSCALL_ERR(EAGAIN);
            }
            break;

        case FUTEX_WAKE: {
            struct futex *futex;

            kmutex_acquire(&FutexLock);
            int get_ret = KHASHMAP_GET(&futex, &FutexMap, &phys);
            kmutex_release(&FutexLock);

            if (0 != get_ret) {
                return COM_SYS_SYSCALL_OK(0);
            }

            if (value >= INT_MAX) {
                com_sys_sched_notify_all(&futex->waiters);
            } else {
                com_sys_sched_notify_n(&futex->waiters, value);
            }

            return COM_SYS_SYSCALL_OK(value);
        }

        default:
            return COM_SYS_SYSCALL_ERR(ENOSYS);
    }

    return COM_SYS_SYSCALL_OK(0);
}
