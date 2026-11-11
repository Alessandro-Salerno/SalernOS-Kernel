#pragma once

#include <kernel/com/util.h>
#include <vendor/printf.h>

#if CONFIG_LOG_LOCATION == CONST_LOG_LOCATION_ALL
#define LOG_LOCATION __FILE__, __func__, __LINE__
#else
#define LOG_LOCATION NULL, NULL, 0
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_URGENT
#define log_urgent(...) \
    log_line("URGENT", "\033[35m", true, LOG_LOCATION, __VA_ARGS__)
#else
#define log_urgent(...) ((void)0)
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_INFO
#define log(...) log_line("INFO", "\033[32m", true, LOG_LOCATION, __VA_ARGS__)
#else
#define log(...) ((void)0)
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_OPTION
#define log_opt(optname, ...) \
    log_line(optname, "\033[94m", false, LOG_LOCATION, __VA_ARGS__)
#else
#define log_opt(...) ((void)0)
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_DEBUG
#define log_debug(...) log_line("DEBUG", "", true, LOG_LOCATION, __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif

struct Vnode;
typedef void (*LogFn)(const char *s, usize n);

void log_set_hook_nolock(LogFn hook);
void log_putc_nolock(char c);
void log_puts_nolock(const char *s);
void log_putsn_nolock(const char *s, usize n);
void log_set_vnode_nolock(struct Vnode *vnode);
void log_set_user_hook_nolock(LogFn hook);

void log_lock(void);
void log_unlock(void);

void log_set_hook(LogFn hook);
void log_putc(char c);
void log_puts(const char *s);
void log_putsn(const char *s, usize n);
void log_set_vnode(struct Vnode *vnode);
void log_set_user_hook(LogFn hook);

void log_header_nolock(const char *category, const char *color);
void log_line(const char *category, const char *color, bool newline,
              const char *file, const char *func, int line, const char *fmt,
              ...) __attribute__((format(printf, 7, 8)));

static inline void log_reset_color_nolock(void) {
#if CONFIG_LOG_ALLOW_COLORS
    log_puts_nolock("\033[0m");
#endif
}
