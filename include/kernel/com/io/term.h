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

#include <arch/info.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COM_IO_TERM_BUFSZ (ARCH_PAGE_SIZE / 2)

typedef struct com_term         com_term_t;
typedef struct com_term_backend com_term_backend_t;

typedef struct com_term_backend_ops {
    void *(*init)(void);
    void (*putsn)(void *termdata, const char *s, size_t n);
    void (*get_size)(void *termdata, size_t *rows, size_t *cols);
    void (*set_size)(void *termdata, size_t rows, size_t cols);
    void (*flush)(void *termdata);
    void (*enable)(void *termdata);
    void (*disable)(void *termdata);
} com_term_backend_ops_t;

typedef struct com_term_backend {
    com_term_backend_ops_t *ops;
    void                   *data;
} com_term_backend_t;

typedef struct com_term {
    com_term_backend_t backend;
    com_spinlock_t     lock;
    struct {
        bool                    enabled;
        char                    buffer[COM_IO_TERM_BUFSZ];
        size_t                  index;
        struct com_thread_tailq waiters;
        struct {
            TAILQ_ENTRY(com_term) queue;
        } internal;
    } buffering;
} com_term_t;

com_term_t *com_io_term_new(com_term_backend_t backend);
com_term_t *com_io_term_clone(com_term_t *parent);
void        com_io_term_putc(com_term_t *term, char c);
void        com_io_term_puts(com_term_t *term, const char *s);
void        com_io_term_putsn(com_term_t *term, const char *s, size_t n);
void        com_io_term_get_size(com_term_t *term, size_t *rows, size_t *cols);
void        com_io_term_set_size(com_term_t *term, size_t rows, size_t cols);
void        com_io_term_flush(com_term_t *term);
void        com_io_term_set_buffering(com_term_t *term, bool state);
void        com_io_term_enable(com_term_t *term);
void        com_io_term_disable(com_term_t *term);
void        com_io_term_set_fallback(com_term_t *fallback_term);
void        com_io_term_init(void);
