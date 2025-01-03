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
#include <kernel/com/fs/pipefs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/mem.h>
#include <lib/util.h>
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
  com_vnode_t     *read_end;
  com_vnode_t     *write_end;
};

static com_vnode_ops_t PipefsNodeOps = {.read  = com_fs_pipefs_read,
                                               .write = com_fs_pipefs_write,
                                               .close = com_fs_pipefs_close};

int com_fs_pipefs_read(void               *buf,
                       size_t              buflen,
                       size_t             *bytes_read,
                       com_vnode_t *node,
                       uintmax_t           off,
                       uintmax_t           flags) {
  (void)off;
  (void)flags;

  struct pipefs_node *pipe = node->extra;
  hdr_com_spinlock_acquire(&pipe->lock);

  size_t read_count = 0;

  for (size_t left = buflen; left > 0;) {
    while (pipe->write == pipe->read && NULL != pipe->write_end) {
      com_sys_sched_wait(&pipe->readers, &pipe->lock);
    }

    size_t diff   = pipe->write - pipe->read;
    size_t readsz = KMIN(left, diff);
    size_t modidx = pipe->read % PIPE_BUF_SZ;
    size_t end    = PIPE_BUF_SZ - modidx;

    kmemcpy(buf, (uint8_t *)pipe->buf + modidx, KMIN(readsz, end));

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

    com_sys_sched_notify(&pipe->writers);
  }

  hdr_com_spinlock_release(&pipe->lock);
  *bytes_read = read_count;
  return 0;
}

int com_fs_pipefs_write(size_t             *bytes_written,
                        com_vnode_t *node,
                        void               *buf,
                        size_t              buflen,
                        uintmax_t           off,
                        uintmax_t           flags) {
  (void)off;
  (void)flags;

  struct pipefs_node *pipe = node->extra;
  hdr_com_spinlock_acquire(&pipe->lock);

  size_t req_space   = buflen;
  size_t write_count = 0;

  if (buflen > PIPE_BUF_SZ) {
    // TODO: take a look here
    req_space = 1;
  }

  for (size_t left = buflen; left > 0;) {
    size_t av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);

    while (av_space < req_space) {
      com_sys_sched_wait(&pipe->writers, &pipe->lock);
      av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);
    }

    size_t writesz = KMIN(left, av_space);
    size_t modidx  = pipe->write % PIPE_BUF_SZ;
    size_t end     = PIPE_BUF_SZ - modidx;

    kmemcpy(pipe->buf + modidx, buf, KMIN(writesz, end));

    if (writesz > end) {
      kmemcpy(pipe->buf, (uint8_t *)buf + end, writesz - end);
    }

    pipe->write += writesz;
    buf = (uint8_t *)buf + writesz;
    left -= writesz;

    com_sys_sched_notify(&pipe->readers);
  }

  hdr_com_spinlock_release(&pipe->lock);
  *bytes_written = write_count;
  return 0;
}

int com_fs_pipefs_close(com_vnode_t *vnode) {
  struct pipefs_node *pipe = vnode->extra;
  hdr_com_spinlock_acquire(&pipe->lock);

  if (vnode == pipe->read_end) {
    pipe->write_end = NULL;
  } else if (vnode == pipe->write_end) {
    pipe->write_end = NULL;
    com_sys_sched_notify(&pipe->readers);
  }

  hdr_com_spinlock_release(&pipe->lock);
  // TODO: free pipe?
  return 0;
}

void com_fs_pipefs_new(com_vnode_t **read, com_vnode_t **write) {
  struct pipefs_node *pipe = com_mm_slab_alloc(sizeof(struct pipefs_node));
  TAILQ_INIT(&pipe->readers);
  TAILQ_INIT(&pipe->writers);
  pipe->read  = 0;
  pipe->write = 0;
  pipe->lock  = COM_SPINLOCK_NEW();
  pipe->buf   = (uint8_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());

  com_vnode_t *r = com_mm_slab_alloc(sizeof(com_vnode_t));
  r->extra              = pipe;
  r->mountpointof       = NULL;
  r->ops                = &PipefsNodeOps;
  r->vfs                = NULL; // TODO: vlink stuff
  r->num_ref            = 1;
  pipe->read_end        = r;

  com_vnode_t *w = com_mm_slab_alloc(sizeof(com_vnode_t));
  w->extra              = pipe;
  w->mountpointof       = NULL;
  w->ops                = &PipefsNodeOps;
  w->vfs                = NULL; // TODO: vlink stuff
  w->num_ref            = 1;
  pipe->write_end       = w;

  *read  = r;
  *write = w;
}
