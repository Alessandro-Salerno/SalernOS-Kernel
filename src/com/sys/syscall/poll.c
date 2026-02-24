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

#define _GNU_SROUCE
#include <arch/cpu.h>
#include <arch/info.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <lib/util.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>

#define NUM_STACK_POLLEDS       32
#define POLL_SHOULD_ALLOC(nfds) ((nfds) > NUM_STACK_POLLEDS)

// CREDIT: vloxei64/ke
// SYSCALL: poll(struct pollfd fds[], nfds_t nfds, struct timespec *timeout)
COM_SYS_SYSCALL(com_sys_syscall_poll) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    uintmax_t syscall_start = ARCH_CPU_GET_TIMESTAMP();

    struct pollfd   *fds     = COM_SYS_SYSCALL_ARG(void *, 1);
    nfds_t           nfds    = COM_SYS_SYSCALL_ARG(nfds_t, 2);
    struct timespec *timeout = COM_SYS_SYSCALL_ARG(void *, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_poller_t poller = {0};
    COM_SYS_THREAD_WAITLIST_INIT(&poller.waiters);

    bool      do_wait  = true;
    uintmax_t delay_ns = 0;
    if (NULL != timeout) {
        if (0 == timeout->tv_sec && 0 == timeout->tv_nsec) {
            do_wait = false;
        } else if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                   (uintmax_t)timeout->tv_nsec >= KNANOS_PER_SEC) {
            return COM_SYS_SYSCALL_ERR(EINVAL);
        } else if (timeout->tv_sec >= 0 && timeout->tv_nsec >= 0) {
            delay_ns = timeout->tv_sec * KNANOS_PER_SEC + timeout->tv_nsec;
        }
    }

    com_polled_t  stack_polleds[NUM_STACK_POLLEDS] = {0};
    com_polled_t *polleds                          = stack_polleds;
    size_t polleds_pages = nfds * sizeof(com_polled_t) / ARCH_PAGE_SIZE + 1;
    if (POLL_SHOULD_ALLOC(nfds)) {
        polleds = (void *)ARCH_PHYS_TO_HHDM(
            com_mm_pmm_alloc_many_zero(polleds_pages));
    }

    int invalid_count = 0;
    kspinlock_acquire(&curr_proc->fd_lock);
    for (nfds_t i = 0; i < nfds; i++) {
        com_polled_t *polled = &polleds[i];
        polled->poller       = &poller;
        fds[i].revents       = 0;
        int fd               = fds[i].fd;

        if (fd < 0) {
            goto invalid_fd;
        }

        com_file_t *file = com_sys_proc_get_file_nolock(curr_proc, fd);
        if (NULL == file) {
            goto invalid_fd;
        }

        com_poll_head_t *poll_head = NULL;
        if (0 != com_fs_vfs_poll_head(&poll_head, file->vnode)) {
            goto invalid_fd;
        }

        polled->file = file;

        if (do_wait) {
            kspinlock_acquire(&poll_head->lock);
            LIST_INSERT_HEAD(&poll_head->polled_list, polled, polled_list);
            kspinlock_release(&poll_head->lock);
        }

        continue;
    invalid_fd:
        fds[i].revents |= POLLNVAL;
        invalid_count++;
    }
    kspinlock_release(&curr_proc->fd_lock);

    int  count        = 0;
    int  sig          = COM_IPC_SIGNAL_NONE;
    bool last_recheck = false;

    while (0 == count) {
        for (nfds_t i = 0; i < nfds; i++) {
            com_polled_t *polled = &polleds[i];

            if (NULL == polled->file) {
                continue;
            }

            short revents = 0;
            com_fs_vfs_poll(&revents, polled->file->vnode, fds[i].events);
            revents &= fds[i].events | POLLHUP | POLLERR;

            if (0 != revents) {
                fds[i].revents = revents;
                count++;
            }
        }

        if (0 != count || !do_wait || last_recheck) {
            break;
        }

        int wait_ret = ETIMEDOUT;

        if (0 != delay_ns) {
            uintmax_t curr_time = ARCH_CPU_GET_TIMESTAMP();
            uintmax_t elapsed   = ARCH_CPU_TIMESTAMP_TO_NS(curr_time -
                                                         syscall_start);
            if (elapsed < delay_ns) {
                syscall_start = curr_time;
                delay_ns -= elapsed;
            } else {
                goto skip_wait;
            }
        }

        wait_ret = com_sys_sched_wait_nodrop_timeout(&poller.waiters, delay_ns);
    skip_wait:

        if (ETIMEDOUT == wait_ret) {
            last_recheck = true;
            continue;
        }

        sig = com_ipc_signal_check();

        if (COM_IPC_SIGNAL_NONE != sig) {
            last_recheck = true;
            continue;
        }
    }

    for (nfds_t i = 0; i < nfds; i++) {
        com_polled_t *polled = &polleds[i];

        com_poll_head_t *poll_head;
        if (do_wait && NULL != polled->file &&
            0 == com_fs_vfs_poll_head(&poll_head, polled->file->vnode)) {
            kspinlock_acquire(&poll_head->lock);
            LIST_REMOVE(polled, polled_list);
            kspinlock_release(&poll_head->lock);
        }

        COM_FS_FILE_RELEASE(polled->file);
    }

    // If we had allocated polleds, we shall also free it
    if (POLL_SHOULD_ALLOC(nfds)) {
        com_mm_pmm_free_many((void *)ARCH_HHDM_TO_PHYS(polleds), polleds_pages);
    }

    if (COM_IPC_SIGNAL_NONE != sig && 0 == count + invalid_count) {
        return COM_SYS_SYSCALL_ERR(EINTR);
    }

    return COM_SYS_SYSCALL_OK(count + invalid_count);
}
