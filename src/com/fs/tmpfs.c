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

#include <kernel/com/fs/pagecache.h>
#include <kernel/com/fs/vfs.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

struct tmpfs_dir_entry {
  TAILQ_ENTRY(tmpfs_dir_entry) entries;
  struct tmpfs_node *tnode;
  struct {
    const char *buf;
    size_t      len;
  } name;
};

TAILQ_HEAD(tmpfs_dir_entries, tmpfs_dir_entry);

struct tmpfs_node {
  com_vnode_t *vnode;

  union {
    struct {
      struct tmpfs_dir_entries entries;
      struct tmpfs_node       *parent;
    } dir;
    struct {
      size_t           size;
      com_pagecache_t *data;
    } file;
    struct {
      const char *path;
      size_t      len;
    } link;
  };
};

static com_vfs_ops_t   TmpfsOps;
static com_vnode_ops_t TmpfsNodeOps;
