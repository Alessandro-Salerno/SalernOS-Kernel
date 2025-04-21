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
#include <stdarg.h>
#include <stdint.h>

static const char *uitoa(uint64_t val, char *s) {
    uint64_t size_test = val;
    uint8_t  size      = 0;
    uint8_t  idx       = 0;

    while (size_test >= 10) {
        size_test /= 10;
        size++;
    }

    while (idx < size) {
        uint8_t rem = val % 10;
        val /= 10;
        s[size - idx] = rem + '0';
        idx++;
    }

    uint8_t rem = val % 10;
    val /= 10;
    s[size - idx] = rem + '0';
    s[size + 1]   = 0;

    return s;
}

static const char *itoa(int64_t val, char *s) {
    uint64_t size_test   = 10;
    uint8_t  size        = 0;
    uint8_t  idx         = 0;
    uint8_t  is_negative = 0;

    is_negative = val < 0;
    s[0]        = (val < 0) ? '-' : '+';

    if (val < 0) {
        val *= -1;
    }

    while (val / size_test > 0) {
        size_test *= 10;
        size++;
    }

    while (idx < size) {
        uint8_t rem = val % 10;
        val /= 10;
        s[is_negative + size - idx] = rem + '0';
        idx++;
    }

    uint8_t rem = val % 10;
    val /= 10;
    s[is_negative + size - idx] = rem + '0';
    s[is_negative + size + 1]   = 0;

    return s;
}

static const char *xuitoa(uint64_t val, char *s) {
    uint64_t size_test = val;
    uint8_t  size      = 0;
    uint8_t  idx       = 0;

    while (size_test >= 16) {
        size_test /= 16;
        size++;
    }

    uint64_t diff = 16 - size;

    for (uint64_t i = 0; i < diff; i++) {
        s[i] = '0';
    }

    while (idx < size) {
        uint8_t rem = val % 16;
        val /= 16;
        if (rem < 10) {
            s[15 - idx] = rem + '0';
        } else {
            s[15 - idx] = rem - 10 + 'A';
        }
        idx++;
    }

    uint8_t rem = val % 16;
    val /= 16;
    if (rem < 10) {
        s[15 - idx] = rem + '0';
    } else {
        s[15 - idx] = rem - 10 + 'A';
    }
    s[16] = 0;

    return s;
}

void kvprintf(const char *fmt, va_list args) {
    for (char *ptr = (char *)(fmt); 0 != *ptr; ptr++) {
        char buf[24] = {0};
        switch (*ptr) {
        case '%': {
            switch (*(++ptr)) {
            case 'u':
                com_io_log_puts(
                    (const char *)(uitoa(va_arg(args, uint64_t), buf)));
                break;

            case 'p':
            case 'x':
                com_io_log_puts("0x");
                com_io_log_puts(xuitoa(va_arg(args, uint64_t), buf));
                break;

            case 'd':
            case 'i':
                com_io_log_puts(
                    (const char *)(itoa(va_arg(args, int64_t), buf)));
                break;

            case 'c':
                com_io_log_putc((char)(va_arg(args, signed)));
                break;

            case 's':
                com_io_log_puts((const char *)(va_arg(args, char *)));
                break;
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
