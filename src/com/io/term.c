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

#include <kernel/com/io/term.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/sched.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <stdatomic.h>
#include <vendor/tailq.h>

static _Atomic(com_term_t *) FallbackTerm = NULL;

static void flush_buffer_nolock(com_term_t *term) {
    if (0 == term->buffering.index) {
        return;
    }

    term->backend.ops->putsn(term->backend.data,
                             term->buffering.buffer,
                             term->buffering.index);
    term->buffering.index = 0;
    com_sys_sched_notify_all(&term->buffering.waiters);
}

static void refresh_term_nolock(com_term_t *term) {
    if (term->buffering.enabled) {
        flush_buffer_nolock(term);
    }

    term->backend.ops->flush(term->backend.data);
    term->backend.ops->refresh(term->backend.data);
}

static void flush_callout(com_callout_t *callout) {
    com_term_t *term = callout->arg;
    kspinlock_acquire(&term->lock);
    flush_buffer_nolock(term);
    bool should_resched = term->buffering.enabled && term->enabled;
    kspinlock_release(&term->lock);

    if (should_resched) {
        com_sys_callout_reschedule(callout, KFPS(CONFIG_TERM_FPS));
    }
}

com_term_t *com_io_term_new(com_term_backend_t backend) {
    com_term_t *ret        = com_mm_slab_alloc(sizeof(com_term_t));
    ret->backend           = backend;
    ret->lock              = KSPINLOCK_NEW();
    ret->enabled           = true;
    ret->buffering.enabled = false;
    TAILQ_INIT(&ret->buffering.waiters);

    ret->backend.data = ret->backend.ops->init();

    return ret;
}

com_term_t *com_io_term_clone(com_term_t *parent) {
    com_term_t *ret        = com_mm_slab_alloc(sizeof(com_term_t));
    ret->lock              = KSPINLOCK_NEW();
    ret->buffering.enabled = false;
    TAILQ_INIT(&ret->buffering.waiters);

    if (NULL != parent) {
        ret->enabled      = parent->enabled;
        ret->backend.ops  = parent->backend.ops;
        ret->backend.data = ret->backend.ops->init();
        kspinlock_acquire(&parent->lock);
        bool buffering = parent->buffering.enabled;
        kspinlock_release(&parent->lock);
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

    kspinlock_acquire(&term->lock);

    if (term->buffering.enabled) {
        while (term->buffering.index + n >= COM_IO_TERM_BUFSZ) {
            com_sys_sched_wait(&term->buffering.waiters, &term->lock);
        }
        kmemcpy(&term->buffering.buffer[term->buffering.index], s, n);
        term->buffering.index += n;
    } else {
        term->backend.ops->putsn(term->backend.data, s, n);
    }

    kspinlock_release(&term->lock);
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

    kspinlock_acquire(&term->lock);

    if (term->buffering.enabled) {
        flush_buffer_nolock(term);
    }

    term->backend.ops->flush(term->backend.data);
    kspinlock_release(&term->lock);
}

void com_io_term_set_buffering(com_term_t *term, bool state) {
    if (NULL == term) {
        term = atomic_load(&FallbackTerm);
    }

    if (NULL == term) {
        return;
    }

    kspinlock_acquire(&term->lock);

    if (state == term->buffering.enabled) {
        kspinlock_release(&term->lock);
        return;
    }

    if (term->buffering.enabled) {
        flush_buffer_nolock(term);
    }

    term->backend.ops->flush(term->backend.data);
    term->backend.ops->refresh(term->backend.data);
    term->buffering.enabled = state;
    kspinlock_release(&term->lock);

    if (state) {
        com_sys_callout_add(flush_callout, term, KFPS(CONFIG_TERM_FPS));
    }
}

void com_io_term_enable(com_term_t *term) {
    if (NULL == term) {
        return;
    }

    kspinlock_acquire(&term->lock);
    term->backend.ops->enable(term->backend.data);
    term->enabled = true;

    refresh_term_nolock(term);
    kspinlock_release(&term->lock);

    if (term->buffering.enabled) {
        com_sys_callout_add(flush_callout, term, KFPS(CONFIG_TERM_FPS));
    }
}

void com_io_term_disable(com_term_t *term) {
    if (NULL == term) {
        return;
    }

    kspinlock_acquire(&term->lock);
    term->enabled = false;
    term->backend.ops->disable(term->backend.data);
    kspinlock_release(&term->lock);
}

void com_io_term_refresh(com_term_t *term) {
    if (NULL == term) {
        term = atomic_load(&FallbackTerm);
    }

    if (NULL == term) {
        return;
    }

    kspinlock_acquire(&term->lock);
    refresh_term_nolock(term);
    kspinlock_release(&term->lock);
}

void com_io_term_set_fallback(com_term_t *fallback_term) {
    atomic_store(&FallbackTerm, fallback_term);
}

void com_io_term_panic_putsn(const char *s, size_t n) {
    static com_term_t *fallback = NULL;

    // fallback is static, so we need to initialize it once
    if (NULL == fallback) {
        fallback = atomic_load(&FallbackTerm);
        // It was null and now it isn't, so we got it
        if (NULL != fallback) {
            // We don't use the wrapper functions because those do more and we
            // don't need that in panic
            fallback->buffering.enabled = false;
            fallback->buffering.index   = 0;
            fallback->backend.ops->enable(fallback->backend.data);
            fallback->backend.ops->flush(fallback->backend.data);
            fallback->backend.ops->panic(fallback->backend.data);
        }
    }

    // And this is done every other time
    if (NULL != fallback) {
        fallback->backend.ops->putsn(fallback->backend.data, s, n);
    }
}

void com_io_term_panic(void) {
    static const char reset_sequence[] = "\033[0m\033[H\033[2J";
    com_io_term_panic_putsn(reset_sequence, sizeof(reset_sequence));
    // Best to have weird output than to have a deadlock in panic
    com_io_log_set_user_hook_nolock(com_io_term_panic_putsn);
}

void com_io_term_init(void) {
}
