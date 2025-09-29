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
#include <lib/spinlock.h>
#include <lib/util.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

struct futex {
    struct com_thread_tailq waiters;
};

// TODO: use per-futex locks
static kspinlock_t FutexLock = KSPINLOCK_NEW();
static khashmap_t  FutexMap;
static bool        FutexInit = false;

// SYSCALL: futex(uint32_t *word_ptr, int op, uint32_t val)
COM_SYS_SYSCALL(com_sys_syscall_futex) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    uint32_t *word_ptr = COM_SYS_SYSCALL_ARG(uint32_t *, 1);
    int       op       = COM_SYS_SYSCALL_ARG(int, 2);
    uint32_t  value    = COM_SYS_SYSCALL_ARG(uint32_t, 3);

    com_proc_t       *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    com_syscall_ret_t ret       = COM_SYS_SYSCALL_BASE_OK();
    uintptr_t phys = (uintptr_t)com_mm_vmm_get_physical(curr_proc->vmm_context,
                                                        word_ptr);

    kspinlock_acquire(&FutexLock);

    if (!FutexInit) {
        KHASHMAP_INIT(&FutexMap);
        FutexInit = true;
    }

    uint32_t temp = value;

    switch (op) {
        case FUTEX_WAIT:
            if (__atomic_compare_exchange_n(
                    word_ptr, // cast away const (we don't change value anyway)
                    &temp,    // expected value
                    value,    // new value (same as expected, so no change)
                    false,    // don't allow spurious failures
                    __ATOMIC_SEQ_CST, // memory order for success
                    __ATOMIC_SEQ_CST  // memory order for failure
                    )) {
                struct futex *futex;
                int           get_ret = KHASHMAP_GET(&futex, &FutexMap, &phys);
                if (ENOENT == get_ret) {
                    struct futex *default_futex = com_mm_slab_alloc(
                        sizeof(struct futex));
                    TAILQ_INIT(&default_futex->waiters);

                    KASSERT(0 == KHASHMAP_PUT(&FutexMap, &phys, default_futex));
                    futex = default_futex;
                } else if (0 != get_ret) {
                    ret = COM_SYS_SYSCALL_ERR(get_ret);
                    goto end;
                }
                com_sys_sched_wait(&futex->waiters, &FutexLock);
                if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
                    ret = COM_SYS_SYSCALL_ERR(EINTR);
                    goto end;
                }
            }
            break;

        case FUTEX_WAKE: {
            struct futex *futex;
            int           get_ret = KHASHMAP_GET(&futex, &FutexMap, &phys);
            if (ENOENT == get_ret) {
                goto end;
            } else if (0 != get_ret) {
                ret = COM_SYS_SYSCALL_ERR(get_ret);
                goto end;
            }
            if (INT_MAX == value) {
                com_sys_sched_notify_all(&futex->waiters);
            } else if (1 == value) {
                com_sys_sched_notify(&futex->waiters);
            } else {
                ret = COM_SYS_SYSCALL_ERR(EINVAL);
            }
            break;
        }

        default:
            ret = COM_SYS_SYSCALL_ERR(ENOSYS);
            goto end;
    }

end:
    kspinlock_release(&FutexLock);
    return ret;
}
