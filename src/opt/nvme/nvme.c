#include <arch/barrier.h>
#include <arch/info.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/opt/nvme.h>
#include <kernel/opt/pci.h>

// TAKEN: vloxei64/ke
// CREDIT: vloxei64/ke

#define NVME_LOG(fmt, ...) log_opt("NVME", fmt "\n", __VA_ARGS__)
#define NVME_PANIC()       panic(NULL, "nvme error")

#define NVME_REG64_CAP             0x00
#define NVME_REG32_VS              0x08
#define NVME_REG32_INTMS           0x0C
#define NVME_REG32_INTMC           0x10
#define NVME_REG32_CC              0x14
#define NVME_REG32_CC_EN           0b1
#define NVME_REG32_CC_IOSQES_SHIFT 16
#define NVME_REG32_CC_IOCQES_SHIFT 20
#define NVME_REG32_CSTS            0x1C
#define NVME_REG32_CSTS_RDY        0b1
#define NVME_REG32_AQA             0x24
#define NVME_REG64_ASQ             0x28
#define NVME_REG64_ACQ             0x30

#define NVME_OPCODE_ADMIN_CREATE_SQ 0x01
#define NVME_OPCODE_ADMIN_CREATE_CQ 0x05
#define NVME_OPCODE_FLUSH           0x00
#define NVME_OPCODE_WRITE           0x01
#define NVME_OPCODE_READ            0x02

#define NVME_CQ_CREATE_FLAGS_PC  1
#define NVME_CQ_CREATE_FLAGS_IEN 2

#define NVME_SQ_CREATE_FLAGS_PC 1

// sq = submission queue

struct NVMeIdentifySQE {
    u8 opcode;
    u8 sqe_flags;
    u16 cid;
    u32 nsid;
    u8 _rsv[16];
    u64 prp1;
    u64 prp2;
    u32 cns;
    u8 _rsv2[20];
};

struct NVMeIdentifyNSLBA {
    u16 ms;
    u8 ds;
    u8 rp;
};

struct NVMeIdentifyNS {
    u64 nsze;
    u64 ncap;
    u64 nuse;
    u8 nsfeat;
    u8 nlbaf;
    u8 flbas;
    u8 mc;
    u8 dpc;
    u8 dps;
    u8 _rsv[98];
    struct NVMeIdentifyNSLBA lbaf[16];
};

struct NVMeCreateCQSQE {
    u8 opcode;
    u8 sqe_flags;
    u16 cid;
    u8 _rsv[20];
    u64 prp1;
    u64 _rsv2;
    u16 qid;
    u16 qsize;
    u16 flags; // IEN and PC
    u8 iv;
    u8 _rsv3[16];
};

struct NVMeCreateSQSQE {
    u8 opcode;
    u8 sqe_flags;
    u16 cid;
    u8 _rsv[20];
    u64 prp1;
    u64 _rsvd2;
    u16 qid;
    u16 qsize;
    u16 flags; // QPRIO and PC
    u16 cqid;
    u8 _rsv3[16];
};

struct NVMeRWSQE {
    u8 opcode;
    u8 sqe_flags;
    u16 cid;
    u32 nsid;
    u64 _rsvd1;
    u64 metadata;
    u64 prp1;
    u64 prp2;
    u64 slba;
    u32 cdw12; // NOTE: NLB is a 0-based value (ie value-1)
    u32 cdw13; // dataset management
    u32 eilbrt;
    u16 elbatm;
    u16 elbat;
};

struct NVMeQueue {
    u8 *sq;
    u8 *cq;
    u64 qe;
    u64 sq_doorbell;
    u64 cq_doorbell;
    u32 sq_idx;
    u32 cq_idx;
    u32 qid;
    bool cq_phase;
    Waitlist irq_waiters;
    Mutex lock;
};

struct NVMeDevice {
    u64 num_blocks;
    u64 size;
    u32 blocks_shift;
    struct NVMeQueue queues[1]; // 1 for now, 64 in general
};

// Used to generate /dev/nvme%zu
static usize g_next_drive_id = 0;

static inline u32 nvme_read32(u64 base) {
    return *(volatile u32 *)(PHYS_TO_HHDM(base));
}

static inline void nvme_write32(u64 base, u32 val) {
    *(volatile u32 *)(PHYS_TO_HHDM(base)) = val;
}

static inline u64 nvme_read64(u64 base) {
    return *(volatile u64 *)(PHYS_TO_HHDM(base));
}

static inline void nvme_write64(u64 base, u64 val) {
    *(volatile u64 *)(PHYS_TO_HHDM(base)) = val;
}

static bool nvme_queue_done_nobarrier(struct NVMeQueue *q) {
    return ((((volatile u32 *)(q->cq + q->cq_idx * 16))[3] >> 16) & 1) !=
        q->cq_phase;
}

static bool nvme_queue_done(struct NVMeQueue *q) {
    barrier_generic_read();
    return nvme_queue_done_nobarrier(q);
}

static void nvme_submit_and_wait(struct NVMeQueue *q, void *in) {
    memcpy(q->sq + q->sq_idx * 64, in, 64);
    q->sq_idx++;
    if (q->sq_idx == q->qe) q->sq_idx = 0;
    barrier_generic_write();
    nvme_write32(q->sq_doorbell, q->sq_idx);

    while (!nvme_queue_done_nobarrier(q));
    barrier_generic_read();

    q->cq_idx++;
    if (q->cq_idx == q->qe) {
        q->cq_idx = 0;
        q->cq_phase ^= 1;
    }
    nvme_write32(q->cq_doorbell, q->cq_idx);
}

static void nvme_submit(struct NVMeQueue *q, void *in) {
    memcpy(q->sq + q->sq_idx * 64, in, 64);
    q->sq_idx++;
    if (q->sq_idx == q->qe) q->sq_idx = 0;
    barrier_generic_write();
    nvme_write32(q->sq_doorbell, q->sq_idx);
}

static void nvme_submit_page_command_sync(struct NVMeDevice *drive, void *buf,
                                          usize pageno, u8 opcode) {
    u64 lba = pageno << (12 - drive->blocks_shift);
    u64 count = 1 << (12 - drive->blocks_shift);
    struct NVMeRWSQE rw_in = {.opcode = opcode,
                              .nsid = 1,
                              .prp1 = (u64)buf,
                              .slba = lba,
                              .cdw12 = count - 1};
    mutex_acq(&drive->queues[0].lock);
    nvme_submit(&drive->queues[0], &rw_in);

    while (!nvme_queue_done_nobarrier(&drive->queues[0])) {
        mutex_wait(&drive->queues[0].lock, &drive->queues[0].irq_waiters, 0,
                   true);
    }
    barrier_generic_read();

    drive->queues[0].cq_idx++;
    if (drive->queues[0].cq_idx == drive->queues[0].qe) {
        drive->queues[0].cq_idx = 0;
        drive->queues[0].cq_phase ^= 1;
    }
    nvme_write32(drive->queues[0].cq_doorbell, drive->queues[0].cq_idx);

    mutex_rel(&drive->queues[0].lock);
}

static void nvme_submit_multipage_command_sync(struct NVMeDevice *drive,
                                               void *buf, usize pageno,
                                               usize num_pages, u8 opcode) {
    for (usize i = 0; i < num_pages; i++) {
        // DMA does not trigger page faults, so we must do it ourselves if this
        // page was not writable or COW or similar. This value will be
        // overridden by DMA anyway
        if (opcode == NVME_OPCODE_READ) *(u8 *)buf = 0;

        nvme_submit_page_command_sync(drive,
                                      (buf > MMU_KERNELSPACE_START)
                                          ? (void *)HHDM_TO_PHYS(buf)
                                          : vmm_get_physical(NULL, buf),
                                      pageno + i, opcode);
        buf += PAGE_SIZE;
    }
}

static void nvme_isr(InterruptHandler *isr, Context *ctx) {
    (void)ctx;
    struct NVMeDevice *drive = isr->extra;
    sched_notify(&drive->queues[0].irq_waiters);
    sched_yield();
}

// /dev/nvme%z DEVICE OPERATIONS

// TODO: fix the most terrible read implementation ever
static int nvme_read(void *buf, usize buflen, void *devdata, u64 off, u64 flags,
                     usize *out_bytes_read) {
    struct NVMeDevice *device = devdata;

    if (off >= device->size) {
        *out_bytes_read = 0;
        return 0;
    }

    if (off + buflen >= device->size) buflen = device->size - off;

    if (buflen == 0) {
        *out_bytes_read = 0;
        return 0;
    }

    usize base_page = off / PAGE_SIZE;
    usize rem = off % PAGE_SIZE;
    usize rem_buflen = buflen;

    // Fast path if the buffer is aligned
    if (rem == 0 && buflen % PAGE_SIZE == 0 && (uptr)buf % PAGE_SIZE == 0) {
        nvme_submit_multipage_command_sync(
            device, buf, base_page, buflen / PAGE_SIZE, NVME_OPCODE_READ);
        *out_bytes_read = buflen;
        return 0;
    }

    // Other fast path: the buffe and length may not be aligned, but
    // the buffer is long enough to contain at least one full physical page. In
    // this case, we can do the same as above, but only for  those full pages.
    // If buflen >= 2 pages, we know that the buffer must have the following
    // structure: <unaligned base> [<aligned pages>] [<unaligned tail>]. This
    // means we have to load in the base and tail separately
    if (rem == 0 && buflen >= 2 * PAGE_SIZE) {
        void *rounded_buf = (void *)(((uptr)buf + PAGE_SIZE - 1) &
                                     ~(uptr)(PAGE_SIZE - 1));
        void *rounded_last = (void *)(((uptr)buf + buflen) &
                                      ~(uptr)(PAGE_SIZE - 1));
        usize rounded_buflen = (uptr)(rounded_last - rounded_buf);
        usize rounded_pages = rounded_buflen / PAGE_SIZE;
        usize head_size = (uptr)rounded_buf - (uptr)buf;
        usize tail_size = (uptr)buf + buflen - (uptr)rounded_last;
        // Read the contiguous block
        nvme_submit_multipage_command_sync(
            device, rounded_buf, base_page + ((buf != rounded_buf) ? 1 : 0),
            rounded_pages, NVME_OPCODE_READ);
        // Read base and tail
        void *tmp_phys = pmm_alloc();
        void *tmp_hhdm = (void *)PHYS_TO_HHDM(tmp_phys);
        // No need to copy base/head if the buffer was aligned because we
        // already did before
        if (buf != rounded_buf) {
            nvme_submit_page_command_sync(device, tmp_phys, base_page,
                                          NVME_OPCODE_READ);
            memcpy(buf, tmp_hhdm, head_size);
        }
        // Tail must always be copied however
        nvme_submit_page_command_sync(
            device, tmp_phys, base_page + rounded_pages, NVME_OPCODE_READ);
        memcpy(rounded_last, tmp_hhdm, tail_size);
        pmm_free(tmp_phys);
        *out_bytes_read = buflen;
        return 0;
    }

    void *tmp_phys = pmm_alloc();
    void *tmp_hhdm = (void *)PHYS_TO_HHDM(tmp_phys);

    for (usize i = 0; i < (buflen + PAGE_SIZE - 1) / PAGE_SIZE; i++) {
        usize read_size = min(PAGE_SIZE - rem, rem_buflen);
        nvme_submit_page_command_sync(device, tmp_phys, base_page + i,
                                      NVME_OPCODE_READ);
        memcpy(buf, tmp_hhdm + rem, read_size);
        buf += read_size;
        *out_bytes_read += read_size;
        rem_buflen -= read_size;
        rem = 0;
    }

    pmm_free(tmp_phys);
    return 0;
}

static int nvme_write(void *devdata, void *buf, usize buflen, u64 off,
                      u64 flags, usize *out_bytes_written) {
    struct NVMeDevice *device = devdata;

    if (off >= device->size) {
        *out_bytes_written = 0;
        return 0;
    }

    if (off + buflen >= device->size) buflen = device->size - off;

    if (buflen == 0) {
        *out_bytes_written = 0;
        return 0;
    }

    usize base_page = off / PAGE_SIZE;
    usize rem = off % PAGE_SIZE;
    usize rem_buflen = buflen;

    // Fast path if the buffer is aligned
    if (rem == 0 && buflen % PAGE_SIZE == 0 && (uptr)buf % PAGE_SIZE == 0) {
        nvme_submit_multipage_command_sync(
            device, buf, base_page, buflen / PAGE_SIZE, NVME_OPCODE_WRITE);
        *out_bytes_written = buflen;
        return 0;
    }

    void *tmp_phys = pmm_alloc();
    void *tmp_hhdm = (void *)PHYS_TO_HHDM(tmp_phys);

    for (usize i = 0; i < (buflen + PAGE_SIZE - 1) / PAGE_SIZE; i++) {
        usize read_size = min(PAGE_SIZE - rem, rem_buflen);
        nvme_submit_page_command_sync(device, tmp_phys, base_page + i,
                                      NVME_OPCODE_READ);
        memcpy(tmp_hhdm + rem, buf, read_size);
        nvme_submit_page_command_sync(device, tmp_phys, base_page + i,
                                      NVME_OPCODE_WRITE);
        buf += read_size;
        *out_bytes_written += read_size;
        rem_buflen -= read_size;
        rem = 0;
    }

    pmm_free(tmp_phys);
    return 0;
}

static int nvme_stat(void *devdata, struct stat *out_stat) {
    struct NVMeDevice *drive = devdata;
    out_stat->st_blksize = 1 << drive->blocks_shift;
    out_stat->st_mode = 0777;
    out_stat->st_blocks = drive->num_blocks;
    out_stat->st_size = drive->size;
    return 0;
}

static DevOps g_nvme_dev_ops = {.type = VNODE_TYPE_BLOCKDEV,
                                .read = nvme_read,
                                .write = nvme_write,
                                .stat = nvme_stat};

static int nvme_init_device(PCIEnum *pci_enum) {
    NVME_LOG("(info) found device " PCI_ADDR_PRINTF_FMT,
             PCI_ADDR_PRINTF_VALUES(pci_enum->addr));
    u16 msix_entries = pci_msix_init(pci_enum);
    NVME_LOG("(debug) found %u msix entries", msix_entries);

    InterruptHandler *devisr = interrupt_allocate(nvme_isr, pci_msi_eoi);
    struct NVMeDevice *dev = slab_alloc(sizeof(struct NVMeDevice));
    waitlist_init(&dev->queues[0].irq_waiters);
    mutex_init(&dev->queues[0].lock);
    devisr->extra = dev;

    pci_msix_add(pci_enum, 0, devisr->vec, PCI_MSIFMT_EDGE_TRIGGER);
    pci_set_command(pci_enum,
                    PCI_COMMAND_MMIO | PCI_COMMAND_BUSMASTER |
                        PCI_COMMAND_IRQDISABLE,
                    PCI_MASKMODE_SET);

    // Init NVMe (1.0e)
    PCIBar bar0 = pci_get_bar(pci_enum, 0);
    uptr base = bar0.physical;
    nvme_write32(base + NVME_REG32_CC,
                 nvme_read32(base + NVME_REG32_CC) & ~NVME_REG32_CC_EN);
    // Wait for reset to complete
    while (nvme_read32(base + NVME_REG32_CSTS) & NVME_REG32_CSTS_RDY);

    // Setup admin queue

    // Maximum Queue Entries Supported
    u16 mqes = nvme_read64(base + NVME_REG64_CAP) & 0xFFFF;
    u32 qe = min(mqes, 4096 / 64);
    u64 dstrd = (nvme_read64(base + NVME_REG64_CAP) >> 32) & 0xF;
    void *asq_phys = pmm_allocz();
    void *acq_phys = pmm_allocz();
    nvme_write32(base + NVME_REG32_AQA,
                 ((qe - 1) << 16) |
                     (qe - 1)); // CQ size is at 16, SQ size is at 0
    nvme_write64(base + NVME_REG64_ASQ, (uptr)asq_phys);
    nvme_write64(base + NVME_REG64_ACQ, (uptr)acq_phys);

    // Enable and wait for readiness
    nvme_write32(base + NVME_REG32_CC,
                 (6 << NVME_REG32_CC_IOSQES_SHIFT) | NVME_REG32_CC_EN |
                     (4 << NVME_REG32_CC_IOCQES_SHIFT));
    while (!(nvme_read32(base + NVME_REG32_CSTS) & NVME_REG32_CSTS_RDY));

    struct NVMeQueue admin_queue = {0};
    admin_queue.cq = (void *)PHYS_TO_HHDM(acq_phys);
    admin_queue.sq = (void *)PHYS_TO_HHDM(asq_phys);
    admin_queue.qid = 0;
    admin_queue.qe = qe;
    admin_queue.sq_doorbell = base + 0x1000 +
        ((2 * admin_queue.qid) << (2 + dstrd));
    admin_queue.cq_doorbell = base + 0x1000 +
        ((2 * admin_queue.qid + 1) << (2 + dstrd));

    void *temp_buf = pmm_allocz();

    // issue NS0 Identify command
    struct NVMeIdentifySQE ident_in = {.opcode = 0x06,
                                       .nsid = 1,
                                       .cns = 0,
                                       .prp1 = (uptr)temp_buf};
    nvme_submit_and_wait(&admin_queue, &ident_in);
    struct NVMeIdentifyNS *ident = (void *)PHYS_TO_HHDM(temp_buf);
    dev->num_blocks = ident->nsze;
    dev->blocks_shift = ident->lbaf[ident->flbas & 0b11111].ds;
    dev->size = (u64)dev->num_blocks << dev->blocks_shift;
    NVME_LOG("(debug) %zu blocks, block size %d, %zu MiB", dev->num_blocks,
             1 << dev->blocks_shift, dev->size / (1024 * 1024));

    nvme_write32(base + NVME_REG32_INTMC, 0xFFFFFFFF);
    pci_set_command(pci_enum, PCI_COMMAND_IRQDISABLE, PCI_MASKMODE_UNSET);
    pci_msix_set_mask(pci_enum, PCI_MASKMODE_UNSET);

    // create CQ0
    void *sq0_phys = pmm_allocz();
    void *cq0_phys = pmm_allocz();
    struct NVMeCreateCQSQE ccq_in = {.opcode = NVME_OPCODE_ADMIN_CREATE_CQ,
                                     .prp1 = (uptr)cq0_phys,
                                     .qid = 1,
                                     .qsize = qe - 1,
                                     .iv = 0, // msix vector 0
                                     .flags = NVME_CQ_CREATE_FLAGS_IEN |
                                         NVME_CQ_CREATE_FLAGS_PC};
    nvme_submit_and_wait(&admin_queue, &ccq_in);
    // create SQ0
    struct NVMeCreateSQSQE csq_in = {.opcode = NVME_OPCODE_ADMIN_CREATE_SQ,
                                     .prp1 = (uptr)sq0_phys,
                                     .qid = 1,
                                     .cqid = 1,
                                     .qsize = qe - 1,
                                     .flags = NVME_SQ_CREATE_FLAGS_PC};
    nvme_submit_and_wait(&admin_queue, &csq_in);
    pmm_free(temp_buf);

    dev->queues[0].cq = (void *)PHYS_TO_HHDM(cq0_phys);
    dev->queues[0].sq = (void *)PHYS_TO_HHDM(sq0_phys);
    dev->queues[0].qid = 1;
    dev->queues[0].qe = qe;
    dev->queues[0].sq_doorbell = base + 0x1000 +
        ((2 * dev->queues[0].qid) << (2 + dstrd));
    dev->queues[0].cq_doorbell = base + 0x1000 +
        ((2 * dev->queues[0].qid + 1) << (2 + dstrd));

    // Init devfs
    char devname[32];
    int namelen = snprintf(
        devname, 32, "nvme%zu",
        __atomic_fetch_add(&g_next_drive_id, 1, __ATOMIC_SEQ_CST));
    Vnode *nvme_vn;
    devfs_register(NULL, devname, namelen, &g_nvme_dev_ops, dev, &nvme_vn);

    return 0;
}

static PCIDevDriver g_nvme_driver = {
    .wildcard = {.clazz = PCI_CLASS_STORAGE,
                 .subclass = PCI_SUBCLASS_STORAGE_NVM},
    .wildcard_mask = PCI_QUERYMASK_CLASS | PCI_QUERYMASK_SUBCLASS,
    .init_dev = nvme_init_device};

int nvme_init(void) {
    log("initializing nvme");
    return pci_install_driver(&g_nvme_driver);
}
