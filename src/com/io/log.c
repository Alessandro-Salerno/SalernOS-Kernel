#include <arch/cpu.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/util.h>
#include <stdarg.h>

static Spinlock g_log_lock = SPINLOCK_NEW();

// TODO: reimplement vnode logging correctly (do not write to vnode directly,
// file systems should not be invoked under spinlocks)

static void dummy_hook(const char *s, usize n) {
    (void)s;
    (void)n;
}

static LogFn g_log_hook = dummy_hook;
static LogFn g_user_hook = dummy_hook;

void log_set_hook_nolock(LogFn hook) {
    if (hook == NULL) {
        g_log_hook = dummy_hook;
        return;
    }

    g_log_hook = hook;
}

void log_putc_nolock(char c) { log_putsn_nolock(&c, 1); }

void log_puts_nolock(const char *s) { log_putsn_nolock(s, strlen(s)); }

void log_putsn_nolock(const char *s, usize n) {
    g_log_hook(s, n);
    g_user_hook(s, n);
}

void log_lock(void) { spinlock_acq(&g_log_lock); }

void log_unlock(void) { spinlock_rel(&g_log_lock); }

void log_set_vnode_nolock(struct Vnode *vnode) { (void)vnode; }

void log_set_hook(LogFn hook) {
    log_lock();
    log_set_hook_nolock(hook);
    log_unlock();
}

void log_putc(char c) {
    log_lock();
    log_putc_nolock(c);
    log_unlock();
}

void log_puts(const char *s) {
    log_lock();
    log_puts_nolock(s);
    log_unlock();
}

void log_putsn(const char *s, usize n) {
    log_lock();
    log_putsn_nolock(s, n);
    log_unlock();
}

void log_set_vnode(struct Vnode *vnode) {
    log_lock();
    log_set_vnode_nolock(vnode);
    log_unlock();
}

void log_set_user_hook_nolock(LogFn hook) {
    if (hook == NULL) {
        g_user_hook = dummy_hook;
        return;
    }

    g_user_hook = hook;
}

void log_set_user_hook(LogFn hook) {
    log_lock();
    log_set_user_hook_nolock(hook);
    log_unlock();
}

void log_header_nolock(const char *category, const char *color) {
    struct Cpu *curr_cpu = cpu_get();
    struct Thread *curr_thread = cpu_get_thread();

#if CONFIG_LOG_ALLOW_COLORS
    log_reset_color_nolock();
    printf("%s[%s  ", color, category);
#else
    (void)color;
    printf("[%s  ", category);
#endif

    isize cat_len = strlen(category);
    isize diff = CONFIG_LOG_SEP_LEN - cat_len;
    for (isize i = 0; i < diff; i++) log_putc_nolock(' ');

    if (curr_cpu == NULL) {
        printf("**NOCPU**");
        goto log;
    }

    printf("cpu=%d/", curr_cpu->id);

    if (curr_thread == NULL) {
        printf("KERNEL");
        goto log;
    }

    if (curr_thread == curr_cpu->idle_thread) {
        printf("IDLE");
        goto log;
    }

    printf("tid=%d/", curr_thread->tid);

    if (curr_thread->proc == NULL) {
        printf("KERNEL");
        goto log;
    }

    printf("pid=%d (%s)", curr_thread->proc->pid,
           curr_thread->proc->executable_path);
log:
    printf("]\t");
}

void log_line(const char *category, const char *color, bool newline,
              const char *file, const char *func, int line, const char *fmt,
              ...) {
    log_lock();
    log_header_nolock(category, color);
    if (file != NULL) printf("%s:%s:%d: ", file, func, line);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    log_reset_color_nolock();
    if (newline) log_putc_nolock('\n');
    log_unlock();
}

void assert_fail(const char *expr, const char *file, const char *func,
                 int line) {
    log_lock();
#if CONFIG_ASSERT_ACTION == CONST_ASSERT_PANIC
    log_reset_color_nolock();
#endif
    log_header_nolock("ASSERT", "");
    printf("%s:%s:%d: %s failed\n", file, func, line, expr);
#if CONFIG_ASSERT_ACTION == CONST_ASSERT_PANIC
    panic(NULL, "assertion failed: %s:%s:%d: %s", file, func, line, expr);
#else
    log_unlock();
#endif
}
