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

#include <kernel/com/io/log.h>
#include <kernel/com/spinlock.h>

static com_spinlock_t LogLock    = COM_SPINLOCK_NEW();
static bool           InPanic    = false;
static bool           isPrinting = false;

static void dummy_hook(char c) {
    (void)c;
}

static volatile com_io_log_hook_t Hook = dummy_hook;

void com_io_log_set_hook(com_io_log_hook_t hook) {
    if (NULL == hook) {
        Hook = dummy_hook;
        return;
    }

    Hook = hook;
}

void com_io_log_putc(char c) {
    Hook(c);
}

void com_io_log_puts(const char *s) {
    isPrinting = true;
    for (; 0 != *s; s++) {
        com_io_log_putc(*s);
    }
    isPrinting = false;
}

void com_io_log_acquire(void) {
    while (InPanic)
        ;
    com_spinlock_acquire(&LogLock);
}

void com_io_log_release(void) {
    while (InPanic)
        ;
    com_spinlock_release(&LogLock);
}

void com_io_log_panic(void) {
    InPanic = true;
}
