#include <arch/info.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/blockdev.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/buffcache.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/util.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <vendor/printf.h>

#define MBR_ENTRIES_OFF  446
#define MBR_NUM_ENTRIES  4
#define MBR_TYPE_GPT     0xEE
#define GPT_SIGNATURE    "EFI PART"
#define GPT_MIN_HDR_SIZE 92
#define GPT_MAX_ENTRIES  1024

// We use this for both a device in its strictest sense (whole device, start =
// 0) and for each partition
struct BlockDev {
    DevOps *devops;
    void *devdata;
    u64 start;
    u64 size;
    u32 lba_size;
};

struct MBREntry {
    u8 status;
    u8 chs_first[3];
    u8 type;
    u8 chs_last[3];
    u32 first_lba;
    u32 num_lbas;
} packed;

struct GPTHeader {
    char signature[8];
    u32 revision;
    u32 header_size;
    u32 header_crc;
    u32 reserved;
    u64 my_lba;
    u64 alt_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8 disk_guid[16];
    u64 entries_lba;
    u32 num_entries;
    u32 entry_size;
    u32 entries_crc;
} packed;

struct GPTEntry {
    u8 type_guid[16];
    u8 part_guid[16];
    u64 first_lba;
    u64 last_lba; // inclusive
    u64 attrs;
    u16 name[36];
} packed;

_Static_assert(sizeof(struct MBREntry) == 16, "");
_Static_assert(sizeof(struct GPTHeader) == GPT_MIN_HDR_SIZE, "");
_Static_assert(sizeof(struct GPTEntry) == 128, "");

// PARTITION DEVICE OPERATIONS

// Partitions start on a page boundary, so their pages are the disk's pages and
// the two share the disk's buffer cache

static int part_read(void *buf, usize buflen, void *devdata, u64 off, u64 flags,
                     usize *out_bytes_read) {
    struct BlockDev *part = devdata;
    *out_bytes_read = 0;
    if (off >= part->size) return 0;
    buflen = min(buflen, part->size - off);
    return part->devops->read(buf, buflen, part->devdata, part->start + off,
                              flags, out_bytes_read);
}

static int part_write(void *devdata, void *buf, usize buflen, u64 off,
                      u64 flags, usize *out_bytes_written) {
    struct BlockDev *part = devdata;
    *out_bytes_written = 0;
    if (off >= part->size) return 0;
    buflen = min(buflen, part->size - off);
    return part->devops->write(part->devdata, buf, buflen, part->start + off,
                               flags, out_bytes_written);
}

static int part_get_page(void *devdata, usize index, int flags,
                         BufferPage **out_page) {
    struct BlockDev *part = devdata;
    if (index >= (part->size + PAGE_SIZE - 1) / PAGE_SIZE) return ERANGE;
    return part->devops->get_page(
        part->devdata, part->start / PAGE_SIZE + index, flags, out_page);
}

static int part_stat(void *devdata, struct stat *out_stat) {
    struct BlockDev *part = devdata;
    int ret = part->devops->stat(part->devdata, out_stat);
    if (ret != 0) return ret;
    out_stat->st_size = part->size;
    out_stat->st_blocks = part->size / part->lba_size;
    return 0;
}

static int part_ioctl(void *devdata, u64 op, void *buf) {
    struct BlockDev *part = devdata;
    return blockdev_ioctl(part->size, part->lba_size, op, buf);
}

static DevOps g_part_dev_ops = {.type = VNODE_TYPE_BLOCKDEV,
                                .read = part_read,
                                .write = part_write,
                                .get_page = part_get_page,
                                .ioctl = part_ioctl,
                                .stat = part_stat};

// PARTITION TABLE SCANNING

static u32 crc32_update(u32 crc, const u8 *data, usize len) {
    for (usize i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}

// *out_data points into *out_page, released with buffcache_rel
static int disk_map(struct BlockDev *disk, u64 off, usize len,
                    BufferPage **out_page, u8 **out_data) {
    if (off > disk->size || len > disk->size - off) return EIO;
    if (off % PAGE_SIZE + len > PAGE_SIZE) return EIO;

    BufferPage *page;
    int ret = disk->devops->get_page(disk->devdata, off / PAGE_SIZE,
                                     VFS_GETPAGE_RDONLY, &page);
    if (ret != 0) return ret;
    *out_page = page;
    *out_data = (u8 *)PHYS_TO_HHDM(buffcache_get_phys(page)) + off % PAGE_SIZE;
    return 0;
}

static void part_add(struct BlockDev *disk, const char *name, usize namelen,
                     u32 num, u64 first_lba, u64 num_lbas) {
    u64 disk_lbas = disk->size / disk->lba_size;
    if (num_lbas == 0 || first_lba >= disk_lbas ||
        num_lbas > disk_lbas - first_lba) {
        log_debug("%.*s: partition %u is out of range", (int)namelen, name,
                  num);
        return;
    }

    u64 start = first_lba * disk->lba_size;
    if (start % PAGE_SIZE != 0) {
        log_debug("%.*s: partition %u is not page-aligned", (int)namelen, name,
                  num);
        return;
    }

    struct BlockDev *part = slab_alloc(sizeof(struct BlockDev));
    if (part == NULL) return;
    *part = *disk;
    part->start = start;
    part->size = num_lbas * disk->lba_size;

    char partname[64];
    int partnamelen = snprintf(partname, sizeof(partname), "%.*sp%u",
                               (int)namelen, name, num);
    if (partnamelen < 0 || (usize)partnamelen >= sizeof(partname) ||
        devfs_register(NULL, partname, partnamelen, &g_part_dev_ops, part,
                       NULL) != 0) {
        slab_free(part, sizeof(struct BlockDev));
        return;
    }

    log("%s: %lu bytes at offset %lu", partname, part->size, part->start);
}

static bool guid_is_null(const u8 *guid) {
    for (usize i = 0; i < 16; i++)
        if (guid[i] != 0) return false;
    return true;
}

static int gpt_entries_crc(struct BlockDev *disk, struct GPTHeader *hdr,
                           u32 *out_crc) {
    u32 crc = ~0u;
    for (u32 i = 0; i < hdr->num_entries; i++) {
        BufferPage *page;
        u8 *data;
        u64 off = hdr->entries_lba * disk->lba_size + (u64)i * hdr->entry_size;
        int ret = disk_map(disk, off, hdr->entry_size, &page, &data);
        if (ret != 0) return ret;
        crc = crc32_update(crc, data, hdr->entry_size);
        buffcache_rel(page);
    }

    *out_crc = ~crc;
    return 0;
}

static void gpt_scan(struct BlockDev *disk, const char *name, usize namelen) {
    BufferPage *page;
    u8 *data;
    if (disk_map(disk, disk->lba_size, disk->lba_size, &page, &data) != 0)
        return;

    struct GPTHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    bool valid = memcmp(hdr.signature, GPT_SIGNATURE, 8) == 0 &&
        hdr.header_size >= GPT_MIN_HDR_SIZE &&
        hdr.header_size <= disk->lba_size;
    if (valid) {
        // The CRC is computed with the CRC field itself zeroed
        u32 zero = 0;
        u32 crc = crc32_update(~0u, data,
                               offsetof(struct GPTHeader, header_crc));
        crc = crc32_update(crc, (u8 *)&zero, sizeof(zero));
        crc = crc32_update(crc, data + offsetof(struct GPTHeader, reserved),
                           hdr.header_size -
                               offsetof(struct GPTHeader, reserved));
        valid = ~crc == hdr.header_crc;
    }
    buffcache_rel(page);

    // Entries are 128 << n bytes, so a power of two no larger than a page never
    // crosses a page boundary
    if (!valid || hdr.my_lba != 1 || hdr.entry_size < sizeof(struct GPTEntry) ||
        hdr.entry_size > PAGE_SIZE ||
        (hdr.entry_size & (hdr.entry_size - 1)) != 0 ||
        hdr.num_entries > GPT_MAX_ENTRIES) {
        log_debug("%.*s: invalid GPT header", (int)namelen, name);
        return;
    }

    u32 entries_crc;
    if (gpt_entries_crc(disk, &hdr, &entries_crc) != 0 ||
        entries_crc != hdr.entries_crc) {
        log_debug("%.*s: invalid GPT partition entries", (int)namelen, name);
        return;
    }

    for (u32 i = 0; i < hdr.num_entries; i++) {
        u64 off = hdr.entries_lba * disk->lba_size + (u64)i * hdr.entry_size;
        if (disk_map(disk, off, sizeof(struct GPTEntry), &page, &data) != 0)
            return;

        struct GPTEntry entry;
        memcpy(&entry, data, sizeof(entry));
        buffcache_rel(page);

        if (guid_is_null(entry.type_guid)) continue;
        if (entry.last_lba < entry.first_lba) continue;
        part_add(disk, name, namelen, i + 1, entry.first_lba,
                 entry.last_lba - entry.first_lba + 1);
    }
}

static void mbr_scan(struct BlockDev *disk, const char *name, usize namelen) {
    BufferPage *page;
    u8 *data;
    if (disk_map(disk, 0, 512, &page, &data) != 0) return;

    struct MBREntry entries[MBR_NUM_ENTRIES];
    memcpy(entries, data + MBR_ENTRIES_OFF, sizeof(entries));
    bool signature = data[510] == 0x55 && data[511] == 0xAA;
    buffcache_rel(page);

    // Unpartitioned media
    if (!signature) return;

    // A FAT boot sector also has the signature, but its "status" bytes are
    // usually neither 0x00 nor 0x80, because we like heuristics, don't we
    bool gpt = false;
    for (usize i = 0; i < MBR_NUM_ENTRIES; i++) {
        if ((entries[i].status & 0x7F) != 0) return;
        if (entries[i].type == MBR_TYPE_GPT) gpt = true;
    }

    if (gpt) {
        gpt_scan(disk, name, namelen);
        return;
    }

    // Logical partitions inside extended ones are not supported
    for (usize i = 0; i < MBR_NUM_ENTRIES; i++) {
        u8 type = entries[i].type;
        if (type == 0x00 || type == 0x05 || type == 0x0F || type == 0x85)
            continue;
        part_add(disk, name, namelen, i + 1, entries[i].first_lba,
                 entries[i].num_lbas);
    }
}

// INTERFACE

int blockdev_ioctl(u64 size, u32 lba_size, u64 op, void *buf) {
    if (op == BLKGETSIZE64) return memcpy_to_user(buf, &size, sizeof(size));

    if (op == BLKSSZGET) {
        int ssz = lba_size;
        return memcpy_to_user(buf, &ssz, sizeof(ssz));
    }

    return ENOSYS;
}

int blockdev_register(const char *name, usize namelen, DevOps *devops,
                      void *devdata, u64 size, u32 lba_size) {
    assert(devops->type == VNODE_TYPE_BLOCKDEV);
    assert(devops->read != NULL && devops->write != NULL &&
           devops->get_page != NULL && devops->stat != NULL);
    assert(lba_size >= 512 && lba_size <= PAGE_SIZE &&
           (lba_size & (lba_size - 1)) == 0);

    int ret = devfs_register(NULL, name, namelen, devops, devdata, NULL);
    if (ret != 0) return ret;

    // Partitions are optional, so scanning never fails registration
    struct BlockDev disk = {.devops = devops,
                            .devdata = devdata,
                            .start = 0,
                            .size = size,
                            .lba_size = lba_size};
    mbr_scan(&disk, name, namelen);
    return 0;
}
