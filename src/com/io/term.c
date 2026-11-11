#include <kernel/com/crt/string.h>
#include <kernel/com/io/term.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <stdatomic.h>
#include <vendor/tailq.h>

static _Atomic(Term *) g_fallback_term = NULL;

static void flush_buffer_nolock(Term *term) {
    if (term->buffering.index == 0) return;

    term->backend.ops->putsn(term->backend.data, term->buffering.buffer,
                             term->buffering.index);
    term->buffering.index = 0;
    sched_notify_all(&term->buffering.waiters);
}

static void refresh_term_nolock(Term *term) {
    if (term->buffering.enabled) flush_buffer_nolock(term);

    term->backend.ops->flush(term->backend.data);
    term->backend.ops->refresh(term->backend.data);
}

static void flush_callout(Callout *callout) {
    Term *term = callout->arg;
    spinlock_acq(&term->lock);
    flush_buffer_nolock(term);
    bool should_resched = term->buffering.enabled && term->enabled;
    spinlock_rel(&term->lock);

    if (should_resched) callout_reschedule(callout, fps_to_ns(CONFIG_TERM_FPS));
}

Term *term_new(TermBackend backend) {
    Term *ret = slab_alloc(sizeof(Term));
    ret->backend = backend;
    ret->lock = SPINLOCK_NEW();
    ret->enabled = true;
    ret->buffering.enabled = false;
    waitlist_init(&ret->buffering.waiters);

    ret->backend.data = ret->backend.ops->init();

    return ret;
}

Term *term_clone(Term *parent) {
    Term *ret = slab_alloc(sizeof(Term));
    ret->lock = SPINLOCK_NEW();
    ret->buffering.enabled = false;
    waitlist_init(&ret->buffering.waiters);

    if (parent != NULL) {
        ret->enabled = parent->enabled;
        ret->backend.ops = parent->backend.ops;
        ret->backend.data = ret->backend.ops->init();
        spinlock_acq(&parent->lock);
        bool buffering = parent->buffering.enabled;
        spinlock_rel(&parent->lock);
        term_set_buffering(ret, buffering);
    }

    return ret;
}

void term_putc(Term *term, char c) { term_putsn(term, &c, 1); }

void term_puts(Term *term, const char *s) { term_putsn(term, s, strlen(s)); }

void term_putsn(Term *term, const char *s, usize n) {
    if (term == NULL) term = atomic_load(&g_fallback_term);
    if (term == NULL) return;

    spinlock_acq(&term->lock);

    if (term->buffering.enabled) {
        while (term->buffering.index + n >= TERM_BUFSZ) {
            spinlock_wait(&term->lock, &term->buffering.waiters, 0, false);
        }
        memcpy(&term->buffering.buffer[term->buffering.index], s, n);
        term->buffering.index += n;
    } else {
        term->backend.ops->putsn(term->backend.data, s, n);
    }

    spinlock_rel(&term->lock);
}

void term_get_size(Term *term, usize *out_rows, usize *out_cols) {
    if (term == NULL) term = atomic_load(&g_fallback_term);

    if (term == NULL) {
        *out_rows = 0;
        *out_cols = 0;
        return;
    }

    term->backend.ops->get_size(term->backend.data, out_rows, out_cols);
}

void term_set_size(Term *term, usize rows, usize cols) {
    if (term == NULL) term = atomic_load(&g_fallback_term);
    if (term == NULL) return;

    term->backend.ops->set_size(term->backend.data, rows, cols);
}

void term_flush(Term *term) {
    if (term == NULL) term = atomic_load(&g_fallback_term);
    if (term == NULL) return;

    spinlock_acq(&term->lock);

    if (term->buffering.enabled) flush_buffer_nolock(term);

    term->backend.ops->flush(term->backend.data);
    spinlock_rel(&term->lock);
}

void term_set_buffering(Term *term, bool state) {
    if (term == NULL) term = atomic_load(&g_fallback_term);
    if (term == NULL) return;

    spinlock_acq(&term->lock);

    if (state == term->buffering.enabled) {
        spinlock_rel(&term->lock);
        return;
    }

    if (term->buffering.enabled) flush_buffer_nolock(term);

    term->backend.ops->flush(term->backend.data);
    term->backend.ops->refresh(term->backend.data);
    term->buffering.enabled = state;
    spinlock_rel(&term->lock);

    if (state) callout_add(flush_callout, term, fps_to_ns(CONFIG_TERM_FPS));
}

void term_enable(Term *term) {
    if (term == NULL) return;

    spinlock_acq(&term->lock);
    term->backend.ops->enable(term->backend.data);
    term->enabled = true;

    refresh_term_nolock(term);
    spinlock_rel(&term->lock);

    if (term->buffering.enabled) {
        callout_add(flush_callout, term, fps_to_ns(CONFIG_TERM_FPS));
    }
}

void term_disable(Term *term) {
    if (term == NULL) return;

    spinlock_acq(&term->lock);
    term->enabled = false;
    term->backend.ops->disable(term->backend.data);
    spinlock_rel(&term->lock);
}

void term_refresh(Term *term) {
    if (term == NULL) term = atomic_load(&g_fallback_term);
    if (term == NULL) return;

    spinlock_acq(&term->lock);
    refresh_term_nolock(term);
    spinlock_rel(&term->lock);
}

void term_set_fallback(Term *fallback_term) {
    atomic_store(&g_fallback_term, fallback_term);
}

void term_panic_putsn(const char *s, usize n) {
    static Term *fallback = NULL;

    // fallback is static, so we need to initialize it once
    if (fallback == NULL) {
        fallback = atomic_load(&g_fallback_term);
        // It was null and now it isn't, so we got it
        if (fallback != NULL) {
            // We don't use the wrapper functions because those do more and we
            // don't need that in panic
            fallback->buffering.enabled = false;
            fallback->buffering.index = 0;
            fallback->backend.ops->enable(fallback->backend.data);
            fallback->backend.ops->flush(fallback->backend.data);
            fallback->backend.ops->panic(fallback->backend.data);
        }
    }

    // And this is done every other time
    if (fallback != NULL) {
        fallback->backend.ops->putsn(fallback->backend.data, s, n);
    }
}

void term_panic(void) {
    static const char reset_sequence[] = "\033[0m\033[H\033[2J";
    term_panic_putsn(reset_sequence, sizeof(reset_sequence));
    // Best to have weird output than to have a deadlock in panic
    log_set_user_hook_nolock(term_panic_putsn);
}

void term_init(void) {}
