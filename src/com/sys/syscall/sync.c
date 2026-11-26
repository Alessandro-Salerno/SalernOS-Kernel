#include <kernel/com/mm/buffcache.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: sync(void)
SYSCALL(syscall_sync) {
    SYSCALL_UNUSED_START(0);

    int ret = buffcache_sync();
    if (ret == 0) return SYSCALL_OK(0);
    else return SYSCALL_ERR(ret);
}
