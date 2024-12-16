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

#include <kernel/com/fs/initrd.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/log.h>
#include <lib/mem.h>
#include <stdint.h>

#define GNUTAR_LONG_NAME 'L'
#define GNUTAR_LONG_LINK 'K'
#define GNUTAR_DIR       '5'

struct tar_header {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char unused[12]; // mtime
  char unused2[8]; // checksum
  char type;
};

static uintmax_t oct_atoi(const char *s, size_t len) {
  uintmax_t val = 0;

  for (size_t i = 0; 0 != s[i] && i < len; i++) {
    val *= 8;          // exponentiation in base 8
    val += s[i] - '0'; // ASCII to binary number
  }

  return val;
}

static void create_node(com_vnode_t      **file,
                        const char        *name,
                        size_t             namelen,
                        com_vnode_t       *dir,
                        struct tar_header *hdr) {
  if (GNUTAR_DIR == hdr->type) {
    ASSERT(0 == com_fs_vfs_mkdir(file, dir, name, namelen, 0));
    return;
  }

  ASSERT(0 == com_fs_vfs_create(file, dir, name, namelen, 0));
  void  *contents  = (uint8_t *)hdr + 512;
  size_t file_size = oct_atoi(hdr->size, 11);
  size_t written   = 0;
  ASSERT(0 == com_fs_vfs_write(&written, *file, contents, file_size, 0, 0));
  ASSERT(file_size == written);
}

void com_fs_initrd_make(com_vnode_t *root, void *tar, size_t tarsize) {
  LOG("extracting initrd");

  for (uintmax_t i = 0; i < tarsize;) {
    struct tar_header *hdr = (struct tar_header *)((uint8_t *)tar + i);

    // If the first byte of the header is zero, than either the file has no name
    // or we've reached the last two blocks (end of file) that are filled with
    // zers
    if (0 == *(uint8_t *)hdr) {
      break;
    }

    // These two are not supported
    if (GNUTAR_LONG_NAME == hdr->type || GNUTAR_LONG_LINK == hdr->type) {
      goto skip;
    }

    size_t file_size     = oct_atoi(hdr->size, 11);
    char  *file_name     = hdr->name;
    size_t file_name_len = 100;

    if (0 == file_name[99]) {
      // TODO: poor man's kstrlen, move this to lib/str.h
      file_name_len = 0;
      char *s       = file_name;
      while (0 != *s++) {
        file_name_len++;
      }
    }

    DEBUG("extracting file %s", file_name);

    while (0 < file_name_len && '/' == file_name[file_name_len - 1]) {
      file_name_len--;
    }

    const char  *path_end = kmemchr(file_name, '/', file_name_len);
    com_vnode_t *dir      = root;
    size_t       file_idx = 0;
    size_t       file_len = file_name_len;

    if (NULL != path_end) {
      size_t sl_len = path_end - file_name;
      com_fs_vfs_lookup(&dir, file_name, sl_len, root, root);
      ASSERT(NULL != dir);
      file_idx = sl_len + 1;
      file_len = file_name_len - sl_len - 1;
    }

    while (0 < file_len && '/' == file_name[file_idx + file_len - 1]) {
      file_len--;
    }

    com_vnode_t *file = NULL;
    create_node(&file, file_name + file_idx, file_len, dir, hdr);

    COM_VNODE_RELEASE(file);
    if (dir != root) {
      COM_VNODE_RELEASE(dir);
    }

  skip:
    i += 512;
    i += (file_size + 511) & ~511UL;
  }
}
