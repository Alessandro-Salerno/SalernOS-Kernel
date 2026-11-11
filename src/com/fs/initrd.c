#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/initrd.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/util.h>

#define GNUTAR_SYMLINK   '2'
#define GNUTAR_LONG_NAME 'L'
#define GNUTAR_LONG_LINK 'K'
#define GNUTAR_DIR       '5'

struct TarHeader {
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

static u64 oct_atoi(const char *s, usize len) {
    u64 val = 0;

    for (usize i = 0; s[i] != 0 && i < len; i++) {
        val *= 8;          // exponentiation in base 8
        val += s[i] - '0'; // ASCII to binary number
    }

    return val;
}

static usize field_len(const char *field, usize size) {
    const char *nul = memchr(field, '\0', size);
    return nul != NULL ? (usize)(nul - field) : size;
}

// Existing directories are kept and existing files are replaced, as tar does
static void create_node(Vnode *dir, const char *name, usize namelen,
                        struct TarHeader *hdr) {
    if (hdr->type == GNUTAR_DIR || hdr->type == GNUTAR_SYMLINK) {
        VnodeAttr attr = VFS_ATTR(VNODE_TYPE_DIR);
        if (hdr->type == GNUTAR_SYMLINK) {
            attr.type = VNODE_TYPE_LINK;
            attr.target = hdr->linkname;
            attr.targetlen = field_len(hdr->linkname, sizeof(hdr->linkname));
        }

        int ret = vfs_create(dir, name, namelen, &attr, NULL);
        assert(ret == 0 || ret == EEXIST);
        (void)ret;
        return;
    }

    Vnode *file = NULL;
    int ret = vfs_create(dir, name, namelen, &VFS_ATTR(VNODE_TYPE_FILE), &file);
    if (ret == EEXIST) {
        ret = vfs_lookup(name, namelen, dir, dir, 0, &file);
        assert(ret != 0 || file->type == VNODE_TYPE_FILE);
        if (ret == 0) ret = vfs_truncate(file, 0);
    }
    assert(ret == 0);

    void *contents = (u8 *)hdr + 512;
    usize file_size = oct_atoi(hdr->size, 11);
    usize written = 0;
    verify(vfs_write(file, contents, file_size, 0, 0, &written) == 0);
    assert(file_size == written);
    vfs_vnode_rel(file);
}

void initrd_make(Vnode *root, void *tar, usize tarsize) {
    log("extracting initrd");

    for (u64 i = 0; i < tarsize;) {
        struct TarHeader *hdr = (struct TarHeader *)((u8 *)tar + i);

        // If the first byte of the header is zero, than either the file has no
        // name or we've reached the last two blocks (end of file) that are
        // filled with zeros
        if (hdr->name[0] == 0) break;

        usize file_size = 0;

        if (hdr->type == GNUTAR_LONG_LINK) goto skip;

        file_size = oct_atoi(hdr->size, 11);
        char *file_path = hdr->name;
        usize file_path_len = 100;

        if (hdr->type == GNUTAR_LONG_NAME) {
            // The size of the long name includes its NUL
            file_path = (char *)((char *)tar + i + 512);
            file_path_len = field_len(file_path, file_size);
            i += 512;
            i += (file_size + 512) & ~511UL;
            hdr = (void *)((u8 *)tar + i);
            file_size = oct_atoi(hdr->size, 11);
        } else if (file_path[99] == 0) {
            file_path_len = strlen(file_path);
        }

        // Skip creating the . directory
        if (file_path_len == 2 && file_path[0] == '.') goto skip;

        // log_debug("extracting file %s", file_path);

        Vnode *dir = NULL;
        const char *name;
        usize namelen;
        bool must_dir;
        verify(vfs_lookup_parent(file_path, file_path_len, root, root, &dir,
                                 &name, &namelen, &must_dir) == 0);
        create_node(dir, name, namelen, hdr);
        vfs_vnode_rel(dir);

    skip:
        i += 512;
        i += (file_size + 511) & ~511UL;
    }
}
