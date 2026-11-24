#include <arch/cpu.h>
#include <arch/info.h>
#include <dirent.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/ext2.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/buffcache.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sync/rwlock.h>
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
    // Revision 1 only
    u32 first_ino;
    u16 inode_size;
    u16 block_group_nr;
    u32 feature_compat;
    u32 feature_incompat;
    u32 feature_ro_compat;
    u8 reserved[920];
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
    union {
        u32 block[EXT2_NUM_BLOCKS];
        char fast_link[EXT2_NUM_BLOCKS * sizeof(u32)];
        u32 rdev; // major << 8 | minor, as Linux does when they fit
    };
    u32 generation;
    u32 file_acl;
    union {
        u32 size_high; // regular files
        u32 dir_acl;
    };
    u32 faddr;
    u8 frag;
    u8 fsize;
    u16 pad;
    u16 uid_high;
    u16 gid_high;
    u32 reserved;
};

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
    u32 inode_size;
    u32 first_ino;
    // Technically an over-approximation, but it works and its practical impact
    // is low for now: allocations are all group-local, so an fswide lock is
    // safe but potentially slow, though or block device drivers are slow enough
    // that they make this a good simplification
    Mutex alloc_lock;
};

struct Ext2Node {
    Vnode vnode;
    BufferPage *inode_page; // held until inactive
    struct Ext2Inode *inode;
    // Directories are already locked by the VFS, so this lock only protects
    // files
    RWLock file_lock;
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

// Devices need rdev, which create cannot provide
static int mode_of_type(VnodeType type, u16 *out_mode) {
    switch (type) {
        case VNODE_TYPE_FILE:
            *out_mode = EXT2_S_IFREG;
            return 0;
        case VNODE_TYPE_DIR:
            *out_mode = EXT2_S_IFDIR;
            return 0;
        case VNODE_TYPE_LINK:
            *out_mode = EXT2_S_IFLNK;
            return 0;
        case VNODE_TYPE_FIFO:
            *out_mode = EXT2_S_IFIFO;
            return 0;
        case VNODE_TYPE_SOCKET:
            *out_mode = EXT2_S_IFSOCK;
            return 0;
        default:
            return EPERM;
    }
}

static inline u64 file_size(struct Ext2Inode *inode) {
    if ((inode->mode & EXT2_S_IFMT) != EXT2_S_IFREG) return inode->size;
    return inode->size | (u64)inode->size_high << 32;
}

static inline bool has_incompat(struct Ext2Superblock *superblock,
                                u32 feature) {
    return superblock->rev == EXT2_DYNAMIC_REV &&
        (superblock->feature_incompat & feature) != 0;
}

static unsigned char dirent_type(struct Ext2FS *fs, u8 type) {
    if (!has_incompat(fs->superblock, EXT2_FEATURE_INCOMPAT_FILETYPE)) {
        return DT_UNKNOWN;
    }

    switch (type) {
        case EXT2_FT_REG_FILE:
            return DT_REG;
        case EXT2_FT_DIR:
            return DT_DIR;
        case EXT2_FT_CHRDEV:
            return DT_CHR;
        case EXT2_FT_BLKDEV:
            return DT_BLK;
        case EXT2_FT_FIFO:
            return DT_FIFO;
        case EXT2_FT_SOCK:
            return DT_SOCK;
        case EXT2_FT_SYMLINK:
            return DT_LNK;
        default:
            return DT_UNKNOWN;
    }
}

static u8 ft_of_type(struct Ext2FS *fs, VnodeType type) {
    if (!has_incompat(fs->superblock, EXT2_FEATURE_INCOMPAT_FILETYPE)) {
        return EXT2_FT_UNKNOWN;
    }

    switch (type) {
        case VNODE_TYPE_FILE:
            return EXT2_FT_REG_FILE;
        case VNODE_TYPE_DIR:
            return EXT2_FT_DIR;
        case VNODE_TYPE_CHARDEV:
            return EXT2_FT_CHRDEV;
        case VNODE_TYPE_BLOCKDEV:
            return EXT2_FT_BLKDEV;
        case VNODE_TYPE_FIFO:
            return EXT2_FT_FIFO;
        case VNODE_TYPE_SOCKET:
            return EXT2_FT_SOCK;
        case VNODE_TYPE_LINK:
            return EXT2_FT_SYMLINK;
        default:
            return EXT2_FT_UNKNOWN;
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
        (usize)inode_index * fs->inode_size;
    usize inode_page_index = inode_glob_off / PAGE_SIZE;
    if (inode_table_start == 0 ||
        inode_table_start + inode_index * fs->inode_size / fs->block_size >=
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

static int bitmap_alloc(struct Ext2FS *fs, u32 bitmap_block, u32 num_bits,
                        u32 preferred_bit, u32 *out_bit) {
    assert(preferred_bit < num_bits);
    u32 num_bytes = num_bits / 8 + (num_bits % 8 != 0);

    BufferPage *bitmap_page;
    int ret = vfs_get_page(fs->vfs.backing, bitmap_block, 0, &bitmap_page);
    if (ret != 0) return ret;

    u8 *bitmap = (void *)PHYS_TO_HHDM(buffcache_get_phys(bitmap_page));
    u32 found = 0;

    u32 preferred_byte = preferred_bit / 8;
    u32 preferred_off = preferred_bit % 8;
    if (!(bitmap[preferred_byte] & (1 << preferred_off))) {
        found = preferred_bit;
        goto exit;
    }

    // num_bits is bounded by how many bits can stay in a block, but it can also
    // be lower. When it is lower, it can also not be a multiple of the byte
    // size. Therefore, we loop on every byte and get the first free bit. If the
    // iteration is the "tail" iteration, the allocation may still fail even if
    // a bit is found when that bit is past the tail

    // We start from preferred_bit and wrap around instead of starting from 0 to
    // hopefully keep some locality

    for (usize n = 0; n < num_bytes; n++) {
        u32 i = (preferred_bit / 8 + n) % num_bytes;
        u8 byte = bitmap[i];
        usize free_bit = __builtin_ffs(~byte & 0xff);
        if (free_bit != 0) {
            free_bit--;
            found = i * 8 + free_bit;
            // This terminates because the loop guard is monotonic, though it
            // may perform extra checks if the bitmap is entirely full
            if (found < num_bits) goto exit;
        }
    }

    // We only get here if the loop terminated naturally, which only happens on
    // failed allocations
    ret = ENOSPC;

exit:
    if (ret == 0) {
        bitmap[found / 8] |= 1 << (found % 8);
        buffcache_mkdirty(bitmap_page);
        *out_bit = found;
    }
    buffcache_rel(bitmap_page);
    return ret;
}

static int bitmap_free(struct Ext2FS *fs, u32 bitmap_block, u32 bit) {
    BufferPage *bitmap_page;
    int ret = vfs_get_page(fs->vfs.backing, bitmap_block, 0, &bitmap_page);
    if (ret != 0) return ret;

    u8 *bitmap = (void *)PHYS_TO_HHDM(buffcache_get_phys(bitmap_page));
    u32 byte = bit / 8;
    u32 off = bit % 8;
    if (!(bitmap[byte] & (1 << off))) ret = EIO;
    else {
        bitmap[byte] &= ~(1 << off);
        buffcache_mkdirty(bitmap_page);
    }

    buffcache_rel(bitmap_page);
    return ret;
}

// The last group is usually shorter
static u32 group_num_blocks(struct Ext2FS *fs, u32 bgid) {
    struct Ext2Superblock *superblock = fs->superblock;
    if (bgid < fs->num_groups - 1) return superblock->blocks_per_group;
    return superblock->num_blocks - superblock->first_data_block -
        bgid * superblock->blocks_per_group;
}

static int block_alloc(struct Ext2FS *fs, u32 preferred_block, u32 *out_block) {
    assert(preferred_block < fs->superblock->num_blocks);
    mutex_acq(&fs->alloc_lock);
    int ret = 0;
    u32 block = EXT2_NULL;

    u32 preferred_rel = preferred_block == EXT2_NULL
        ? EXT2_NULL
        : preferred_block - fs->superblock->first_data_block;
    u32 preferred_group = preferred_rel / fs->superblock->blocks_per_group;
    for (u32 n = 0; n < fs->num_groups; n++) {
        u32 bgid = (preferred_group + n) % fs->num_groups;
        BufferPage *bg_page;
        struct Ext2GroupDesc *bg_desc;
        ret = bg_get(fs, bgid, &bg_page, &bg_desc);
        if (ret != 0) goto exit;

        if (bg_desc->num_free_blocks == 0) {
            buffcache_rel(bg_page);
            continue;
        }

        u32 preferred_bit = n == 0
            ? preferred_rel % fs->superblock->blocks_per_group
            : 0;
        u32 bit;
        ret = bitmap_alloc(fs, bg_desc->block_bitmap,
                           group_num_blocks(fs, bgid), preferred_bit, &bit);
        if (ret == 0) {
            bg_desc->num_free_blocks--;
            fs->superblock->num_free_blocks--;
            block = fs->superblock->first_data_block +
                bgid * fs->superblock->blocks_per_group + bit;
            buffcache_mkdirty(bg_page);
            buffcache_mkdirty(fs->superblock_page);
        } else if (ret == ENOSPC) ret = EIO;

        // A block group whose free count is non-zero should never fail
        // allocation, so we interpret it as filesystem corruption and return an
        // I/O error

        buffcache_rel(bg_page);
        goto exit;
    }

    // Same story as before (I mean, bitmap_alloc)
    ret = ENOSPC;

exit:
    mutex_rel(&fs->alloc_lock);
    if (ret == 0) *out_block = block;
    return ret;
}

static int block_free(struct Ext2FS *fs, u32 block) {
    assert(block < fs->superblock->num_blocks);
    assert(block != EXT2_NULL);
    mutex_acq(&fs->alloc_lock);

    u32 rel = block - fs->superblock->first_data_block;
    u32 bpg = fs->superblock->blocks_per_group;
    u32 bgid = rel / bpg;
    BufferPage *bg_page;
    struct Ext2GroupDesc *bg_desc;
    int ret = bg_get(fs, bgid, &bg_page, &bg_desc);
    if (ret != 0) {
        mutex_rel(&fs->alloc_lock);
        return ret;
    }

    if (bg_desc->num_free_blocks == group_num_blocks(fs, bgid)) ret = EIO;
    else ret = bitmap_free(fs, bg_desc->block_bitmap, rel % bpg);
    if (ret != 0) goto exit;

    bg_desc->num_free_blocks++;
    fs->superblock->num_free_blocks++;
    buffcache_mkdirty(bg_page);
    buffcache_mkdirty(fs->superblock_page);

    // TODO: remove page from backing device (eviction optimization)

exit:
    mutex_rel(&fs->alloc_lock);
    buffcache_rel(bg_page);
    return ret;
}

// *out_depth is how many levels were left below the last pointer read, so a
// hole spans 1 << (ptr_shift * *out_depth) blocks
static int block_translate(struct Ext2FS *fs, u32 block_map[EXT2_NUM_BLOCKS],
                           u32 lblock, u32 *out_depth, u32 *out_pblock) {
    int ret = 0;
    u32 pblock = EXT2_NULL;
    u32 level = 0;

    if (lblock < EXT2_NUM_DIRECT) {
        pblock = block_map[lblock];
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

    pblock = block_map[EXT2_IND_BLOCK + depth - 1];
    if (pblock >= fs->superblock->num_blocks) {
        ret = EIO;
        goto exit;
    }

    for (level = depth; level > 0 && pblock != EXT2_NULL; level--) {
        u32 index = (lblock >> (ptr_shift * (level - 1))) & ptr_mask;
        ret = ptr_read(fs, pblock, index, &pblock);
        if (ret != 0) goto exit;
    }

exit:
    // ptr_read checks what it reads, but we read direct pointers ourselves
    if (ret == 0 && pblock >= fs->superblock->num_blocks) ret = EIO;
    if (out_depth != NULL) *out_depth = level;
    *out_pblock = pblock;
    return ret;
}

// First logical block mapped through block[EXT2_IND_BLOCK + depth - 1]
static u64 tree_base(u32 ptr_shift, u32 depth) {
    u64 base = depth > 0 ? EXT2_NUM_DIRECT : 0;
    for (u32 i = 1; i < depth; i++) base += 1ULL << (ptr_shift * i);
    return base;
}

// *out_pblock is the closest mapped block before lblock, or EXT2_NULL if there
// was no predecessor
static int block_get_best_predecessor(struct Ext2FS *fs,
                                      u32 block_map[EXT2_NUM_BLOCKS],
                                      u32 lblock, u32 *out_pblock) {
    u32 ptr_shift = fs->superblock->log_block_size + 8;
    u32 pblock = EXT2_NULL;
    int ret = 0;

    while (lblock > 0) {
        u32 depth;
        u32 prev = lblock - 1;
        ret = block_translate(fs, block_map, prev, &depth, &pblock);
        if (ret != 0 || pblock != EXT2_NULL) break;

        // Skip the whole hole. It is aligned to its size within its tree, and
        // the bases of deeper trees only differ by multiples of that size
        u64 span_mask = (1ULL << (ptr_shift * depth)) - 1;
        lblock = prev - ((prev - tree_base(ptr_shift, depth)) & span_mask);
    }

    *out_pblock = pblock;
    return ret;
}

static int block_allocz(struct Ext2FS *fs, u32 preferred_block,
                        u32 *out_block) {
    u32 block;
    int ret = block_alloc(fs, preferred_block, &block);
    if (ret != 0) return ret;

    BufferPage *page;
    ret = vfs_get_page(fs->vfs.backing, block, VFS_GETPAGE_NOFETCH, &page);
    if (ret != 0) {
        // We ignore the return value of this free, since we wouldn't be able to
        // propagate it anyway since we're on the error path of another error
        block_free(fs, block);
        return ret;
    }
    memset((void *)PHYS_TO_HHDM(buffcache_get_phys(page)), 0, fs->block_size);
    buffcache_mkdirty(page);
    buffcache_rel(page);

    *out_block = block;
    return 0;
}

static int bmap_fill(struct Ext2Node *node, u32 lblock, u32 preferred_block,
                     u32 *out_pblock) {
    struct Ext2Inode *inode = node->inode;
    struct Ext2FS *fs = fs_of(node->vnode.vfs);
    u32 ptr_shift = fs->superblock->log_block_size + 8;
    u32 ptr_mask = (1ULL << ptr_shift) - 1;
    u32 depth = 0;
    u32 *slot;

    if (lblock < EXT2_NUM_DIRECT) slot = &inode->block[lblock];
    else {
        u64 span = 1ULL << ptr_shift;
        lblock -= EXT2_NUM_DIRECT;
        for (depth = 1; lblock >= span; depth++, span <<= ptr_shift) {
            assert(depth < 3);
            lblock -= span;
        }
        slot = &inode->block[EXT2_IND_BLOCK + depth - 1];
    }

    // slot points into the block covered by slot_page. slot_page == NULL <==>
    // the block was in one of the 12 direct links
    BufferPage *slot_page = NULL;
    u32 pblock = EXT2_NULL;
    int ret = 0;
    for (u32 level = depth;; level--) {
        pblock = *slot;
        if (pblock == EXT2_NULL) {
            ret = block_allocz(fs, preferred_block, &pblock);
            if (ret != 0) break;
            *slot = pblock;
            inode->num_sectors += fs->block_size / 512;
            buffcache_mkdirty(node->inode_page);
            if (slot_page != NULL) buffcache_mkdirty(slot_page);
            preferred_block = pblock + 1 < fs->superblock->num_blocks
                ? pblock + 1
                : preferred_block;
        } else if (pblock >= fs->superblock->num_blocks) {
            ret = EIO;
            break;
        }

        if (slot_page != NULL) buffcache_rel(slot_page);
        slot_page = NULL;
        if (level == 0) break;

        // Here the slot page already exists and we don't need to allocate a new
        // one. This happens both at the first iterations in a tree that was
        // already partly built, or in all iterations of a tree that's just
        // missing a last-level pointer that is currently EXT2_NULL
        BufferPage *next;
        ret = vfs_get_page(fs->vfs.backing, pblock, 0, &next);
        if (ret != 0) break;
        slot_page = next;
        u32 *ptrs = (void *)PHYS_TO_HHDM(buffcache_get_phys(slot_page));
        slot = &ptrs[(lblock >> (ptr_shift * (level - 1))) & ptr_mask];
    }

    if (slot_page != NULL) buffcache_rel(slot_page);
    if (ret == 0) *out_pblock = pblock;
    return ret;
}

// *out_pblock == EXT2_NULL is a hole
static int bmap(struct Ext2Node *node, u32 lblock, bool alloc,
                u32 *out_pblock) {
    struct Ext2Inode *inode = node->inode;
    struct Ext2FS *fs = fs_of(node->vnode.vfs);

    u32 pblock = EXT2_NULL;
    int ret = block_translate(fs, inode->block, lblock, NULL, &pblock);
    if (ret != 0 || pblock != EXT2_NULL || !alloc) goto exit;

    u32 preferred_block;
    ret = block_get_best_predecessor(fs, inode->block, lblock,
                                     &preferred_block);
    if (ret != 0) goto exit;
    // If there's no predecessor, we try to keep the new block in near the one
    // in which the inode is held
    if (preferred_block == EXT2_NULL) {
        struct Ext2Superblock *superblock = fs->superblock;
        u32 bgid = (node->vnode.ino - 1) / superblock->inodes_per_group;
        preferred_block = superblock->first_data_block +
            bgid * superblock->blocks_per_group;
    }

    ret = bmap_fill(node, lblock, preferred_block, &pblock);

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
static int data_get_common(struct Ext2Node *dir, u32 lblock, bool write,
                           BufferPage **out_page, u8 **out_data) {
    struct Ext2FS *fs = fs_of(dir->vnode.vfs);
    u32 pblock;
    int ret = bmap(dir, lblock, write, &pblock);
    if (ret != 0) return ret;
    if (pblock == EXT2_NULL) return EIO;

    ret = vfs_get_page(fs->vfs.backing, pblock, write ? 0 : VFS_GETPAGE_RDONLY,
                       out_page);
    if (ret != 0) return ret;
    *out_data = (u8 *)PHYS_TO_HHDM(buffcache_get_phys(*out_page));
    return 0;
}

static int data_get(struct Ext2Node *dir, u32 lblock, BufferPage **out_page,
                    u8 **out_data) {
    return data_get_common(dir, lblock, false, out_page, out_data);
}

// Holes are allocated (zeroed), so appending a block to a directory is
// data_get_w on the block past the end
static int data_get_w(struct Ext2Node *dir, u32 lblock, BufferPage **out_page,
                      u8 **out_data) {
    return data_get_common(dir, lblock, true, out_page, out_data);
}

static int inode_alloc(struct Ext2FS *fs, u32 preferred_group, bool is_dir,
                       ino_t *out_ino) {
    assert(preferred_group < fs->num_groups);
    mutex_acq(&fs->alloc_lock);

    u32 found_group = 0;
    u32 bit = 0;
    BufferPage *bg_page;
    struct Ext2GroupDesc *bg_desc;
    int ret = 0;

    // This is the only place that can return ENOSPC, because the rest can
    // assume that there's at least 1 free inode, and that any allocation
    // failure is disk corruption and therefore EIO
    if (fs->superblock->num_free_inodes == 0) {
        ret = ENOSPC;
        goto exit;
    }

    if (is_dir) goto dir_check;
    ret = bg_get(fs, preferred_group, &bg_page, &bg_desc);
    if (ret != 0) goto exit;

    // We skip the preferred block even if it had space for another inode simply
    // because it had no space for its blocks, so we would end up with an inode
    // whose metadata is guaranteed to live in a different group than its data
    if (bg_desc->num_free_inodes != 0 && bg_desc->num_free_blocks != 0) {
        found_group = preferred_group;
        goto alloc;
    }

    buffcache_rel(bg_page);
    bg_page = NULL;
    bg_desc = NULL;

    // Our allocation policy differs between directories and other types of
    // files: a directory is the root of an independent tree, whereas regular
    // files are part of one of those trees. We try to keep
    // directories in those groups that have more than average free inodes, and
    // have as many free blocks as possible, which should then let subsequent
    // directory or file inode/block allocations in that directory to land on
    // the preferred group (because it was chosen among the ones that had more
    // space). For files the reasoning is reversed: since the preferred-group
    // path has failed, we place it in whatever group we find because wherever
    // that may be, it will still be slow to seek from the parent directory's
    // block

dir_check:
    if (is_dir) {
        usize avg_free_inodes = fs->superblock->num_free_inodes /
            fs->num_groups;
        BufferPage *bg_best_page = NULL;
        struct Ext2GroupDesc *bg_best_desc = NULL;
        for (u32 bgid = 0; bgid < fs->num_groups; bgid++) {
            ret = bg_get(fs, bgid, &bg_page, &bg_desc);
            if (ret != 0) {
                if (bg_best_page != NULL) buffcache_rel(bg_best_page);
                goto exit;
            }

            if (bg_desc->num_free_inodes != 0 &&
                bg_desc->num_free_inodes >= avg_free_inodes) {
                if (bg_best_page != NULL) {
                    if (bg_desc->num_free_blocks >
                        bg_best_desc->num_free_blocks) {
                        buffcache_rel(bg_best_page);
                        bg_best_page = bg_page;
                        bg_best_desc = bg_desc;
                        found_group = bgid;
                    } else buffcache_rel(bg_page);
                } else {
                    bg_best_page = bg_page;
                    bg_best_desc = bg_desc;
                    found_group = bgid;
                }
            } else buffcache_rel(bg_page);
        }

        bg_page = bg_best_page;
        bg_desc = bg_best_desc;
    } else {
        for (u32 n = 0; n < fs->num_groups; n++) {
            u32 bgid = (preferred_group + n) % fs->num_groups;
            ret = bg_get(fs, bgid, &bg_page, &bg_desc);
            if (ret != 0) goto exit;
            if (bg_desc->num_free_inodes != 0) {
                found_group = bgid;
                break;
            }
            buffcache_rel(bg_page);
            bg_page = NULL;
            bg_desc = NULL;
        }
    }

    if (bg_page == NULL) {
        ret = EIO;
        goto exit;
    }

alloc:
    assert(bg_desc->num_free_inodes != 0);
    // If the block group has a free inode, the allocation can only fail due
    // to disk corruption, so we always return EIO on ENOSPC
    ret = bitmap_alloc(fs, bg_desc->inode_bitmap,
                       fs->superblock->inodes_per_group, 0, &bit);
    if (ret != 0) {
        if (ret == ENOSPC) ret = EIO;
        buffcache_rel(bg_page);
    }

exit:
    // bg_page here is valid (and therefore, needs to be released) only if the
    // allocation was successful. This lets us handle all block group lookup
    // failures through this path as well
    if (ret == 0) {
        // In this case, the corruption is already deep so honestly I don't
        // think preserving coherency makes any sense
        ino_t ino = found_group * fs->superblock->inodes_per_group + bit + 1;
        if (ino >= fs->first_ino) *out_ino = ino;
        else ret = EIO;

        fs->superblock->num_free_inodes--;
        bg_desc->num_free_inodes--;
        bg_desc->num_dirs += is_dir;
        buffcache_mkdirty(fs->superblock_page);
        buffcache_mkdirty(bg_page);
        buffcache_rel(bg_page);
    }
    mutex_rel(&fs->alloc_lock);
    return ret;
}

static int inode_free(struct Ext2FS *fs, ino_t ino, bool is_dir) {
    assert(ino - 1 < fs->superblock->num_inodes);
    assert(ino >= fs->first_ino);
    assert(ino >= 1);

    BufferPage *bg_page;
    struct Ext2GroupDesc *bg_desc;
    u32 bgid = (ino - 1) / fs->superblock->inodes_per_group;
    u32 bit = (ino - 1) % fs->superblock->inodes_per_group;
    int ret = bg_get(fs, bgid, &bg_page, &bg_desc);
    if (ret != 0) return ret;

    mutex_acq(&fs->alloc_lock);

    if (fs->superblock->num_free_inodes == fs->superblock->num_inodes ||
        bg_desc->num_free_inodes == fs->superblock->inodes_per_group ||
        (is_dir && bg_desc->num_dirs == 0)) {
        ret = EIO;
        goto exit;
    }

    ret = bitmap_free(fs, bg_desc->inode_bitmap, bit);
    if (ret != 0) goto exit;

    bg_desc->num_free_inodes++;
    bg_desc->num_dirs -= is_dir;
    fs->superblock->num_free_inodes++;
    buffcache_mkdirty(bg_page);
    buffcache_mkdirty(fs->superblock_page);

exit:
    mutex_rel(&fs->alloc_lock);
    buffcache_rel(bg_page);
    return ret;
}

static int inode_alloc_initialized(struct Ext2FS *fs, u32 preferred_group,
                                   bool is_dir, VnodeAttr *attr, ino_t *out_ino,
                                   BufferPage **out_page,
                                   struct Ext2Inode **out_desc) {
    u16 mode;
    int ret = mode_of_type(attr->type, &mode);
    if (ret != 0) return ret;

    ino_t ino;
    ret = inode_alloc(fs, preferred_group, is_dir, &ino);
    if (ret != 0) return ret;

    BufferPage *ino_page;
    struct Ext2Inode *ino_desc;
    ret = inode_get(fs, ino, &ino_page, &ino_desc);
    if (ret != 0) {
        buffcache_rel(ino_page);
        return ret;
    }

    memset(ino_desc, 0, fs->inode_size);
    ino_desc->mode = mode | (attr->mode & 07777);
    ino_desc->num_links = 1;

    // TODO: handle time stamps

    buffcache_mkdirty(ino_page);
    *out_ino = ino;
    *out_page = ino_page;
    *out_desc = ino_desc;
    return 0;
}

static usize dirent_rec_size(usize namelen) {
    return (sizeof(struct Ext2DirEnt) + namelen + 3) & ~(usize)3;
}

static bool dirent_validate(struct Ext2DirEnt *entry, usize pos,
                            usize block_size) {
    return entry->reclen >= sizeof(struct Ext2DirEnt) &&
        entry->reclen % 4 == 0 && pos + entry->reclen <= block_size &&
        sizeof(struct Ext2DirEnt) + entry->namelen <= entry->reclen;
}

// Inode number 0 does not exist, so we skip it because it is either padding or
// deleted. *out_entry and *out_prev point into *out_page, released with
// buffcache_rel. *out_prev is NULL if the entry is the first in its block
static int dirent_find(struct Ext2Node *dir, const char *name, usize namelen,
                       BufferPage **out_page, struct Ext2DirEnt **out_entry,
                       struct Ext2DirEnt **out_prev) {
    struct Ext2FS *fs = fs_of(dir->vnode.vfs);
    u32 num_blocks = dir->inode->size / fs->block_size;

    for (u32 lblock = 0; lblock < num_blocks; lblock++) {
        BufferPage *page;
        u8 *data;
        int ret = data_get(dir, lblock, &page, &data);
        if (ret != 0) return ret;

        struct Ext2DirEnt *prev = NULL;
        for (usize pos = 0; pos < fs->block_size;) {
            struct Ext2DirEnt *entry = (void *)(data + pos);
            if (!dirent_validate(entry, pos, fs->block_size)) {
                buffcache_rel(page);
                return EIO;
            }
            if (entry->ino != 0 && entry->namelen == namelen &&
                memcmp(entry->name, name, namelen) == 0) {
                *out_page = page;
                *out_entry = entry;
                if (out_prev != NULL) *out_prev = prev;
                return 0;
            }
            prev = entry;
            pos += entry->reclen;
        }

        buffcache_rel(page);
    }

    return ENOENT;
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
    *out_vnode = NULL;
    BufferPage *page;
    struct Ext2DirEnt *entry;
    int ret = dirent_find(extnode_of(dir), name, namelen, &page, &entry, NULL);
    if (ret != 0) return ret;

    ino_t ino = entry->ino;
    buffcache_rel(page);
    return vfs_vget(dir->vfs, ino, out_vnode);
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
                hdr.type = dirent_type(fs, entry->type);
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

    // Fast symlinks keep the target in the block pointers, but an xattr block
    // still counts towards num_sectors
    u32 ea_sectors = node->inode->file_acl != 0 ? fs->block_size / 512 : 0;
    if (node->inode->num_sectors == ea_sectors) {
        if (size > sizeof(node->inode->fast_link)) return EIO;
        memcpy(buf, node->inode->fast_link, min(buflen, size));
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
    rwlock_acq_read(&node->file_lock);
    u64 size = file_size(node->inode);
    buflen = off < size ? min(buflen, size - off) : 0;

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

    rwlock_rel_read(&node->file_lock);
    return *out_bytes_read != 0 ? 0 : ret;
}

static u64 max_file_size(struct Ext2FS *fs) {
    struct Ext2Superblock *superblock = fs->superblock;
    u32 ptr_shift = superblock->log_block_size + 8;
    u64 num_ptrs = 1ULL << ptr_shift;
    u64 max_blocks = tree_base(ptr_shift, 4);

    // num_sectors is 32 bits wide and also counts the indirect blocks, which
    // are at most one every num_ptrs - 1 data blocks plus the 3 tree roots
    u64 sector_blocks = 0xFFFFFFFFULL / (fs->block_size / 512);
    max_blocks = min(max_blocks,
                     (sector_blocks - 3) / num_ptrs * (num_ptrs - 1));
    u64 max_size = max_blocks * fs->block_size;

    // Without large_file, size_high is dir_acl
    if (superblock->rev == EXT2_GOOD_OLD_REV)
        return min(max_size, 0x7FFFFFFFULL);
    return max_size;
}

// LOCKING: node->file_lock held in write mode
static void file_set_size(struct Ext2Node *node, u64 size) {
    struct Ext2FS *fs = fs_of(node->vnode.vfs);
    struct Ext2Superblock *superblock = fs->superblock;
    node->inode->size = size;
    node->inode->size_high = size >> 32;
    buffcache_mkdirty(node->inode_page);

    // Older drivers would truncate files past 2 GiB without this
    if (size <= 0x7FFFFFFF) return;
    mutex_acq(&fs->alloc_lock);
    if (!(superblock->feature_ro_compat & EXT2_FEATURE_RO_COMPAT_LARGE_FILE)) {
        superblock->feature_ro_compat |= EXT2_FEATURE_RO_COMPAT_LARGE_FILE;
        buffcache_mkdirty(fs->superblock_page);
    }
    mutex_rel(&fs->alloc_lock);
}

static int ext2_write(Vnode *vnode, void *buf, usize buflen, u64 off, u64 flags,
                      usize *out_bytes_written) {
    (void)flags;
    struct Ext2Node *node = extnode_of(vnode);
    struct Ext2FS *fs = fs_of(vnode->vfs);
    assert(fs->block_size == PAGE_SIZE);
    *out_bytes_written = 0;

    int ret = check_file_io(vnode);
    if (ret != 0) return ret;
    if (buflen == 0) return 0;

    u64 max_size = max_file_size(fs);
    if (off >= max_size) return EFBIG;
    buflen = min(buflen, max_size - off);

    rwlock_acq_write(&node->file_lock);
    usize done = 0;
    while (done < buflen) {
        if (signal_check() != SIGNAL_NONE) {
            ret = EINTR;
            break;
        }

        // Partial writes need the old contents, new blocks are already zeroed
        u64 pos = off + done;
        usize page_off = pos % PAGE_SIZE;
        usize len = min(PAGE_SIZE - page_off, buflen - done);
        u32 pblock;
        ret = bmap(node, pos / PAGE_SIZE, true, &pblock);
        if (ret != 0) break;

        BufferPage *page;
        ret = vfs_get_page(fs->vfs.backing, pblock, 0, &page);
        if (ret != 0) break;
        u8 *data = (void *)PHYS_TO_HHDM(buffcache_get_phys(page));
        ret = memcpy_maybe_user(data + page_off, (u8 *)buf + done, len);
        // A faulting copy may still have changed part of the page
        buffcache_mkdirty(page);
        buffcache_rel(page);
        if (ret != 0) break;

        done += len;
    }

    // TODO: blocks allocated past the new size on failure are left behind
    // until truncate exists
    if (done != 0) {
        if (off + done > file_size(node->inode))
            file_set_size(node, off + done);

        u32 now = cpu_get_time() / NANOS_PER_SEC;
        node->inode->mtime = now;
        node->inode->ctime = now;
        buffcache_mkdirty(node->inode_page);
    }

    rwlock_rel_write(&node->file_lock);
    *out_bytes_written = done;
    return done != 0 ? 0 : ret;
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
    out_stat->st_size = file_size(inode);
    out_stat->st_blksize = fs_of(vnode->vfs)->block_size;
    out_stat->st_blocks = inode->num_sectors;
    out_stat->st_atim.tv_sec = inode->atime;
    out_stat->st_mtim.tv_sec = inode->mtime;
    out_stat->st_ctim.tv_sec = inode->ctime;
    if (vnode->type == VNODE_TYPE_CHARDEV ||
        vnode->type == VNODE_TYPE_BLOCKDEV) {
        out_stat->st_rdev = inode->rdev;
    }

    return 0;
}

static VnodeOps g_ext2_node_ops = {.inactive = ext2_inactive,
                                   .lookup = ext2_lookup,
                                   .readdir = ext2_readdir,
                                   .readlink = ext2_readlink,
                                   .read = ext2_read,
                                   .write = ext2_write,
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
    if (superblock->rev > EXT2_DYNAMIC_REV) return false;
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
    if (superblock->num_inodes > max_inodes) return false;
    if (superblock->rev == EXT2_GOOD_OLD_REV) return true;

    // We use a power of two because it ensures that an inode structure never
    // straddles a page
    u16 inode_size = superblock->inode_size;
    if (inode_size < EXT2_GOOD_OLD_INODE_SIZE || inode_size > PAGE_SIZE ||
        (inode_size & (inode_size - 1)) != 0) {
        return false;
    }

    if ((superblock->feature_incompat & ~EXT2_FEATURE_INCOMPAT_SUPP) != 0) {
        return false;
    }

    return (superblock->feature_ro_compat & ~EXT2_FEATURE_RO_COMPAT_SUPP) == 0;
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

    rwlock_init(&node->file_lock);
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
    mutex_init(&fs->alloc_lock);
    fs->vfs.backing = backing;
    fs->superblock = superblock;
    fs->superblock_page = superblock_page;
    fs->block_size = 1024 << superblock->log_block_size;
    fs->num_groups = sb_num_groups(superblock);
    fs->inode_size = superblock->rev == EXT2_GOOD_OLD_REV
        ? EXT2_GOOD_OLD_INODE_SIZE
        : superblock->inode_size;
    fs->first_ino = superblock->rev == EXT2_GOOD_OLD_REV
        ? EXT2_GOOD_OLD_FIRST_INO
        : superblock->first_ino;
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
