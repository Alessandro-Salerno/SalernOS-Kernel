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
#include <arch/mmu.h>
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
#include <lib/mem.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <lib/usercopy.h>
#include <lib/util.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>

_Static_assert(CONFIG_OPEN_MAX *
                       (sizeof(com_polled_t) + sizeof(struct pollfd)) <=
                   ARCH_PAGE_SIZE,
               "poll state must fit the thread scratch page");

// CREDIT: vloxei64/ke
// SYSCALL: poll(struct pollfd fds[], nfds_t nfds, struct timespec *timeout)
COM_SYS_SYSCALL(com_sys_syscall_poll) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    uintmax_t syscall_start = ARCH_CPU_GET_TIMESTAMP();

    struct pollfd   *fds     = COM_SYS_SYSCALL_ARG(void *, 1);
    nfds_t           nfds    = COM_SYS_SYSCALL_ARG(nfds_t, 2);
    struct timespec *timeout = COM_SYS_SYSCALL_ARG(void *, 3);

    if (nfds > CONFIG_OPEN_MAX) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_poller_t poller = {0};
    COM_SYS_THREAD_WAITLIST_INIT(&poller.waiters);
    poller.lock = KSPINLOCK_NEW();

    bool      do_wait  = true;
    uintmax_t delay_ns = 0;
    if (NULL != timeout) {
        struct timespec ktimeout;
        int copy_ret = kmemcpy_from_user(&ktimeout, timeout, sizeof(ktimeout));
        if (0 != copy_ret) {
            return COM_SYS_SYSCALL_ERR(copy_ret);
        }
        if (0 == ktimeout.tv_sec && 0 == ktimeout.tv_nsec) {
            do_wait = false;
        } else if (ktimeout.tv_sec < 0 || ktimeout.tv_nsec < 0 ||
                   (uintmax_t)ktimeout.tv_nsec >= KNANOS_PER_SEC) {
            return COM_SYS_SYSCALL_ERR(EINVAL);
        } else {
            delay_ns = ktimeout.tv_sec * KNANOS_PER_SEC + ktimeout.tv_nsec;
        }
    }

    com_polled_t  *polleds = curr_thread->scratch;
    struct pollfd *kfds    = (void *)&polleds[nfds];
    kmemset(polleds, nfds * sizeof(*polleds), 0);
    int copy_ret = kmemcpy_from_user(kfds, fds, nfds * sizeof(*kfds));
    if (0 != copy_ret) {
        return COM_SYS_SYSCALL_ERR(copy_ret);
    }

    int invalid_count = 0;
    kspinlock_acquire(&curr_proc->fd_lock);
    for (nfds_t i = 0; i < nfds; i++) {
        com_polled_t *polled = &polleds[i];
        polled->poller       = &poller;
        kfds[i].revents      = 0;
        int fd               = kfds[i].fd;

        if (fd < 0) {
            continue;
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
        kfds[i].revents |= POLLNVAL;
        invalid_count++;
    }
    kspinlock_release(&curr_proc->fd_lock);

    int  count        = 0;
    bool interrupted  = false;
    bool last_recheck = false;

    while (0 == count) {
        for (nfds_t i = 0; i < nfds; i++) {
            com_polled_t *polled = &polleds[i];

            if (NULL == polled->file) {
                continue;
            }

            short revents = 0;
            com_fs_vfs_poll(&revents, polled->file->vnode, kfds[i].events);
            revents &= kfds[i].events | POLLHUP | POLLERR;

            if (0 != revents) {
                kfds[i].revents = revents;
                count++;
            }
        }

        if (0 != count || 0 != invalid_count || !do_wait || last_recheck) {
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

        kspinlock_acquire(&poller.lock);
        if (!poller.notified) {
            wait_ret = kspinlock_wait(&poller.lock,
                                      &poller.waiters,
                                      delay_ns,
                                      true);
        } else {
            wait_ret = 0;
        }
        poller.notified = false;
        kspinlock_release(&poller.lock);

    skip_wait:

        if (0 != wait_ret) {
            interrupted  = EINTR == wait_ret;
            last_recheck = true;
            continue;
        }

        if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
            interrupted  = true;
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

    copy_ret = kmemcpy_to_user(fds, kfds, nfds * sizeof(*kfds));
    if (0 != copy_ret) {
        return COM_SYS_SYSCALL_ERR(copy_ret);
    }

    if (interrupted && 0 == count + invalid_count) {
        return COM_SYS_SYSCALL_ERR(EINTR);
    }

    return COM_SYS_SYSCALL_OK(count + invalid_count);
}
