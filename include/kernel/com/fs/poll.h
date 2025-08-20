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

#pragma once

#include <kernel/com/fs/file.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <vendor/tailq.h>

typedef struct com_poller {
    struct com_thread_tailq waiters;
    com_spinlock_t          lock;
} com_poller_t;

typedef struct com_polled {
    com_poller_t *poller;
    com_file_t   *file;
    LIST_ENTRY(com_polled) polled_list;
} com_polled_t;

typedef struct com_poll_head {
    LIST_HEAD(, com_polled) polled_list;
    com_spinlock_t lock;
} com_poll_head_t;
