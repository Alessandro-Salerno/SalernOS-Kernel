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

#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <lib/mem.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

// TODO: turn this into something userspace can control
#define PIPE_BUF_SZ ARCH_PAGE_SIZE

struct pipefs_node {
  uint8_t                *buf;
  size_t                  read;
  size_t                  write;
  com_spinlock_t          lock;
  struct com_thread_tailq readers;
  struct com_thread_tailq writers;
  com_vnode_t            *read_end;
  com_vnode_t            *write_end;
};

static com_vfs_ops_t   PipefsOps;
static com_vnode_ops_t PipefsNodeOps;

int com_fs_pipefs_read(void        *buf,
                       size_t       buflen,
                       size_t      *bytes_read,
                       com_vnode_t *node,
                       uintmax_t    off,
                       uintmax_t    flags) {
  (void)off;
  (void)flags;

  struct pipefs_node *pipe = node->extra;
  hdr_com_spinlock_acquire(&pipe->lock);

  size_t read_count = 0;

  for (size_t left = buflen; 0 < left;) {
    while (pipe->write == pipe->read && NULL != pipe->write_end) {
      // TODO: wait
    }

    size_t diff   = pipe->write - pipe->read;
    size_t readsz = left;
    size_t modidx = pipe->read % PIPE_BUF_SZ;
    size_t end    = PIPE_BUF_SZ - modidx;

    if (left > diff) {
      readsz = diff;
    }

    kmemcpy(buf, (uint8_t *)pipe->buf + modidx, MIN(readsz, end));

    if (readsz > end) {
      kmemcpy((uint8_t *)buf + end, pipe->buf, readsz - end);
    }

    pipe->read += readsz;
    buf = (uint8_t *)buf + readsz;
    left -= readsz;
    read_count += readsz;

    // If the write end clsoed while we were reading
    if (NULL == pipe->write_end) {
      break;
    }

    // TODO: notify writers
  }

  hdr_com_spinlock_release(&pipe->lock);
  *bytes_read = read_count;
  return 0;
}

int com_fs_pipefs_write(size_t      *bytes_written,
                        com_vnode_t *node,
                        void        *buf,
                        size_t       buflen,
                        uintmax_t    off,
                        uintmax_t    flags) {
  (void)off;
  (void)flags;
}

void com_fs_pipefs_new(com_vnode_t **read, com_vnode_t **write) {
  struct pipefs_node *pipe = com_mm_slab_alloc(sizeof(struct pipefs_node));
  TAILQ_INIT(&pipe->readers);
  TAILQ_INIT(&pipe->writers);
  pipe->read  = 0;
  pipe->write = 0;
  pipe->lock  = COM_SPINLOCK_NEW();
  pipe->buf   = (uint8_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());

  com_vnode_t *r  = com_mm_slab_alloc(sizeof(com_vnode_t));
  r->extra        = pipe;
  r->mountpointof = NULL;
  r->ops          = &PipefsNodeOps;
  r->vfs          = NULL; // TODO: vlink stuff
  r->num_ref      = 1;
  pipe->read_end  = r;

  com_vnode_t *w  = com_mm_slab_alloc(sizeof(com_vnode_t));
  w->extra        = pipe;
  w->mountpointof = NULL;
  w->ops          = &PipefsNodeOps;
  w->vfs          = NULL; // TODO: vlink stuff
  w->num_ref      = 1;
  pipe->write_end = w;

  *read  = r;
  *write = w;
}
