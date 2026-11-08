/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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
#include <kernel/com/fs/initrd.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <lib/mem.h>
#include <lib/str.h>
#include <stdint.h>

#define GNUTAR_SYMLINK   '2'
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
    char linkname[100];
};

static uintmax_t oct_atoi(const char *s, size_t len) {
    uintmax_t val = 0;

    for (size_t i = 0; 0 != s[i] && i < len; i++) {
        val *= 8;          // exponentiation in base 8
        val += s[i] - '0'; // ASCII to binary number
    }

    return val;
}

static size_t field_len(const char *field, size_t size) {
    const char *nul = kmemchr(field, '\0', size);
    return NULL != nul ? (size_t)(nul - field) : size;
}

// Existing directories are kept and existing files are replaced, as tar does
static void create_node(com_vnode_t       *dir,
                        const char        *name,
                        size_t             namelen,
                        struct tar_header *hdr) {
    if (GNUTAR_DIR == hdr->type || GNUTAR_SYMLINK == hdr->type) {
        com_vnode_attr_t attr = COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_DIR);
        if (GNUTAR_SYMLINK == hdr->type) {
            attr.type      = E_COM_VNODE_TYPE_LINK;
            attr.target    = hdr->linkname;
            attr.targetlen = field_len(hdr->linkname, sizeof(hdr->linkname));
        }

        int ret = com_fs_vfs_create(NULL, dir, name, namelen, &attr);
        KASSERT(0 == ret || EEXIST == ret);
        (void)ret;
        return;
    }

    com_vnode_t *file = NULL;
    int ret = com_fs_vfs_create(&file,
                                dir,
                                name,
                                namelen,
                                &COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_FILE));
    if (EEXIST == ret) {
        ret = com_fs_vfs_lookup(&file, name, namelen, dir, dir, 0);
        KASSERT(0 != ret || E_COM_VNODE_TYPE_FILE == file->type);
        if (0 == ret) {
            ret = com_fs_vfs_truncate(file, 0);
        }
    }
    KASSERT(0 == ret);

    void  *contents  = (uint8_t *)hdr + 512;
    size_t file_size = oct_atoi(hdr->size, 11);
    size_t written   = 0;
    KASSERT_CALL(0,
                 ==,
                 com_fs_vfs_write(&written, file, contents, file_size, 0, 0));
    KASSERT(file_size == written);
    COM_FS_VFS_VNODE_RELEASE(file);
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
            // The size of the long name includes its NUL
            file_path     = (char *)((char *)tar + i + 512);
            file_path_len = field_len(file_path, file_size);
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

        com_vnode_t *dir = NULL;
        const char  *name;
        size_t       namelen;
        bool         must_dir;
        KASSERT_CALL(0,
                     ==,
                     com_fs_vfs_lookup_parent(&dir,
                                              &name,
                                              &namelen,
                                              &must_dir,
                                              file_path,
                                              file_path_len,
                                              root,
                                              root));
        create_node(dir, name, namelen, hdr);
        COM_FS_VFS_VNODE_RELEASE(dir);

    skip:
        i += 512;
        i += (file_size + 511) & ~511UL;
    }
}
