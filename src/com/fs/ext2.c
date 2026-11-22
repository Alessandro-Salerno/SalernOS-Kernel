#include <arch/info.h>
#include <dirent.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/ext2.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/buffcache.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/util.h>
#include <sys/stat.h>

#define EXT2_SUPERBLOCK_OFF 1024
#define EXT2_MAGIC          0xEF53
#define EXT2_ROOT_INO       2
#define EXT2_NULL           0

#define EXT2_GOOD_OLD_REV        0
#define EXT2_DYNAMIC_REV         1
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_GOOD_OLD_FIRST_INO  11

#define EXT2_NUM_DIRECT 12
#define EXT2_IND_BLOCK  12
#define EXT2_DIND_BLOCK 13
#define EXT2_TIND_BLOCK 14
#define EXT2_NUM_BLOCKS 15

#define EXT2_VALID_FS 1
#define EXT2_ERROR_FS 2

#define EXT2_FEATURE_INCOMPAT_FILETYPE      0x0002
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE   0x0002

#define EXT2_FEATURE_INCOMPAT_SUPP EXT2_FEATURE_INCOMPAT_FILETYPE
#define EXT2_FEATURE_RO_COMPAT_SUPP \
    (EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER | EXT2_FEATURE_RO_COMPAT_LARGE_FILE)

#define EXT2_S_IFMT   0xF000
#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000

#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

struct Ext2Superblock {
    u32 num_inodes;
    u32 num_blocks;
    u32 num_reserved_blocks;
    u32 num_free_blocks;
    u32 num_free_inodes;
    u32 first_data_block;
    u32 log_block_size; // block size is 1024 << log_block_size
    u32 log_frag_size;
    u32 blocks_per_group;
    u32 frags_per_group;
    u32 inodes_per_group;
    u32 mtime;
    u32 wtime;
    u16 num_mounts;
    i16 max_mounts;
    u16 magic;
    u16 state;
    u16 errors;
    u16 minor_rev;
    u32 last_check;
    u32 check_interval;
    u32 creator_os;
    u32 rev;
    u16 def_resuid;
    u16 def_resgid;
    u8 reserved[940];
} packed;

struct Ext2GroupDesc {
    u32 block_bitmap;
    u32 inode_bitmap;
    u32 inode_table;
    u16 num_free_blocks;
    u16 num_free_inodes;
    u16 num_dirs;
    u16 pad;
    u8 reserved[12];
} packed;

struct Ext2Inode {
    u16 mode;
    u16 uid;
    u32 size;
    u32 atime;
    u32 ctime;
    u32 mtime;
    u32 dtime;
    u16 gid;
    u16 num_links;
    u32 num_sectors; // 512-byte units, not blocks
    u32 flags;
    u32 osd1;
    u32 block[EXT2_NUM_BLOCKS];
    u32 generation;
    u32 file_acl;
    u32 size_high; // dir_acl in revision 0
    u32 faddr;
    u8 frag;
    u8 fsize;
    u16 pad;
    u16 uid_high;
    u16 gid_high;
    u32 reserved;
} packed;

// Without EXT2_FEATURE_INCOMPAT_FILETYPE, type is the high byte of a 16-bit
// name length, so it is always 0
struct Ext2DirEnt {
    u32 ino;
    u16 reclen;
    u8 namelen;
    u8 type;
    char name[];
} packed;

_Static_assert(sizeof(struct Ext2Superblock) == 1024, "");
_Static_assert(sizeof(struct Ext2GroupDesc) == 32, "");
_Static_assert(sizeof(struct Ext2Inode) == 128, "");

struct Ext2FS {
    Vfs vfs;
    struct Ext2Superblock *superblock;
    BufferPage *superblock_page;
    u32 num_groups;
    u32 block_size;
};

struct Ext2Node {
    Vnode vnode;
    BufferPage *inode_page; // held until inactive
    struct Ext2Inode *inode;
};

static VnodeOps g_ext2_node_ops;
static FSType g_ext2_type;

static inline struct Ext2FS *fs_of(Vfs *vfs) { return (struct Ext2FS *)vfs; }

static inline struct Ext2Node *extnode_of(Vnode *vnode) {
    return (struct Ext2Node *)vnode;
}

static int type_of_mode(u16 mode, VnodeType *out_type) {
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG:
            *out_type = VNODE_TYPE_FILE;
            return 0;
        case EXT2_S_IFDIR:
            *out_type = VNODE_TYPE_DIR;
            return 0;
        case EXT2_S_IFLNK:
            *out_type = VNODE_TYPE_LINK;
            return 0;
        case EXT2_S_IFCHR:
            *out_type = VNODE_TYPE_CHARDEV;
            return 0;
        case EXT2_S_IFBLK:
            *out_type = VNODE_TYPE_BLOCKDEV;
            return 0;
        case EXT2_S_IFIFO:
            *out_type = VNODE_TYPE_FIFO;
            return 0;
        case EXT2_S_IFSOCK:
            *out_type = VNODE_TYPE_SOCKET;
            return 0;
        default:
            return EIO;
    }
}

// NODES

// *out_desc points into *out_page and can be released with buffcache_rel
static int bg_get(struct Ext2FS *fs, u32 bgid, BufferPage **out_page,
                  struct Ext2GroupDesc **out_desc) {
    u32 first_data_block = fs->superblock->first_data_block;
    u32 block_groups_start = first_data_block + 1;
    usize bg_glob_off = (usize)block_groups_start * fs->block_size +
        (usize)bgid * sizeof(struct Ext2GroupDesc);
    usize bg_page_index = bg_glob_off / PAGE_SIZE;

    BufferPage *bg_page;
    int ret = vfs_get_page(fs->vfs.backing, bg_page_index, VFS_GETPAGE_RDONLY,
                           &bg_page);
    if (ret != 0) return ret;
    struct Ext2GroupDesc *bg_desc = (void *)PHYS_TO_HHDM(
        (u8 *)buffcache_get_phys(bg_page) + bg_glob_off % PAGE_SIZE);

    *out_page = bg_page;
    *out_desc = bg_desc;
    return 0;
}

// *out_inode points into *out_page and can be released with buffcache_rel
static int inode_get(struct Ext2FS *fs, ino_t ino, BufferPage **out_page,
                     struct Ext2Inode **out_inode) {
    assert(ino > 0);
    u32 inodes_per_group = fs->superblock->inodes_per_group;
    u32 bgid = (ino - 1) / inodes_per_group;
    u32 inode_index = (ino - 1) % inodes_per_group;

    BufferPage *bg_page;
    struct Ext2GroupDesc *bg_desc;
    int ret = bg_get(fs, bgid, &bg_page, &bg_desc);
    if (ret != 0) return ret;

    u32 inode_table_start = bg_desc->inode_table;
    usize inode_glob_off = (usize)inode_table_start * fs->block_size +
        (usize)inode_index * EXT2_GOOD_OLD_INODE_SIZE;
    usize inode_page_index = inode_glob_off / PAGE_SIZE;
    if (inode_table_start == 0 ||
        inode_table_start +
                inode_index * EXT2_GOOD_OLD_INODE_SIZE / fs->block_size >=
            fs->superblock->num_blocks)
        return EIO;

    BufferPage *inode_page;
    ret = vfs_get_page(fs->vfs.backing, inode_page_index, 0, &inode_page);
    if (ret != 0) {
        buffcache_rel(bg_page);
        return ret;
    }

    struct Ext2Inode *inode = (void *)PHYS_TO_HHDM(
        (u8 *)buffcache_get_phys(inode_page) + inode_glob_off % PAGE_SIZE);

    // We don't release the inode's page and instead keep a reference for each
    // open inode. This way, the inode structure does not need to be copied,
    // hich is a minor performance gain, but a major simplification for
    // filesystem sync because it means we don't need to have a special hook for
    // metadata sync and can instead just flush the whole buffer cache and get
    // the same result
    buffcache_rel(bg_page);
    *out_page = inode_page;
    *out_inode = inode;
    return 0;
}

static int ptr_read(struct Ext2FS *fs, u32 block, u32 index, u32 *out_ptr) {
    assert(index < PAGE_SIZE / sizeof(u32));
    assert(fs->block_size == PAGE_SIZE);
    BufferPage *page;
    int ret = vfs_get_page(fs->vfs.backing, block, VFS_GETPAGE_RDONLY, &page);
    if (ret != 0) return ret;
    u32 *block_contents = (void *)PHYS_TO_HHDM(buffcache_get_phys(page));
    *out_ptr = block_contents[index];
    buffcache_rel(page);
    // We allow EXT2_NULL because bmap uses it to find holes, so returning an
    // error would erroneously confuse a hole with a corrupted fs. We also do
    // the check here because it's easier, the value of *out_ptr should be
    // ignored on failures anyway
    if (*out_ptr >= fs->superblock->num_blocks) return EIO;
    return 0;
}

// *out_pblock == EXT2_NULL is a hole
static int bmap(struct Ext2Node *node, u32 lblock, bool alloc,
                u32 *out_pblock) {
    assert(!alloc);
    struct Ext2Inode *inode = node->inode;
    struct Ext2FS *fs = fs_of(node->vnode.vfs);

    u32 pblock = EXT2_NULL;
    int ret = 0;

    if (lblock < EXT2_NUM_DIRECT) {
        pblock = inode->block[lblock];
        goto exit;
    }

    // Each layer is an array of pointers. Given a block size B (in bytes),
    // there are B/4 pointers per layer. Assuming B = 4096, for example, we have
    // 1024 pointers per layer. log2(4096) = 12, but superblock->log_block_size
    // is stored as a shift from 1024, not from one, so log_block_size = log2(B)
    // - log2(1024) = log2(B) - 10. Again, assuming B = 4096, log2(B) =
    // log2(4096) = 12, so log_block_shift = 12 - 10 = 2. Therefore, for 1 << N
    // to be equal to B/4 and N to be derived from log_block_shift, we need N =
    // log_block_shift + 8

    u32 ptr_shift = fs->superblock->log_block_size + 8;
    u32 ptr_mask = (1ULL << ptr_shift) - 1;
    u64 span = 1ULL << ptr_shift;
    u32 depth = 1;
    lblock -= EXT2_NUM_DIRECT;
    for (; lblock >= span; depth++, span <<= ptr_shift) {
        if (depth == 3) {
            ret = EFBIG;
            goto exit;
        }
        lblock -= span;
    }

    pblock = inode->block[EXT2_IND_BLOCK + depth - 1];
    if (pblock >= fs->superblock->num_blocks) return EIO;
    for (u32 level = depth; level > 0 && pblock != EXT2_NULL; level--) {
        u32 index = (lblock >> (ptr_shift * (level - 1))) & ptr_mask;
        ret = ptr_read(fs, pblock, index, &pblock);
        if (ret != 0) goto exit;
    }

exit:
    if (pblock >= fs->superblock->num_blocks) return EIO;
    *out_pblock = pblock;
    return ret;
}

static int check_file_io(Vnode *node) {
    if (node->type == VNODE_TYPE_FILE) return 0;
    return node->type == VNODE_TYPE_DIR ? EISDIR : EINVAL;
}

// *out_data points into *out_page, released with buffcache_rel
static int data_get(struct Ext2Node *dir, u32 lblock, BufferPage **out_page,
                    u8 **out_data) {
    struct Ext2FS *fs = fs_of(dir->vnode.vfs);
    u32 pblock;
    int ret = bmap(dir, lblock, false, &pblock);
    if (ret != 0) return ret;
    if (pblock == EXT2_NULL) return EIO;

    ret = vfs_get_page(fs->vfs.backing, pblock, VFS_GETPAGE_RDONLY, out_page);
    if (ret != 0) return ret;
    *out_data = (u8 *)PHYS_TO_HHDM(buffcache_get_phys(*out_page));
    return 0;
}

static bool dirent_validate(struct Ext2DirEnt *entry, usize pos,
                            usize block_size) {
    return entry->reclen >= sizeof(struct Ext2DirEnt) &&
        entry->reclen % 4 == 0 && pos + entry->reclen <= block_size &&
        sizeof(struct Ext2DirEnt) + entry->namelen <= entry->reclen;
}

static int readdir_emit(void *buf, usize buflen, VFSDirEnt *hdr,
                        const char *name, usize namelen,
                        usize *out_bytes_read) {
    usize req_size = sizeof(VFSDirEnt) + namelen + 1;
    if (buflen < req_size) return EOVERFLOW;

    hdr->reclen = req_size;
    VFSDirEnt *ud = buf;
    int ret = memcpy_maybe_user(ud, hdr, sizeof(*hdr));
    if (ret == 0) ret = memcpy_maybe_user(ud->name, name, namelen);
    if (ret == 0) ret = memset_maybe_user(ud->name + namelen, 0, 1);
    if (ret == 0) *out_bytes_read = req_size;
    return ret;
}

// VNODE OPS

static void ext2_inactive(Vnode *node) {
    buffcache_rel(extnode_of(node)->inode_page);
    slab_free(node, sizeof(struct Ext2Node));
}

static int ext2_lookup(Vnode *dir, const char *name, usize namelen,
                       Vnode **out_vnode) {
    struct Ext2Node *edir = extnode_of(dir);
    struct Ext2FS *fs = fs_of(dir->vfs);
    u32 num_blocks = edir->inode->size / fs->block_size;
    *out_vnode = NULL;

    // Inode number 0 does not exist, so we skip it because it is either padding
    // or deleted

    for (u32 lblock = 0; lblock < num_blocks; lblock++) {
        BufferPage *page;
        u8 *data;
        int ret = data_get(edir, lblock, &page, &data);
        if (ret != 0) return ret;

        ino_t ino = 0;
        for (usize pos = 0; pos < fs->block_size;) {
            struct Ext2DirEnt *entry = (void *)(data + pos);
            if (!dirent_validate(entry, pos, fs->block_size)) {
                ret = EIO;
                break;
            }
            if (entry->ino != 0 && entry->namelen == namelen &&
                memcmp(entry->name, name, namelen) == 0) {
                ino = entry->ino;
                break;
            }
            pos += entry->reclen;
        }

        buffcache_rel(page);
        if (ret != 0) return ret;
        if (ino != 0) return vfs_vget(dir->vfs, ino, out_vnode);
    }

    return ENOENT;
}

static int ext2_readdir(void *buf, usize buflen, Vnode *dir, u64 off,
                        usize *out_bytes_read, u64 *out_next_off) {
    struct Ext2Node *edir = extnode_of(dir);
    struct Ext2FS *fs = fs_of(dir->vfs);
    u64 size = edir->inode->size;
    *out_bytes_read = 0;
    *out_next_off = off;

    while (off < size) {
        BufferPage *page;
        u8 *data;
        int ret = data_get(edir, off / fs->block_size, &page, &data);
        if (ret != 0) return ret;

        u64 block_start = off - off % fs->block_size;
        for (usize pos = off % fs->block_size; pos < fs->block_size;) {
            struct Ext2DirEnt *entry = (void *)(data + pos);
            if (!dirent_validate(entry, pos, fs->block_size)) {
                buffcache_rel(page);
                return EIO;
            }
            if (entry->ino != 0) {
                VFSDirEnt hdr = {0};
                hdr.ino = entry->ino;
                hdr.type = DT_UNKNOWN;
                hdr.off = block_start + pos + entry->reclen;
                ret = readdir_emit(buf, buflen, &hdr, entry->name,
                                   entry->namelen, out_bytes_read);
                buffcache_rel(page);
                if (ret == 0) *out_next_off = hdr.off;
                return ret;
            }
            pos += entry->reclen;
        }

        buffcache_rel(page);
        off = block_start + fs->block_size;
    }

    *out_next_off = off;
    return 0;
}

static int ext2_readlink(char *buf, usize buflen, Vnode *link, usize *out_len) {
    struct Ext2Node *node = extnode_of(link);
    struct Ext2FS *fs = fs_of(link->vfs);
    u32 size = node->inode->size;
    *out_len = 0;
    if (size >= fs->block_size) return EIO;

    // Fast symlinks keep the target in the block pointers
    if (node->inode->num_sectors == 0) {
        if (size > sizeof(node->inode->block)) return EIO;
        memcpy(buf, node->inode->block, min(buflen, size));
        *out_len = size;
        return 0;
    }

    BufferPage *page;
    u8 *data;
    int ret = data_get(node, 0, &page, &data);
    if (ret != 0) return ret;
    memcpy(buf, data, min(buflen, size));
    buffcache_rel(page);
    *out_len = size;
    return 0;
}

static int ext2_read(void *buf, usize buflen, Vnode *vnode, u64 off, u64 flags,
                     usize *out_bytes_read) {
    (void)flags;
    struct Ext2Node *node = extnode_of(vnode);
    struct Ext2FS *fs = fs_of(vnode->vfs);
    int ret = 0;
    assert(fs->block_size == PAGE_SIZE);

    ret = check_file_io(vnode);
    if (ret != 0) return ret;

    *out_bytes_read = 0;
    if (off >= node->inode->size) return 0;
    buflen = min(buflen, node->inode->size - off);

    while (buflen > 0) {
        if (signal_check() != SIGNAL_NONE) {
            ret = EINTR;
            break;
        }

        usize page_off = off % PAGE_SIZE;
        usize len = min(PAGE_SIZE - page_off, buflen);
        u32 pblock;
        ret = bmap(node, off / PAGE_SIZE, false, &pblock);
        if (ret != 0) break;
        if (pblock == EXT2_NULL) ret = memset_maybe_user(buf, 0, len);
        else {
            BufferPage *page;
            ret = vfs_get_page(fs->vfs.backing, pblock, VFS_GETPAGE_RDONLY,
                               &page);
            if (ret != 0) break;
            u8 *data = (void *)PHYS_TO_HHDM(buffcache_get_phys(page));
            ret = memcpy_maybe_user(buf, data + page_off, len);
            buffcache_rel(page);
        }
        if (ret != 0) break;

        buf = (u8 *)buf + len;
        off += len;
        buflen -= len;
        *out_bytes_read += len;
    }

    return *out_bytes_read != 0 ? 0 : ret;
}

static int ext2_mmap(Vnode *node, VMMContext *vmm_context, void *hint,
                     usize size, int vmm_flags, MMUFlags mmu_flags, u64 off,
                     void **out_addr) {
    return ENOSYS;
}

static int ext2_stat(Vnode *vnode, struct stat *out_stat) {
    struct Ext2Inode *inode = extnode_of(vnode)->inode;
    out_stat->st_mode = inode->mode & ~EXT2_S_IFMT;
    out_stat->st_nlink = inode->num_links;
    out_stat->st_uid = inode->uid | (u32)inode->uid_high << 16;
    out_stat->st_gid = inode->gid | (u32)inode->gid_high << 16;
    out_stat->st_size = inode->size;
    out_stat->st_blksize = fs_of(vnode->vfs)->block_size;
    out_stat->st_blocks = inode->num_sectors;
    out_stat->st_atim.tv_sec = inode->atime;
    out_stat->st_mtim.tv_sec = inode->mtime;
    out_stat->st_ctim.tv_sec = inode->ctime;

    // Linux stores devices that fit major << 8 | minor in block[0]
    if (vnode->type == VNODE_TYPE_CHARDEV ||
        vnode->type == VNODE_TYPE_BLOCKDEV) {
        out_stat->st_rdev = inode->block[0];
    }

    return 0;
}

static VnodeOps g_ext2_node_ops = {.inactive = ext2_inactive,
                                   .lookup = ext2_lookup,
                                   .readdir = ext2_readdir,
                                   .readlink = ext2_readlink,
                                   .read = ext2_read,
                                   .mmap = ext2_mmap,
                                   .stat = ext2_stat};

// FILE SYSTEM OPS

static inline u32 sb_num_groups(struct Ext2Superblock *superblock) {
    u32 data_blocks = superblock->num_blocks - superblock->first_data_block;
    return (data_blocks + superblock->blocks_per_group - 1) /
        superblock->blocks_per_group;
}

static bool validate_superblock(struct Ext2Superblock *superblock) {
    if (superblock->magic != EXT2_MAGIC) return false;
    if (superblock->rev != EXT2_GOOD_OLD_REV) return false;
    // shifting by a garbage value is undefined
    if (superblock->log_block_size > 2 ||
        (1024u << superblock->log_block_size) != PAGE_SIZE) {
        return false;
    }

    // Only 1024-byte blocks start at block 1
    if (superblock->first_data_block != 0) return false;
    if (superblock->num_blocks == 0) return false;

    // A group is as large as one bitmap block can describe
    u32 bitmap_bits = 8 * PAGE_SIZE;
    if (superblock->blocks_per_group == 0 ||
        superblock->blocks_per_group > bitmap_bits) {
        return false;
    }
    if (superblock->inodes_per_group == 0 ||
        superblock->inodes_per_group > bitmap_bits) {
        return false;
    }

    // So that every valid inode number falls in an existing group
    u64 max_inodes = (u64)sb_num_groups(superblock) *
        superblock->inodes_per_group;
    return superblock->num_inodes <= max_inodes;
}

static int ext2_unmount(Vfs *vfs) { return ENOSYS; }

static int ext2_vget(Vfs *vfs, ino_t ino, Vnode **out_vnode) {
    struct Ext2FS *fs = fs_of(vfs);
    *out_vnode = NULL;
    if (ino == 0 || ino > fs->superblock->num_inodes) return EIO;

    struct Ext2Node *node = slab_alloc(sizeof(struct Ext2Node));
    if (node == NULL) return ENOMEM;

    int ret = inode_get(fs, ino, &node->inode_page, &node->inode);
    if (ret != 0) {
        slab_free(node, sizeof(struct Ext2Node));
        return ret;
    }

    VnodeType type;
    if (node->inode->num_links == 0) ret = EIO;
    if (ret == 0) ret = type_of_mode(node->inode->mode, &type);
    if (ret != 0) {
        buffcache_rel(node->inode_page);
        slab_free(node, sizeof(struct Ext2Node));
        return ret;
    }

    vfs_vnode_init(&node->vnode, vfs, type, &g_ext2_node_ops, NULL);
    *out_vnode = &node->vnode;
    return 0;
}

static int ext2_op_mount(Vnode *backing, u64 flags, Vfs **out_vfs) {
    BufferPage *superblock_page;
    int ret = vfs_get_page(backing, 0, VFS_GETPAGE_RDONLY, &superblock_page);
    if (ret != 0) return ret;

    void *sb_phys = buffcache_get_phys(superblock_page);
    void *sb_hhdm = (void *)PHYS_TO_HHDM(sb_phys);
    struct Ext2Superblock *superblock = (void *)((u8 *)sb_hhdm +
                                                 EXT2_SUPERBLOCK_OFF);
    if (!validate_superblock(superblock)) {
        buffcache_rel(superblock_page);
        return EIO;
    }

    struct Ext2FS *fs = slab_alloc(sizeof(struct Ext2FS));
    if (fs == NULL) {
        buffcache_rel(superblock_page);
        return ENOMEM;
    }

    vfs_init_vfs(&fs->vfs, &g_ext2_type, NULL);
    fs->vfs.backing = backing;
    fs->superblock = superblock;
    fs->superblock_page = superblock_page;
    fs->block_size = 1024 << superblock->log_block_size;
    fs->num_groups = sb_num_groups(superblock);
    ret = vfs_cache_init(&fs->vfs, 100);
    if (ret != 0) {
        slab_free(fs, sizeof(*fs));
        buffcache_rel(superblock_page);
        return ret;
    }

    Vnode *root;
    ret = vfs_vget(&fs->vfs, EXT2_ROOT_INO, &root);
    if (ret != 0) {
        vfs_cache_destroy(&fs->vfs);
        slab_free(fs, sizeof(*fs));
        buffcache_rel(superblock_page);
        return ret;
    }

    if (root->type == VNODE_TYPE_DIR) vfs_set_root(&fs->vfs, root);
    else {
        vfs_vnode_rel(root);
        vfs_cache_destroy(&fs->vfs);
        slab_free(fs, sizeof(*fs));
        buffcache_rel(superblock_page);
        return EIO;
    }

    *out_vfs = &fs->vfs;
    return 0;
}

static bool ext2_keep(Vnode *node) { return true; }

static FSType g_ext2_type = {.name = "ext2",
                             .needs_backing = true,
                             .mount = ext2_op_mount,
                             .unmount = ext2_unmount,
                             .vget = ext2_vget,
                             .keep = ext2_keep};

// INTERFACE

void ext2_init(void) { verify(vfs_register_type(&g_ext2_type) == 0); }
