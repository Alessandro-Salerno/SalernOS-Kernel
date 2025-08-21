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
#include <lib/printf.h>
#include <lib/str.h>
#include <stdarg.h>
#include <stdint.h>

void kvprintf(const char *fmt, va_list args) {
    for (char *ptr = (char *)(fmt); 0 != *ptr; ptr++) {
        char buf[24] = {0};
        switch (*ptr) {
            case '%': {
                switch (*(++ptr)) {
                    case 'u':
                        com_io_log_puts((
                            const char *)(kuitoa(va_arg(args, uint64_t), buf)));
                        break;

                    case 'p':
                    case 'x':
                        com_io_log_puts("0x");
                        com_io_log_puts(kxuitoa(va_arg(args, uint64_t), buf));
                        break;

                    case 'd':
                    case 'i':
                        com_io_log_puts(
                            (const char *)(kitoa(va_arg(args, int), buf)));
                        break;

                    case 'c':
                        com_io_log_putc((char)(va_arg(args, signed)));
                        break;

                    case 's':
                        com_io_log_puts((const char *)(va_arg(args, char *)));
                        break;

                    case '.': {
                        if ('*' == ptr[1] && 's' == ptr[2]) {
                            int         len = va_arg(args, int);
                            const char *s   = va_arg(args, const char *);
                            for (int i = 0; i < len; i++) {
                                com_io_log_putc(s[i]);
                            }
                            ptr += 2;
                        }
                    }
                }

                break;
            }

            default:
                com_io_log_putc(*ptr);
                break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
