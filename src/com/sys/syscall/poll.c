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
#include <arch/info.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/syscall.h>
#include <lib/str.h>
#include <lib/util.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>

#define NUM_STACK_POLLEDS       8
#define POLL_SHOULD_ALLOC(nfds) ((nfds) > NUM_STACK_POLLEDS)

// CREDIT: vloxei64/ke
// SYSCALL: poll(struct pollfd fds[], nfds_t nfds, struct timespec *timeout)
COM_SYS_SYSCALL(com_sys_syscall_poll) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    struct pollfd   *fds     = COM_SYS_SYSCALL_ARG(void *, 1);
    nfds_t           nfds    = COM_SYS_SYSCALL_ARG(nfds_t, 2);
    struct timespec *timeout = COM_SYS_SYSCALL_ARG(void *, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_poller_t poller = {0};
    TAILQ_INIT(&poller.waiters);

    bool do_wait = true;
    (void)timeout;
    // TODO: handle timeout

    com_polled_t  stack_polleds[NUM_STACK_POLLEDS] = {0};
    com_polled_t *polleds                          = stack_polleds;
    if (POLL_SHOULD_ALLOC(nfds)) {
        polleds = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_many(
            nfds * sizeof(com_polled_t) / ARCH_PAGE_SIZE + 1));
    }

    for (nfds_t i = 0; i < nfds; i++) {
        com_polled_t *polled = &polleds[i];
        polled->poller       = &poller;
        polled->file         = NULL; // overriden if all checks pass
        fds[i].revents       = 0;
        int fd               = fds[i].fd;

        if (fd < 0) {
            continue;
        }

        com_file_t *file = com_sys_proc_get_file(curr_proc, fd);
        if (NULL == file) {
            continue;
        }

        com_poll_head_t *poll_head;
        if (0 != com_fs_vfs_poll_head(&poll_head, file->vnode)) {
            continue;
        }

        polled->file = file;

        if (do_wait) {
            com_spinlock_acquire(&poll_head->lock);
            LIST_INSERT_HEAD(&poll_head->polled_list, polled, polled_list);
            com_spinlock_release(&poll_head->lock);
        }
    }

    int count = 0;
    int sig   = COM_SYS_SIGNAL_NONE;

    while (0 == count) {
        for (nfds_t i = 0; i < nfds; i++) {
            com_polled_t *polled = &polleds[i];

            if (NULL == polled->file) {
                continue;
            }

            short revents = 0;
            com_fs_vfs_poll(&revents, polled->file->vnode, fds[i].events);

            if (0 != revents) {
                KDEBUG("passed with revents = %x", revents);
                fds[i].revents = revents;
                count++;
            }
        }

        if (0 != count || !do_wait) {
            break;
        }

        com_spinlock_acquire(&poller.lock);
        // More timeout stuff here

        com_sys_sched_wait(&poller.waiters, &poller.lock);
        // Some more timeout stuff here
        com_spinlock_release(&poller.lock);
        sig = com_sys_signal_check();

        if (COM_SYS_SIGNAL_NONE != sig) {
            break;
        }
    }

    for (nfds_t i = 0; i < nfds; i++) {
        com_polled_t *polled = &polleds[i];

        com_poll_head_t *poll_head;
        if (do_wait && NULL != polled->file &&
            0 == com_fs_vfs_poll_head(&poll_head, polled->file->vnode)) {
            com_spinlock_acquire(&poll_head->lock);
            LIST_REMOVE(polled, polled_list);
            com_spinlock_release(&poll_head->lock);
        }

        COM_FS_FILE_RELEASE(polled->file);
    }

    // Last timeout thing

    if (COM_SYS_SIGNAL_NONE != sig) {
        return COM_SYS_SYSCALL_ERR(EINTR);
    }

    KDEBUG("got back from poll with count=%d", count);
    return COM_SYS_SYSCALL_OK(count);
}
