/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#define _GNU_SOURCE
#define _BSD_SOURCE

#include <arch/cpu.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/fbterm.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/log.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/ctype.h>
#include <lib/mem.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/termios.h>
#include <termios.h>
#include <vendor/tailq.h>

#define CONTROL_DEL 127

struct tty {
  size_t                  rows;
  size_t                  cols;
  struct termios          termios;
  com_spinlock_t          lock;
  struct com_thread_tailq waitlist;
  uint8_t                 buf[2048];
  size_t                  write;
};

static com_vnode_t *TtyDev = NULL;
static struct tty   Tty    = {0};

// DEV OPS

static int tty_read(void     *buf,
                    size_t    buflen,
                    size_t   *bytes_read,
                    void     *devdata,
                    uintmax_t off,
                    uintmax_t flags) {
  (void)off;
  (void)flags;
  struct tty   *tty        = devdata;
  size_t        read_count = 0;
  com_thread_t *cur_thread = hdr_arch_cpu_get()->thread;
  hdr_com_spinlock_acquire(&tty->lock);

  while (true) {
    bool can_read  = false;
    char eol       = tty->termios.c_cc[VEOL];
    char eof       = tty->termios.c_cc[VEOF];
    bool eof_found = false;

    if (0 == eol) {
      eol = '\n';
    }

    size_t i = 0;
    for (; i < tty->write; i++) {
      const char c = tty->buf[i];

      // When using canonical mode, input is line-buffered. Thus, we only allow
      // reading when EOL or EOF are reached
      if ((ICANON & tty->termios.c_lflag) && (c == eol || c == eof)) {
        if (c == eol) {
          i++;
        }

        eof_found = c == eof;
        can_read  = true;
        break;
      }

      // When operating in raw mode, input is not line-buffered. Thus we stop
      // reading when we reach the amount of characters requested by the callee
      else if (!(ICANON & tty->termios.c_lflag) && i + 1 == buflen) {
        i++;
        can_read = true;
        break;
      }
    }

    if (!can_read) {
      com_sys_sched_wait(cur_thread, &tty->lock);
      continue;
    }

    kmemcpy((uint8_t *)buf + read_count, tty->buf, i);
    kmemmove(tty->buf, (uint8_t *)tty->buf + i, tty->write - i);
    tty->write -= i;

    if (eof_found) {
      tty->write--;
    }

    read_count += i;
    break;
  }

  hdr_com_spinlock_release(&tty->lock);
  *bytes_read = read_count;
  return 0;
}

static int tty_write(size_t   *bytes_written,
                     void     *devdata,
                     void     *buf,
                     size_t    buflen,
                     uintmax_t off,
                     uintmax_t flags) {
  (void)devdata;
  (void)off;
  (void)flags;
  com_io_fbterm_putsn(buf, buflen);
  *bytes_written = buflen;
  return 0;
}

// TODO: implement this
static int tty_ioctl(void *devdata, uintmax_t op, void *buf) {
  (void)devdata;
  (void)op;
  (void)buf;
  return 0;
}

static com_dev_ops_t TtyDevOps = {.read  = tty_read,
                                  .write = tty_write,
                                  .ioctl = tty_ioctl};

void com_io_tty_kbd_in(char c, uintmax_t mod) {
  bool is_arrow      = COM_IO_TTY_MOD_ARROW & mod;
  bool is_del        = false;
  bool is_shift_held = (COM_IO_TTY_MOD_LSHIFT | COM_IO_TTY_MOD_RSHIFT) & mod;
  bool is_ctrl_held  = COM_IO_TTY_MOD_LCTRL & mod;
  bool handled       = false;
  bool notify        = true;
  struct tty *tty    = &Tty;

  if (CONTROL_DEL == c) {
    is_del = true;
    goto end;
  }

  if ('\b' == c) {
    c = CONTROL_DEL;
  }

  if (is_ctrl_held) {
    c -= 64;
    if (KISLOWER(c)) {
      c -= 32;
    }
  }

  hdr_com_spinlock_acquire(&tty->lock);

  if (!is_arrow) {
    // TODO: implement POSIX signals

    if (('\r' == c) && !(IGNCR & tty->termios.c_iflag) &&
        (ICRNL & tty->termios.c_iflag)) {
      c = '\n';
    } else if (('\n' == c) && (INLCR & tty->termios.c_iflag)) {
      c = '\r';
    }

    if (!(ICANON & tty->termios.c_lflag)) {
      goto end;
    }

    if (tty->termios.c_cc[VEOF] == c) {
      tty->buf[tty->write] = c;
      tty->write++;
      handled = true;
    }

    if (tty->termios.c_cc[VERASE] == c) {
      if (ECHOE & tty->termios.c_lflag) {
        com_io_fbterm_puts("\b \b");
      }

      if (tty->write > 0) {
        tty->write--;
      }
    }

    // TODO: figure out what to do with VKILL (another thing I think I'll never
    // get to use cause I'm not a TTY pro)
  }

end:
  if (is_arrow || is_del) {
    char   escape[4] = "\e[";
    size_t len       = 2;

    if (is_arrow) {
      escape[2] = c;
      len++;
    } else {
      escape[2] = '3';
      escape[3] = '~';
      len += 2;
    }

    kmemcpy((uint8_t *)tty->buf + tty->write, escape, len);
    tty->write += len;

    if (ECHO & tty->termios.c_lflag) {
      com_io_fbterm_putsn(escape, len);
      handled = true;
    }
  } else if (!handled) {
    tty->buf[tty->write] = c;
    tty->write++;
  }

  if ((ECHO & tty->termios.c_lflag) && !handled) {
    com_io_fbterm_putc(c);
  }

  if (notify && !TAILQ_EMPTY(&tty->waitlist)) {
    com_sys_sched_notify(&tty->waitlist);
  }

  hdr_com_spinlock_release(&tty->lock);
}

int com_io_tty_init(com_vnode_t **out) {
  LOG("initializing kernel tty");
  TAILQ_INIT(&Tty.waitlist);
  int ret = com_fs_devfs_register(&TtyDev, NULL, "tty0", 4, &TtyDevOps, &Tty);

  if (0 != ret) {
    if (NULL != out) {
      *out = NULL;
    }
    return ret;
  }

  Tty.termios.c_cc[VINTR]    = CINTR;
  Tty.termios.c_cc[VQUIT]    = CQUIT;
  Tty.termios.c_cc[VERASE]   = CERASE;
  Tty.termios.c_cc[VKILL]    = CKILL;
  Tty.termios.c_cc[VEOF]     = CEOF;
  Tty.termios.c_cc[VTIME]    = CTIME;
  Tty.termios.c_cc[VMIN]     = CMIN;
  Tty.termios.c_cc[VSWTC]    = 0;
  Tty.termios.c_cc[VSTART]   = CSTART;
  Tty.termios.c_cc[VSTOP]    = CSTOP;
  Tty.termios.c_cc[VSUSP]    = CSUSP;
  Tty.termios.c_cc[VEOL]     = CEOL;
  Tty.termios.c_cc[VREPRINT] = CREPRINT;
  Tty.termios.c_cc[VDISCARD] = 0;
  Tty.termios.c_cc[VWERASE]  = CWERASE;
  Tty.termios.c_cc[VLNEXT]   = 0;
  Tty.termios.c_cflag        = TTYDEF_CFLAG;
  Tty.termios.c_iflag        = TTYDEF_IFLAG;
  Tty.termios.c_lflag        = TTYDEF_LFLAG;
  Tty.termios.c_oflag        = TTYDEF_OFLAG;
  // Tty.termios.ibaud = Tty.termios.obaud = TTYDEF_SPEED;

  if (NULL != out) {
    *out = TtyDev;
  }

  return ret;
}
