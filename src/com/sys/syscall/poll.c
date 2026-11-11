#define _GNU_SROUCE
#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <poll.h>
#include <stdatomic.h>

_Static_assert(CONFIG_OPEN_MAX * (sizeof(Polled) + sizeof(struct pollfd)) <=
                   PAGE_SIZE,
               "poll state must fit the thread scratch page");

// CREDIT: vloxei64/ke
// SYSCALL: poll(struct pollfd fds[], nfds_t nfds, struct timespec *timeout)
SYSCALL(syscall_poll) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    u64 syscall_start = cpu_get_timestamp();

    struct pollfd *fds = SYSCALL_ARG(void *, 1);
    nfds_t nfds = SYSCALL_ARG(nfds_t, 2);
    struct timespec *timeout = SYSCALL_ARG(void *, 3);

    if (nfds > CONFIG_OPEN_MAX) return SYSCALL_ERR(EINVAL);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Poller poller = {0};
    waitlist_init(&poller.waiters);
    poller.lock = SPINLOCK_NEW();

    bool do_wait = true;
    u64 delay_ns = 0;
    if (timeout != NULL) {
        struct timespec ktimeout;
        int copy_ret = memcpy_from_user(&ktimeout, timeout, sizeof(ktimeout));
        if (copy_ret != 0) return SYSCALL_ERR(copy_ret);
        if (ktimeout.tv_sec == 0 && ktimeout.tv_nsec == 0) {
            do_wait = false;
        } else if (ktimeout.tv_sec < 0 || ktimeout.tv_nsec < 0 ||
                   (u64)ktimeout.tv_nsec >= NANOS_PER_SEC) {
            return SYSCALL_ERR(EINVAL);
        } else {
            delay_ns = ktimeout.tv_sec * NANOS_PER_SEC + ktimeout.tv_nsec;
        }
    }

    Polled *polleds = curr_thread->scratch;
    struct pollfd *kfds = (void *)&polleds[nfds];
    memset(polleds, 0, nfds * sizeof(*polleds));
    int copy_ret = memcpy_from_user(kfds, fds, nfds * sizeof(*kfds));
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

    int invalid_count = 0;
    spinlock_acq(&curr_proc->fd_lock);
    for (nfds_t i = 0; i < nfds; i++) {
        Polled *polled = &polleds[i];
        polled->poller = &poller;
        kfds[i].revents = 0;
        int fd = kfds[i].fd;

        if (fd < 0) continue;

        File *file = proc_get_file_nolock(curr_proc, fd);
        if (file == NULL) goto invalid_fd;

        PollHead *poll_head = NULL;
        if (vfs_poll_head(file->vnode, &poll_head) != 0) goto invalid_fd;

        polled->file = file;

        if (do_wait) {
            spinlock_acq(&poll_head->lock);
            LIST_INSERT_HEAD(&poll_head->polled_list, polled, polled_list);
            spinlock_rel(&poll_head->lock);
        }

        continue;
    invalid_fd:
        kfds[i].revents |= POLLNVAL;
        invalid_count++;
    }
    spinlock_rel(&curr_proc->fd_lock);

    int count = 0;
    bool interrupted = false;
    bool last_recheck = false;

    while (count == 0) {
        for (nfds_t i = 0; i < nfds; i++) {
            Polled *polled = &polleds[i];

            if (polled->file == NULL) continue;

            short revents = 0;
            vfs_poll(polled->file->vnode, kfds[i].events, &revents);
            revents &= kfds[i].events | POLLHUP | POLLERR;

            if (revents != 0) {
                kfds[i].revents = revents;
                count++;
            }
        }

        if (count != 0 || invalid_count != 0 || !do_wait || last_recheck) break;

        int wait_ret = ETIMEDOUT;

        if (delay_ns != 0) {
            u64 curr_time = cpu_get_timestamp();
            u64 elapsed = cpu_timestamp_to_ns(curr_time - syscall_start);
            if (elapsed < delay_ns) {
                syscall_start = curr_time;
                delay_ns -= elapsed;
            } else {
                goto skip_wait;
            }
        }

        spinlock_acq(&poller.lock);
        if (!poller.notified) {
            wait_ret = spinlock_wait(&poller.lock, &poller.waiters, delay_ns,
                                     true);
        } else {
            wait_ret = 0;
        }
        poller.notified = false;
        spinlock_rel(&poller.lock);

    skip_wait:

        if (wait_ret != 0) {
            interrupted = wait_ret == EINTR;
            last_recheck = true;
            continue;
        }

        if (signal_check() != SIGNAL_NONE) {
            interrupted = true;
            last_recheck = true;
            continue;
        }
    }

    for (nfds_t i = 0; i < nfds; i++) {
        Polled *polled = &polleds[i];

        PollHead *poll_head;
        if (do_wait && polled->file != NULL &&
            vfs_poll_head(polled->file->vnode, &poll_head) == 0) {
            spinlock_acq(&poll_head->lock);
            LIST_REMOVE(polled, polled_list);
            spinlock_rel(&poll_head->lock);
        }

        FILE_REL(polled->file);
    }

    copy_ret = memcpy_to_user(fds, kfds, nfds * sizeof(*kfds));
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

    if (interrupted && count + invalid_count == 0) return SYSCALL_ERR(EINTR);

    return SYSCALL_OK(count + invalid_count);
}
