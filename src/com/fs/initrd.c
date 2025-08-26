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

#include <kernel/com/fs/initrd.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <lib/mem.h>
#include <lib/str.h>
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
        KASSERT(0 == com_fs_vfs_mkdir(file, dir, name, namelen, 0));
        return;
    }

    KASSERT(0 == com_fs_vfs_create(file, dir, name, namelen, 0));
    void  *contents  = (uint8_t *)hdr + 512;
    size_t file_size = oct_atoi(hdr->size, 11);
    size_t written   = 0;
    COM_FS_VFS_VNODE_HOLD((*file));
    KASSERT(0 == com_fs_vfs_write(&written, *file, contents, file_size, 0, 0));
    KASSERT(file_size == written);
    COM_FS_VFS_VNODE_RELEASE((*file));
}

void com_fs_initrd_make(com_vnode_t *root, void *tar, size_t tarsize) {
    KLOG("extracting initrd");

    for (uintmax_t i = 0; i < tarsize;) {
        struct tar_header *hdr = (struct tar_header *)((uint8_t *)tar + i);

        // If the first byte of the header is zero, than either the file has no
        // name or we've reached the last two blocks (end of file) that are
        // filled with zeros
        if (0 == hdr->name[0]) {
            break;
        }

        size_t file_size = 0;

        if (GNUTAR_LONG_LINK == hdr->type) {
            goto skip;
        }

        file_size            = oct_atoi(hdr->size, 11);
        char  *file_path     = hdr->name;
        size_t file_path_len = 100;

        if (GNUTAR_LONG_NAME == hdr->type) {
            file_path     = (char *)((char *)tar + i + 512);
            file_path_len = file_size;
            i += 512;
            i += (file_size + 512) & ~511UL;
            hdr       = (void *)((uint8_t *)tar + i);
            file_size = oct_atoi(hdr->size, 11);
        } else if (0 == file_path[99]) {
            file_path_len = kstrlen(file_path);
        }

        // Skip creating the . directory
        if (2 == file_path_len && '.' == file_path[0]) {
            goto skip;
        }

        // KDEBUG("extracting file %s", file_path);

        // Remove trailing /'es
        while (0 < file_path_len && '/' == file_path[file_path_len - 1]) {
            file_path_len--;
        }

        const char  *path_end = kmemrchr(file_path, '/', file_path_len);
        com_vnode_t *dir      = root;
        // offset into file_path where the ACTUAL name starts
        size_t file_name_off = 0;
        // length of the ACTUAL name of the file (not the path)
        size_t file_name_len = file_path_len;

        // kmemrchr returns NULL only if the character is not found, thus this
        // can be read as: "if this is not a directory"
        if (NULL != path_end) {
            size_t sl_len = path_end - file_path;
            com_fs_vfs_lookup(&dir, file_path, sl_len, root, root);
            KASSERT(NULL != dir);
            file_name_off = sl_len + 1;
            file_name_len = file_path_len - sl_len - 1;
        }

        while (file_name_len > 0 &&
               '/' == file_path[file_name_off + file_name_len - 1]) {
            file_name_len--;
        }

        com_vnode_t *file = NULL;
        create_node(&file, file_path + file_name_off, file_name_len, dir, hdr);

        if (dir != root) {
            COM_FS_VFS_VNODE_RELEASE(dir);
        }

    skip:
        i += 512;
        i += (file_size + 511) & ~511UL;
    }
}
