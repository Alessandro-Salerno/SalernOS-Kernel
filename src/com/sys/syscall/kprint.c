#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

#include <salernos/syscall_str.h>

// SYSCALL: kprint(const struct syscall_str *message)
SYSCALL(syscall_kprint) {
#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_USER
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    const struct syscall_str *umessage = SYSCALL_ARG(void *, 1);

    struct syscall_str message;
    int ret = memcpy_from_user(&message, umessage, sizeof(message));
    if (ret != 0) return SYSCALL_ERR(ret);

    char chunk[128];
    usize done = 0;
    do {
        usize n = min(message.len - done, sizeof(chunk));
        ret = memcpy_from_user(chunk, message.str + done, n);
        if (ret != 0) return SYSCALL_ERR(ret);

        log_lock();
        if (done == 0) log_header_nolock("USER", "\033[36m");
        log_putsn_nolock(chunk, n);
        done += n;
        if (done == message.len) log_putc_nolock('\n');
        log_unlock();
    } while (done < message.len);
#else
    SYSCALL_UNUSED_START(0);
#endif

    return SYSCALL_OK(0);
}
