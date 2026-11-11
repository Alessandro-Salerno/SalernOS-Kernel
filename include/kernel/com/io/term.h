#pragma once

#include <arch/info.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>

#define TERM_BUFSZ (PAGE_SIZE / 2)

typedef struct Term Term;
typedef struct TermBackend TermBackend;

typedef struct TermBackendOps {
    void *(*init)(void);
    void (*putsn)(void *termdata, const char *s, usize n);
    void (*get_size)(void *termdata, usize *rows, usize *cols);
    void (*set_size)(void *termdata, usize rows, usize cols);
    void (*flush)(void *termdata);
    void (*enable)(void *termdata);
    void (*disable)(void *termdata);
    void (*refresh)(void *termdata);
    void (*panic)(void *termdata);
} TermBackendOps;

typedef struct TermBackend {
    TermBackendOps *ops;
    void *data;
} TermBackend;

typedef struct Term {
    TermBackend backend;
    Spinlock lock;
    bool enabled;
    struct {
        bool enabled;
        char buffer[TERM_BUFSZ];
        usize index;
        Waitlist waiters;
    } buffering;
} Term;

Term *term_new(TermBackend backend);
Term *term_clone(Term *parent);
void term_putc(Term *term, char c);
void term_puts(Term *term, const char *s);
void term_putsn(Term *term, const char *s, usize n);
void term_get_size(Term *term, usize *out_rows, usize *out_cols);
void term_set_size(Term *term, usize rows, usize cols);
void term_flush(Term *term);
void term_set_buffering(Term *term, bool state);
void term_enable(Term *term);
void term_disable(Term *term);
void term_refresh(Term *term);
void term_set_fallback(Term *fallback_term);
void term_panic_putsn(const char *s, usize n);
void term_panic(void);
void term_init(void);
