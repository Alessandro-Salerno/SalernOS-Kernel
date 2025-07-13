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

#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/io/fbterm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/context.h>
#include <lib/mem.h>
#include <lib/str.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/flanterm/backends/fb.h>
#include <vendor/flanterm/flanterm.h>

#define BUFFER_LEN 2048

static struct flanterm_context *FlantermContext = NULL;
static bool                     Buffered        = false;
static com_spinlock_t           BufferLock      = COM_SPINLOCK_NEW();
static char                     Buffer[BUFFER_LEN];
static size_t                   BufferNext = 0;
static struct com_thread_tailq  BufferWaiters;
static com_thread_t            *PrintThread = NULL;
static size_t                   Ticks       = 0;

static void print_buffer(void) {
    while (true) {
        if (10 != Ticks) {
            Ticks++;
            goto next_time;
        }
        com_spinlock_acquire(&BufferLock);
        if (0 == BufferNext) {
            goto skip;
        }
        flanterm_write(FlantermContext, Buffer, BufferNext);
        BufferNext = 0;
        com_sys_sched_notify_all(&BufferWaiters);
    skip:
        com_spinlock_release(&BufferLock);
        Ticks = 0;
        // TODO: why does this not preempt?
    next_time:
        com_sys_sched_yield();
    }
}

void com_io_fbterm_putc(char c) {
    flanterm_write(FlantermContext, &c, 1);
}

void com_io_fbterm_puts(const char *s) {
    com_io_fbterm_putsn(s, kstrlen(s));
}

void com_io_fbterm_putsn(const char *s, size_t n) {
    if (!Buffered) {
        flanterm_write(FlantermContext, s, n);
        return;
    }

    com_spinlock_acquire(&BufferLock);
    while (BufferNext + n >= BUFFER_LEN) {
        com_sys_sched_wait(&BufferWaiters, &BufferLock);
    }
    kmemcpy(&Buffer[BufferNext], s, n);
    BufferNext += n;
    com_spinlock_release(&BufferLock);
}

void com_io_fbterm_get_size(size_t *rows, size_t *cols) {
    if (NULL != rows) {
        *rows = FlantermContext->rows;
    }

    if (NULL != cols) {
        *cols = FlantermContext->cols;
    }
}

void com_io_fbterm_init_buffering(void) {
    KLOG("initializing fbterm buffering");
    TAILQ_INIT(&BufferWaiters);
    PrintThread = com_sys_thread_new_kernel(NULL, print_buffer);
    // com_sys_thread_ready(PrintThread);
    // com_spinlock_acquire(&ARCH_CPU_GET()->runqueue_lock);
    PrintThread->runnable = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, PrintThread, threads);
    // com_spinlock_release(&ARCH_CPU_GET()->runqueue_lock);
}

void com_io_fbterm_set_buffering(bool buffering) {
    KDEBUG("setting fbterm buffering to %u", buffering);
    Buffered = buffering;
}

void com_io_fbterm_init(arch_framebuffer_t *fb) {
    uint32_t default_fg = 0xffffffff;
    FlantermContext     = flanterm_fb_init(NULL,
                                       NULL,
                                       fb->address,
                                       fb->width,
                                       fb->height,
                                       fb->pitch,
                                       fb->red_mask_size,
                                       fb->red_mask_shift,
                                       fb->green_mask_size,
                                       fb->green_mask_shift,
                                       fb->blue_mask_size,
                                       fb->blue_mask_shift,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &default_fg,
                                       NULL,
                                       NULL,
                                       NULL,
                                       0,
                                       0,
                                       1,
                                       0,
                                       0,
                                       0);
}
