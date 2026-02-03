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

#define NUM_STACK_POLLEDS       8
#define POLL_SHOULD_ALLOC(nfds) ((nfds) > NUM_STACK_POLLEDS)

struct poll_timeout_arg {
    com_poller_t *poller;
    atomic_bool   invalidated;
    bool          expired;
};

static void poll_timeout(com_callout_t *callout) {
    struct poll_timeout_arg *data = callout->arg;

    // If the timeout expires after the poll call has ended (either because an
    // event was caught before it or because a signal interrupted it), we cannot
    // know for sure wether the function is still running or not (might be in
    // the last few lines), but we know for sure it's not going to use this
    // struct ever again, so we can free it
    if (atomic_load(&data->invalidated)) {
        com_mm_slab_free(data, sizeof(struct poll_timeout_arg));
        return;
    }

    ksync_acquire(&data->poller->lock);
    data->expired = true;
    ksync_release(&data->poller->lock);
    ksync_notify_all(&data->poller->lock, &data->poller->waiters);
    // here the struct is not freed because the function will use it, and it
    // will be responsible for cleaning up the memory
}

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

    struct poll_timeout_arg *timeout_arg = NULL;
    com_poller_t             poller      = {0};
    COM_SYS_THREAD_WAITLIST_INIT(&poller.waiters);
    KSYNC_INIT_SPINLOCK(&poller.lock);

    bool do_wait = true;
    if (NULL != timeout) {
        if (0 == timeout->tv_sec && 0 == timeout->tv_nsec) {
            do_wait = false;
        } else if (timeout->tv_nsec >= 0 && timeout->tv_nsec >= 0) {
            uintmax_t delay_ns = timeout->tv_sec * KNANOS_PER_SEC +
                                 timeout->tv_nsec;
            timeout_arg = com_mm_slab_alloc(sizeof(struct poll_timeout_arg));
            timeout_arg->poller = &poller;
            ksync_acquire(&poller.lock);
            com_sys_callout_add(poll_timeout, timeout_arg, delay_ns);
            ksync_release(&poller.lock);
        }
    }

    com_polled_t  stack_polleds[NUM_STACK_POLLEDS] = {0};
    com_polled_t *polleds                          = stack_polleds;
    size_t polleds_pages = nfds * sizeof(com_polled_t) / ARCH_PAGE_SIZE + 1;
    if (POLL_SHOULD_ALLOC(nfds)) {
        polleds = (void *)ARCH_PHYS_TO_HHDM(
            com_mm_pmm_alloc_many_zero(polleds_pages));
    }

    kspinlock_acquire(&curr_proc->fd_lock);
    for (nfds_t i = 0; i < nfds; i++) {
        com_polled_t *polled = &polleds[i];
        polled->poller       = &poller;
        polled->file         = NULL; // overriden if all checks pass
        fds[i].revents       = 0;
        int fd               = fds[i].fd;

        if (fd < 0) {
            continue;
        }

        com_file_t *file = com_sys_proc_get_file_nolock(curr_proc, fd);
        if (NULL == file) {
            continue;
        }

        com_poll_head_t *poll_head = NULL;
        if (0 != com_fs_vfs_poll_head(&poll_head, file->vnode)) {
            continue;
        }

        polled->file = file;

        if (do_wait) {
            kspinlock_acquire(&poll_head->lock);
            LIST_INSERT_HEAD(&poll_head->polled_list, polled, polled_list);
            kspinlock_release(&poll_head->lock);
        }
    }
    kspinlock_release(&curr_proc->fd_lock);

    int count = 0;
    int sig   = COM_IPC_SIGNAL_NONE;

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

        if (0 != count || !do_wait) {
            break;
        }

        ksync_acquire(&poller.lock);

        // If timeout expired before we got here
        if (NULL != timeout_arg && timeout_arg->expired) {
            ksync_release(&poller.lock);
            com_mm_slab_free(timeout_arg, sizeof(struct poll_timeout_arg));
            timeout_arg = NULL; // do not invalidate freed memory
            break;
        }

        ksync_wait(&poller.lock, &poller.waiters);

        // If we were notified by the callout
        if (NULL != timeout_arg && timeout_arg->expired) {
            ksync_release(&poller.lock);
            com_mm_slab_free(timeout_arg, sizeof(struct poll_timeout_arg));
            timeout_arg = NULL; // do not invalidate freed memory
            break;
        }

        ksync_release(&poller.lock);
        sig = com_ipc_signal_check();

        if (COM_IPC_SIGNAL_NONE != sig) {
            break;
        }
    }

    // TODO: I think there is a race condition here: if timeout arrives here,
    // the structure is not freed. Not a big deal for now though

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

    // This poll is over, so we need to invalidate any callout data left
    // otherwise we risk creating stack issues as callout_data->poller points to
    // this syscall's stack
    if (NULL != timeout_arg) {
        atomic_store(&timeout_arg->invalidated, true);
    }

    if (COM_IPC_SIGNAL_NONE != sig) {
        return COM_SYS_SYSCALL_ERR(EINTR);
    }

    return COM_SYS_SYSCALL_OK(count);
}
