#include <arch/barrier.h>
#include <arch/cpu.h>
#include <arch/info.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ds/radixtree.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/blockdev.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/opt/nvme.h>
#include <kernel/opt/pci.h>

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

// One per command ID, waited on by the thread that submitted it
struct NVMeSlot {
    bool busy;
    bool done;
    u16 status;
    Waitlist waiters;
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
    // Held by the ISR while it reaps completions, so one cannot slip in between
    // a submitter's check and its wait
    Spinlock lock;
    // Indexed by command ID. At most qe - 1 commands are in flight, so neither
    // the submission nor the completion queue can fill up
    struct NVMeSlot *slots;
    u32 num_in_flight;
    Waitlist slot_waiters;
};

struct NVMeDevice {
    u64 num_blocks;
    u64 size;
    u32 blocks_shift;
    struct NVMeQueue queues[1]; // 1 for now, 64 in general

    Sync lock;
    RadixTree buffcache;
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

// Marks every completed command done and wakes its submitter
static void nvme_queue_reap_nolock(struct NVMeQueue *q) {
    bool reaped = false;
    for (;;) {
        u32 dw3 = ((volatile u32 *)(q->cq + q->cq_idx * 16))[3];
        if (((dw3 >> 16) & 1) == q->cq_phase) break;
        barrier_generic_read();

        struct NVMeSlot *slot = &q->slots[dw3 & 0xFFFF];
        assert(slot->busy && !slot->done);
        slot->status = dw3 >> 17;
        slot->done = true;
        sched_notify(&slot->waiters);
        reaped = true;

        q->cq_idx++;
        if (q->cq_idx == q->qe) {
            q->cq_idx = 0;
            q->cq_phase ^= 1;
        }
    }

    if (reaped) nvme_write32(q->cq_doorbell, q->cq_idx);
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
    struct NVMeQueue *q = &drive->queues[0];
    spinlock_acq(&q->lock);
    while (q->num_in_flight == q->qe - 1) {
        spinlock_wait(&q->lock, &q->slot_waiters, 0, false);
    }

    u16 cid = 0;
    while (q->slots[cid].busy) cid++;
    struct NVMeSlot *slot = &q->slots[cid];
    slot->busy = true;
    slot->done = false;
    q->num_in_flight++;
    rw_in.cid = cid;
    nvme_submit(q, &rw_in);

    while (!slot->done) {
        // Before the first thread exists nobody can sleep, and the ISR leaves
        // the queue alone, so we poll
        if (cpu_get_thread() == NULL) {
            nvme_queue_reap_nolock(q);
            continue;
        }
        // We make this non-interruptible otherwise it becomes a busy wait after
        // the first signal is delivered. A better solution would be to make it
        // interruptible and fallible, but for now we assume that progress is
        // maintained because the device will eventually send an interrupt
        spinlock_wait(&q->lock, &slot->waiters, 0, false);
    }

    if (slot->status != 0) {
        log_debug("command %u on page %zu failed with status %#x", opcode,
                  pageno, slot->status);
    }

    slot->busy = false;
    q->num_in_flight--;
    sched_notify(&q->slot_waiters);
    spinlock_rel(&q->lock);
}

static void nvme_isr(InterruptHandler *isr, Context *ctx) {
    (void)ctx;
    struct NVMeDevice *drive = isr->extra;
    if (cpu_get_thread() == NULL) return;
    spinlock_acq(&drive->queues[0].lock);
    nvme_queue_reap_nolock(&drive->queues[0]);
    spinlock_rel(&drive->queues[0].lock);
    sched_yield();
}

// /dev/nvme%z DEVICE OPERATIONS

static int nvme_evict_page(void *device, u32 index, void *phys, int page_flags,
                           int evict_flags) {
    struct NVMeDevice *dev = device;
    if (page_flags & BUFFCACHE_DIRTY)
        nvme_submit_page_command_sync(dev, phys, index, NVME_OPCODE_WRITE);
    if (evict_flags & BUFFCACHE_EVICT_SYNC) return 0;

    sync_acq(&dev->lock);
    verify(radixtree_remove_nolock(&dev->buffcache, index) == 0);
    sync_rel(&dev->lock);
    return 0;
}

static int nvme_get_page(void *devdata, usize index, int flags,
                         BufferPage **out_page) {
    struct NVMeDevice *dev = devdata;
    usize dev_pages = (dev->num_blocks << dev->blocks_shift) / PAGE_SIZE;
    if (index >= dev_pages) return ERANGE;

    RadixTree *cache_tree = &dev->buffcache;
    BufferPage *page = NULL;
    BufferPage *old_page = NULL;
    int ret = 0;

    sync_acq(&dev->lock);
    while (ret == 0) {
        if (radixtree_get_nolock(cache_tree, index, (void *)&page) == ENOENT) {
            sync_rel(&dev->lock);
            page = buffcache_alloc();
            sync_acq(&dev->lock);
            if (page == NULL) {
                ret = ENOMEM;
                break;
            }

            // Here the lookup can both fail and succeed: if will probably fail
            // if uncontended, and if may fail if contended too if a page is
            // inserted and removed before we relock. But it may also succeed if
            // a page is added while we were not holding the lock
            BufferPage *present = NULL;
            if (radixtree_get_nolock(cache_tree, index, (void *)&present) ==
                0) {
                assert(present != NULL);
                assert(present != page);
                buffcache_discard(page);
                continue;
            }

            // Here the second lookup failed, so we insert our own
            ret = radixtree_put_nolock(cache_tree, index, page);
            if (ret != 0) break;

            if (!(flags & VFS_GETPAGE_NOFETCH)) {
                sync_rel(&dev->lock);
                void *phys = buffcache_get_phys(page);
                nvme_submit_page_command_sync(dev, phys, index,
                                              NVME_OPCODE_READ);
                // old_page == NULL ==> this is the first time we attempt it. We
                // cannt attempt this multiple times, because if we get here
                // then the page is in the tree and it is marked as BUSY. Being
                // BUSY, it cannot be removed, so the next lookup cannot fail
                assert(old_page == NULL);
                old_page = page;
                sync_acq(&dev->lock);
                continue;
            } else old_page = page;
        }

        assert(page != NULL);

        // old_page == page ==> we created the page and we just need to
        // communicate this to the buffer cache. We cannot have old_page != NULL
        // when the page was already in the tree

        // Case 1: miss + successful I/O fetch. Owning does not pin the page,
        // so from here on this is a hit
        if (old_page != NULL && page == old_page) {
            buffcache_own(page, dev, index, nvme_evict_page);
            old_page = NULL;
        }
        // Case 2: hit, so hold the page and retry on failure
        if (!buffcache_hold(page, &dev->lock)) continue;
        break;
    }
    sync_rel(&dev->lock);

    assert(old_page == NULL);
    if (ret == 0) *out_page = page;
    else if (page != NULL) buffcache_discard(page);
    return ret;
}

static int nvme_read(void *buf, usize buflen, void *devdata, u64 off, u64 flags,
                     usize *out_bytes_read) {
    struct NVMeDevice *dev = devdata;
    *out_bytes_read = 0;
    if (off >= dev->size) return 0;
    buflen = min(buflen, dev->size - off);

    int ret = 0;
    while (buflen > 0) {
        if (signal_check() != SIGNAL_NONE) {
            ret = EINTR;
            break;
        }

        usize page_off = off % PAGE_SIZE;
        usize len = min(PAGE_SIZE - page_off, buflen);

        BufferPage *page;
        ret = nvme_get_page(dev, off / PAGE_SIZE, VFS_GETPAGE_RDONLY, &page);
        if (ret != 0) break;
        u8 *data = (u8 *)PHYS_TO_HHDM(buffcache_get_phys(page));
        ret = memcpy_maybe_user(buf, data + page_off, len);
        buffcache_rel(page);
        if (ret != 0) break;

        buf += len;
        off += len;
        buflen -= len;
        *out_bytes_read += len;
    }

    return *out_bytes_read != 0 ? 0 : ret;
}

static int nvme_write(void *devdata, void *buf, usize buflen, u64 off,
                      u64 flags, usize *out_bytes_written) {
    struct NVMeDevice *dev = devdata;
    *out_bytes_written = 0;
    if (off >= dev->size) return 0;
    buflen = min(buflen, dev->size - off);

    int ret = 0;
    while (buflen > 0) {
        if (signal_check() != SIGNAL_NONE) {
            ret = EINTR;
            break;
        }

        usize page_off = off % PAGE_SIZE;
        usize len = min(PAGE_SIZE - page_off, buflen);

        BufferPage *page;
        ret = nvme_get_page(dev, off / PAGE_SIZE, 0, &page);
        if (ret != 0) break;
        u8 *data = (u8 *)PHYS_TO_HHDM(buffcache_get_phys(page));
        ret = memcpy_maybe_user(data + page_off, buf, len);
        // A faulting copy may still have changed part of the page
        buffcache_mkdirty(page);
        buffcache_rel(page);
        if (ret != 0) break;

        buf += len;
        off += len;
        buflen -= len;
        *out_bytes_written += len;
    }

    return *out_bytes_written != 0 ? 0 : ret;
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
                                .get_page = nvme_get_page,
                                .stat = nvme_stat};

static int nvme_init_device(PCIEnum *pci_enum) {
    NVME_LOG("(info) found device " PCI_ADDR_PRINTF_FMT,
             PCI_ADDR_PRINTF_VALUES(pci_enum->addr));
    u16 msix_entries = pci_msix_init(pci_enum);
    NVME_LOG("(debug) found %u msix entries", msix_entries);

    InterruptHandler *devisr = interrupt_allocate(nvme_isr, pci_msi_eoi);
    struct NVMeDevice *dev = slab_alloc(sizeof(struct NVMeDevice));
    assert(dev != NULL);
    dev->queues[0].lock = SPINLOCK_NEW();
    waitlist_init(&dev->queues[0].slot_waiters);
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
    assert(asq_phys != NULL && acq_phys != NULL);
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
    assert(temp_buf != NULL);

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
    assert(sq0_phys != NULL && cq0_phys != NULL);
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

    assert(qe * sizeof(struct NVMeSlot) <= PAGE_SIZE);
    void *slots_phys = pmm_allocz();
    assert(slots_phys != NULL);
    dev->queues[0].slots = (void *)PHYS_TO_HHDM(slots_phys);
    for (u32 i = 0; i < qe; i++) {
        waitlist_init(&dev->queues[0].slots[i].waiters);
    }

    dev->queues[0].sq_doorbell = base + 0x1000 +
        ((2 * dev->queues[0].qid) << (2 + dstrd));
    dev->queues[0].cq_doorbell = base + 0x1000 +
        ((2 * dev->queues[0].qid + 1) << (2 + dstrd));

    // Enough layers to index every page of the device
    usize dev_pages = dev->size / PAGE_SIZE;
    usize num_layers = 1;
    while (dev_pages > 1ull << (RADIXTREE_INDEX_BITS * num_layers)) {
        num_layers++;
    }
    sync_init_spinlock(&dev->lock);
    verify(radixtree_init(&dev->buffcache, num_layers) == 0);

    // Init devfs
    char devname[32];
    int namelen = snprintf(
        devname, 32, "nvme%zu",
        __atomic_fetch_add(&g_next_drive_id, 1, __ATOMIC_SEQ_CST));
    blockdev_register(devname, namelen, &g_nvme_dev_ops, dev, dev->size,
                      1 << dev->blocks_shift);

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
