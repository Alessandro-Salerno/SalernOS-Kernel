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

#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <lib/spinlock.h>
#include <lib/str.h>

static kspinlock_t LogLock = KSPINLOCK_NEW();

static struct {
    com_vnode_t *vnode;
    size_t       off;
} SecondaryLog;

static void dummy_hook(const char *s, size_t n) {
    (void)s;
    (void)n;
}

static com_intf_log_t LogHook  = dummy_hook;
static com_intf_log_t UserHook = dummy_hook;

void com_io_log_set_hook_nolock(com_intf_log_t hook) {
    if (NULL == hook) {
        LogHook = dummy_hook;
        return;
    }

    LogHook = hook;
}

void com_io_log_putc_nolock(char c) {
    com_io_log_putsn_nolock(&c, 1);
}

void com_io_log_puts_nolock(const char *s) {
    com_io_log_putsn_nolock(s, kstrlen(s));
}

void com_io_log_putsn_nolock(const char *s, size_t n) {
    LogHook(s, n);
    UserHook(s, n);

    if (NULL != SecondaryLog.vnode) {
        com_fs_vfs_write(NULL,
                         SecondaryLog.vnode,
                         (void *)s,
                         n,
                         SecondaryLog.off,
                         0);
        SecondaryLog.off += n;
    }
}

void com_io_log_lock(void) {
    kspinlock_acquire(&LogLock);
}

void com_io_log_unlock(void) {
    kspinlock_release(&LogLock);
}

void com_io_log_set_vnode_nolock(struct com_vnode *vnode) {
    SecondaryLog.vnode = vnode;
}

void com_io_log_set_hook(com_intf_log_t hook) {
    com_io_log_lock();
    com_io_log_set_hook_nolock(hook);
    com_io_log_unlock();
}

void com_io_log_putc(char c) {
    com_io_log_lock();
    com_io_log_putc_nolock(c);
    com_io_log_unlock();
}

void com_io_log_puts(const char *s) {
    com_io_log_lock();
    com_io_log_puts_nolock(s);
    com_io_log_unlock();
}

void com_io_log_putsn(const char *s, size_t n) {
    com_io_log_lock();
    com_io_log_putsn_nolock(s, n);
    com_io_log_unlock();
}

void com_io_log_set_vnode(struct com_vnode *vnode) {
    com_io_log_lock();
    com_io_log_set_vnode_nolock(vnode);
    com_io_log_unlock();
}

void com_io_log_set_user_hook_nolock(com_intf_log_t hook) {
    if (NULL == hook) {
        UserHook = dummy_hook;
        return;
    }

    UserHook = hook;
}

void com_io_log_set_user_hook(com_intf_log_t hook) {
    com_io_log_lock();
    com_io_log_set_user_hook_nolock(hook);
    com_io_log_unlock();
}
