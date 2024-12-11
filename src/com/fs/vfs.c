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

#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <lib/mem.h>
#include <stdbool.h>

#define GET_VLINK(vn)              \
  while (NULL != vn->vlink.next) { \
    vn = vn->vlink.next;           \
  }

#define SKIP_SEPARATORS(path, end)                 \
  while (path != end && COM_VFS_PATH_SEP == '/') { \
    path++;                                        \
  }

void com_fs_vfs_vlink_set(com_vnode_t *parent, com_vnode_t *vlink) {
  if (NULL != parent) {
    if (NULL != parent->vlink.next) {
      parent->vlink.next->vlink.prev = NULL;
    }

    parent->vlink.next = vlink;
  }

  if (NULL != vlink) {
    vlink->vlink.prev = parent;
  }
}

int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd) {
  com_vnode_t *ret     = cwd;
  const char  *pathend = path + pathlen;

  if (0 < pathlen && COM_VFS_PATH_SEP == path[0]) {
    path++;
    ret = root;
  } else if (NULL == cwd) {
    return EINVAL;
  }

  SKIP_SEPARATORS(path, pathend);
  COM_VNODE_HOLD(ret);

  const char *section_end = NULL;
  bool        end_reached = false;
  while (path < pathend && !end_reached) {
    section_end = kmemchr(path, COM_VFS_PATH_SEP, pathend - path);
    end_reached = NULL == section_end;

    if (end_reached) {
      section_end = pathend;
    }

    bool dot    = 1 == section_end - path && 0 == kmemcmp(path, ".", 1);
    bool dotdot = 2 == section_end - path && 0 == kmemcmp(path, "..", 2);

    while (dotdot && ret->isroot && NULL != ret->vfs->mountpoint) {
      com_vnode_t *old = ret;
      ret              = ret->vfs->mountpoint;
      COM_VNODE_RELEASE(old);
      COM_VNODE_HOLD(ret);
    }

    if (!dot) {
      if (COM_VNODE_TYPE_DIR != ret->type) {
        goto invtype;
      }

      com_vnode_t *dir = ret;
      ret              = NULL;
      dir->ops->lookup(&ret, dir, path, section_end - path);

      if (NULL == ret) {
        COM_VNODE_RELEASE(ret);
        *out = NULL;
        return ENOENT;
      }

      // TODO: handle case in which ret is FS mountpoint
      // TODO: handle case in which ret is symlink
    }

    path = section_end + 1;
  }

  *out = ret;
  return 0;

invtype:
  COM_VNODE_RELEASE(ret);
  *out = NULL;
  return ENOTDIR;
}
