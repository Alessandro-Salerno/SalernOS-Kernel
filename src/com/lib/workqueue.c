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

#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/workqueue.h>

// NOTE: kernel threads have no process to deliver signals to, so waits must be
// uninterruptible

static void worker_main(kworkqueue_t *wq) {
    kspinlock_acquire(&wq->lock);

    while (true) {
        kwork_t *work = TAILQ_FIRST(&wq->works);
        if (NULL == work) {
            kspinlock_wait(&wq->lock, &wq->waitlist, 0, false);
            continue;
        }

        TAILQ_REMOVE(&wq->works, work, queue);
        work->pending = false;
        kspinlock_release(&wq->lock);
        work->handler(work);
        kspinlock_acquire(&wq->lock);
    }
}

void kworkqueue_init(kworkqueue_t *wq) {
    wq->lock = KSPINLOCK_NEW();
    TAILQ_INIT(&wq->works);
    COM_SYS_THREAD_WAITLIST_INIT(&wq->waitlist);
    wq->worker = com_sys_thread_new_kernel(NULL, worker_main, wq);
    com_sys_thread_ready(wq->worker);
}

void kworkqueue_push(kworkqueue_t *wq, kwork_t *work) {
    kspinlock_acquire(&wq->lock);
    if (!work->pending) {
        work->pending = true;
        TAILQ_INSERT_TAIL(&wq->works, work, queue);
        com_sys_sched_notify(&wq->waitlist);
    }
    kspinlock_release(&wq->lock);
}
