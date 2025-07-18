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

#include <kernel/com/io/term.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <lib/str.h>
#include <stdatomic.h>
#include <vendor/tailq.h>

#define THREAD_INTERVAL 10

static _Atomic(com_term_t *) FallbackTerm = NULL;
static size_t                ThreadTicks  = 0;

static com_spinlock_t BufferedQueueLock = COM_SPINLOCK_NEW();
static TAILQ_HEAD(, com_term) BufferQueue;

static void flush_buffer_nolock(com_term_t *term) {
    if (0 == term->buffering.index) {
        goto skip;
    }

    term->backend.ops->putsn(
        term->backend.data, term->buffering.buffer, term->buffering.index);
    term->buffering.index = 0;
    com_sys_sched_notify_all(&term->buffering.waiters);

skip:
    ThreadTicks = 0;
}

static void flush_thread(void) {
    while (true) {
        if (THREAD_INTERVAL != ThreadTicks) {
            ThreadTicks++;
            goto next_time;
        }

        com_spinlock_acquire(&BufferedQueueLock);
        struct com_term *c, *_;
        TAILQ_FOREACH_SAFE(c, &BufferQueue, buffering.internal.queue, _) {
            com_spinlock_acquire(&c->lock);
            flush_buffer_nolock(c);
            com_spinlock_release(&c->lock);
        }
        com_spinlock_release(&BufferedQueueLock);

    next_time:
        // TODO: why does this not preempt?
        com_sys_sched_yield();
    }
}

com_term_t *com_io_term_new(com_term_backend_t backend) {
    com_term_t *ret        = com_mm_slab_alloc(sizeof(com_term_t));
    ret->backend           = backend;
    ret->lock              = COM_SPINLOCK_NEW();
    ret->buffering.enabled = false;
    TAILQ_INIT(&ret->buffering.waiters);

    ret->backend.data = ret->backend.ops->init();

    return ret;
}

com_term_t *com_io_term_clone(com_term_t *parent) {
    com_term_t *ret        = com_mm_slab_alloc(sizeof(com_term_t));
    ret->lock              = COM_SPINLOCK_NEW();
    ret->buffering.enabled = false;
    TAILQ_INIT(&ret->buffering.waiters);

    if (NULL != parent) {
        ret->backend.ops  = parent->backend.ops;
        ret->backend.data = ret->backend.ops->init();
        com_spinlock_acquire(&parent->lock);
        bool buffering = parent->buffering.enabled;
        com_spinlock_release(&parent->lock);
        com_io_term_set_buffering(ret, buffering);
    }

    return ret;
}

void com_io_term_putc(com_term_t *term, char c) {
    com_io_term_putsn(term, &c, 1);
}

void com_io_term_puts(com_term_t *term, const char *s) {
    com_io_term_putsn(term, s, kstrlen(s));
}

void com_io_term_putsn(com_term_t *term, const char *s, size_t n) {
    if (NULL == term) {
        term = atomic_load(&FallbackTerm);
    }

    if (NULL == term) {
        return;
    }

    com_spinlock_acquire(&term->lock);

    if (term->buffering.enabled) {
        while (term->buffering.index + n >= COM_IO_TERM_BUFSZ) {
            com_sys_sched_wait(&term->buffering.waiters, &term->lock);
        }
        kmemcpy(&term->buffering.buffer[term->buffering.index], s, n);
        term->buffering.index += n;
    } else {
        term->backend.ops->putsn(term->backend.data, s, n);
    }

    com_spinlock_release(&term->lock);
}

void com_io_term_get_size(com_term_t *term, size_t *rows, size_t *cols) {
    if (NULL == term) {
        term = atomic_load(&FallbackTerm);
    }

    if (NULL == term) {
        *rows = 0;
        *cols = 0;
        return;
    }

    term->backend.ops->get_size(term->backend.data, rows, cols);
}

void com_io_term_set_size(com_term_t *term, size_t rows, size_t cols) {
    if (NULL == term) {
        term = atomic_load(&FallbackTerm);
    }

    if (NULL == term) {
        return;
    }

    term->backend.ops->set_size(term->backend.data, rows, cols);
}

void com_io_term_flush(com_term_t *term) {
    if (NULL == term) {
        term = atomic_load(&FallbackTerm);
    }

    if (NULL == term) {
        return;
    }

    com_spinlock_acquire(&term->lock);

    if (term->buffering.enabled) {
        flush_buffer_nolock(term);
    }
    term->backend.ops->flush(term->backend.data);

    com_spinlock_release(&term->lock);
}

void com_io_term_set_buffering(com_term_t *term, bool state) {
    if (NULL == term) {
        term = atomic_load(&FallbackTerm);
    }

    if (NULL == term) {
        return;
    }

    com_spinlock_acquire(&term->lock);

    if (state != term->buffering.enabled) {
        if (term->buffering.enabled) {
            flush_buffer_nolock(term);
        }
        term->backend.ops->flush(term->backend.data);
        term->buffering.enabled = state;
    }

    com_spinlock_release(&term->lock);

    com_spinlock_acquire(&BufferedQueueLock);
    TAILQ_INSERT_TAIL(&BufferQueue, term, buffering.internal.queue);
    com_spinlock_release(&BufferedQueueLock);
}

void com_io_term_enable(com_term_t *term) {
    if (NULL == term) {
        return;
    }

    term->backend.ops->enable(term->backend.data);
}

void com_io_term_disable(com_term_t *term) {
    if (NULL == term) {
        return;
    }

    term->backend.ops->disable(term->backend.data);
}

void com_io_term_set_fallback(com_term_t *fallback_term) {
    atomic_store(&FallbackTerm, fallback_term);
}

void com_io_term_init(void) {
    TAILQ_INIT(&BufferQueue);
    com_thread_t *print_thread = com_sys_thread_new_kernel(NULL, flush_thread);
    // com_sys_thread_ready(PrintThread);
    com_spinlock_acquire(&ARCH_CPU_GET()->runqueue_lock);
    print_thread->runnable = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, print_thread, threads);
    com_spinlock_release(&ARCH_CPU_GET()->runqueue_lock);
}
