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

#include <arch/barrier.h>
#include <arch/info.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/opt/nvme.h>
#include <kernel/opt/pci.h>
#include <lib/spinlock.h>
#include <lib/util.h>

// TAKEN: vloxei64/ke
// CREDIT: vloxei64/ke

#define NVME_LOG(fmt, ...) KOPTMSG("NVME", fmt "\n", __VA_ARGS__)
#define NVME_PANIC()       com_sys_panic(NULL, "nvme error")

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

struct nvme_identify_sqe {
    uint8_t  opcode;
    uint8_t  sqe_flags;
    uint16_t cid;
    uint32_t nsid;
    uint8_t  _rsv[16];
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cns;
    uint8_t  _rsv2[20];
};

struct nvme_identify_ns_lba {
    uint16_t ms;
    uint8_t  ds;
    uint8_t  rp;
};

struct nvme_identify_ns {
    uint64_t                    nsze;
    uint64_t                    ncap;
    uint64_t                    nuse;
    uint8_t                     nsfeat;
    uint8_t                     nlbaf;
    uint8_t                     flbas;
    uint8_t                     mc;
    uint8_t                     dpc;
    uint8_t                     dps;
    uint8_t                     _rsv[98];
    struct nvme_identify_ns_lba lbaf[16];
};

struct nvme_create_cq_sqe {
    uint8_t  opcode;
    uint8_t  sqe_flags;
    uint16_t cid;
    uint8_t  _rsv[20];
    uint64_t prp1;
    uint64_t _rsv2;
    uint16_t qid;
    uint16_t qsize;
    uint16_t flags; // IEN and PC
    uint8_t  iv;
    uint8_t  _rsv3[16];
};

struct nvme_create_sq_sqe {
    uint8_t  opcode;
    uint8_t  sqe_flags;
    uint16_t cid;
    uint8_t  _rsv[20];
    uint64_t prp1;
    uint64_t _rsvd2;
    uint16_t qid;
    uint16_t qsize;
    uint16_t flags; // QPRIO and PC
    uint16_t cqid;
    uint8_t  _rsv3[16];
};

struct nvme_rw_sqe {
    uint8_t  opcode;
    uint8_t  sqe_flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t _rsvd1;
    uint64_t metadata;
    uint64_t prp1;
    uint64_t prp2;
    uint64_t slba;
    uint32_t cdw12; // NOTE: NLB is a 0-based value (ie value-1)
    uint32_t cdw13; // dataset management
    uint32_t eilbrt;
    uint16_t elbatm;
    uint16_t elbat;
};

struct nvme_queue {
    uint8_t                *sq;
    uint8_t                *cq;
    uint64_t                qe;
    uint64_t                sq_doorbell;
    uint64_t                cq_doorbell;
    uint32_t                sq_idx;
    uint32_t                cq_idx;
    uint32_t                qid;
    bool                    cq_phase;
    struct com_thread_tailq irq_waiters;
    kspinlock_t             lock;
};

struct nvme_device {
    uint64_t          num_blocks;
    uint64_t          size;
    uint32_t          blocks_shift;
    struct nvme_queue queues[1]; // 1 for now, 64 in general
};

// Used to generate /dev/nvme%zu
static size_t NextDriveId = 0;

static inline uint32_t nvme_read32(uint64_t base) {
    return *(volatile uint32_t *)(ARCH_PHYS_TO_HHDM(base));
}

static inline void nvme_write32(uint64_t base, uint32_t val) {
    *(volatile uint32_t *)(ARCH_PHYS_TO_HHDM(base)) = val;
}

static inline uint64_t nvme_read64(uint64_t base) {
    return *(volatile uint64_t *)(ARCH_PHYS_TO_HHDM(base));
}

static inline void nvme_write64(uint64_t base, uint64_t val) {
    *(volatile uint64_t *)(ARCH_PHYS_TO_HHDM(base)) = val;
}

static bool nvme_queue_done_nobarrier(struct nvme_queue *q) {
    return ((((volatile uint32_t *)(q->cq + q->cq_idx * 16))[3] >> 16) & 1) !=
           q->cq_phase;
}

static bool nvme_queue_done(struct nvme_queue *q) {
    ARCH_BARRIER_GENERIC_READ();
    return nvme_queue_done_nobarrier(q);
}

static void nvme_submit_and_wait(struct nvme_queue *q, void *in) {
    kmemcpy(q->sq + q->sq_idx * 64, in, 64);
    q->sq_idx++;
    if (q->sq_idx == q->qe) {
        q->sq_idx = 0;
    }
    ARCH_BARRIER_GENERIC_WRITE();
    nvme_write32(q->sq_doorbell, q->sq_idx);

    while (!nvme_queue_done_nobarrier(q));
    ARCH_BARRIER_GENERIC_READ();

    q->cq_idx++;
    if (q->cq_idx == q->qe) {
        q->cq_idx = 0;
        q->cq_phase ^= 1;
    }
    nvme_write32(q->cq_doorbell, q->cq_idx);
}

static void nvme_submit(struct nvme_queue *q, void *in) {
    kmemcpy(q->sq + q->sq_idx * 64, in, 64);
    q->sq_idx++;
    if (q->sq_idx == q->qe) {
        q->sq_idx = 0;
    }
    ARCH_BARRIER_GENERIC_WRITE();
    nvme_write32(q->sq_doorbell, q->sq_idx);
}

static void nvme_submit_page_command_sync(struct nvme_device *drive,
                                          void               *buf,
                                          size_t              pageno,
                                          uint8_t             opcode) {
    uint64_t           lba   = pageno << (12 - drive->blocks_shift);
    uint64_t           count = 1 << (12 - drive->blocks_shift);
    struct nvme_rw_sqe rw_in = {.opcode = opcode,
                                .nsid   = 1,
                                .prp1   = (uint64_t)buf,
                                .slba   = lba,
                                .cdw12  = count - 1};
    kspinlock_acquire(&drive->queues[0].lock);
    nvme_submit(&drive->queues[0], &rw_in);

    while (!nvme_queue_done_nobarrier(&drive->queues[0])) {
        com_sys_sched_wait(&drive->queues[0].irq_waiters,
                           &drive->queues[0].lock);
    }
    ARCH_BARRIER_GENERIC_READ();

    drive->queues[0].cq_idx++;
    if (drive->queues[0].cq_idx == drive->queues[0].qe) {
        drive->queues[0].cq_idx = 0;
        drive->queues[0].cq_phase ^= 1;
    }
    nvme_write32(drive->queues[0].cq_doorbell, drive->queues[0].cq_idx);

    kspinlock_release(&drive->queues[0].lock);
}

static void nvme_submit_multipage_command_sync(struct nvme_device *drive,
                                               void               *buf,
                                               size_t              pageno,
                                               size_t              num_pages,
                                               uint8_t             opcode) {
    for (size_t i = 0; i < num_pages; i++) {
        nvme_submit_page_command_sync(drive,
                                      com_mm_vmm_get_physical(NULL, buf),
                                      pageno + i,
                                      opcode);
        buf += ARCH_PAGE_SIZE;
    }
}

static void nvme_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)ctx;
    struct nvme_device *drive = isr->extra;
    kspinlock_acquire(&drive->queues[0].lock);
    com_sys_sched_notify(&drive->queues[0].irq_waiters);
    kspinlock_release(&drive->queues[0].lock);
    com_sys_sched_yield();
}

// /dev/nvme%z DEVICE OPERATIONS

// TODO: fix the most terrible read implementation ever
static int nvme_read(void     *buf,
                     size_t    buflen,
                     size_t   *bytes_read,
                     void     *devdata,
                     uintmax_t off,
                     uintmax_t flags) {
    struct nvme_device *device = devdata;

    if (off >= device->size) {
        *bytes_read = 0;
        return 0;
    }

    if (off + buflen >= device->size) {
        buflen = device->size - off;
    }

    if (0 == buflen) {
        *bytes_read = 0;
        return 0;
    }

    size_t base_page  = off / ARCH_PAGE_SIZE;
    size_t rem        = off % ARCH_PAGE_SIZE;
    size_t rem_buflen = buflen;

    // Fast path if the buffer is aligned
    if (0 == rem && 0 == buflen % ARCH_PAGE_SIZE &&
        0 == (uintptr_t)buf % ARCH_PAGE_SIZE) {
        nvme_submit_multipage_command_sync(device,
                                           buf,
                                           base_page,
                                           buflen / ARCH_PAGE_SIZE,
                                           NVME_OPCODE_READ);
        *bytes_read = buflen;
        return 0;
    }

    void *tmp_phys = com_mm_pmm_alloc();
    void *tmp_hhdm = (void *)ARCH_PHYS_TO_HHDM(tmp_phys);

    for (size_t i = 0; i < (buflen + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
         i++) {
        size_t read_size = KMIN(ARCH_PAGE_SIZE - rem, rem_buflen);
        nvme_submit_page_command_sync(device,
                                      tmp_phys,
                                      base_page + i,
                                      NVME_OPCODE_READ);
        kmemcpy(buf, tmp_hhdm + rem, read_size);
        buf += read_size;
        *bytes_read += read_size;
        rem_buflen -= read_size;
        rem = 0;
    }

    com_mm_pmm_free(tmp_phys);
    return 0;
}

static int nvme_write(size_t   *bytes_written,
                      void     *devdata,
                      void     *buf,
                      size_t    buflen,
                      uintmax_t off,
                      uintmax_t flags) {
    struct nvme_device *device = devdata;

    if (off >= device->size) {
        *bytes_written = 0;
        return 0;
    }

    if (off + buflen >= device->size) {
        buflen = device->size - off;
    }

    if (0 == buflen) {
        *bytes_written = 0;
        return 0;
    }

    size_t base_page  = off / ARCH_PAGE_SIZE;
    size_t rem        = off % ARCH_PAGE_SIZE;
    size_t rem_buflen = buflen;

    // Fast path if the buffer is aligned
    if (0 == rem && 0 == buflen % ARCH_PAGE_SIZE &&
        0 == (uintptr_t)buf % ARCH_PAGE_SIZE) {
        nvme_submit_multipage_command_sync(device,
                                           buf,
                                           base_page,
                                           buflen / ARCH_PAGE_SIZE,
                                           NVME_OPCODE_WRITE);
        *bytes_written = buflen;
        return 0;
    }

    void *tmp_phys = com_mm_pmm_alloc();
    void *tmp_hhdm = (void *)ARCH_PHYS_TO_HHDM(tmp_phys);

    for (size_t i = 0; i < (buflen + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
         i++) {
        size_t read_size = KMIN(ARCH_PAGE_SIZE - rem, rem_buflen);
        nvme_submit_page_command_sync(device,
                                      tmp_phys,
                                      base_page + i,
                                      NVME_OPCODE_READ);
        kmemcpy(tmp_hhdm + rem, buf, read_size);
        nvme_submit_page_command_sync(device,
                                      tmp_phys,
                                      base_page + i,
                                      NVME_OPCODE_WRITE);
        buf += read_size;
        *bytes_written += read_size;
        rem_buflen -= read_size;
        rem = 0;
    }

    com_mm_pmm_free(tmp_phys);
    return 0;
}

static int nvme_stat(struct stat *out, void *devdata) {
    struct nvme_device *drive = devdata;
    out->st_blksize           = 1 << drive->blocks_shift;
    out->st_ino               = (ino_t)devdata;
    out->st_mode              = 0777 | S_IFBLK;
    out->st_blocks            = drive->num_blocks;
    out->st_nlink             = 1;
    out->st_size              = drive->size;
    return 0;
}

static com_dev_ops_t NVMEDevOps = {.read  = nvme_read,
                                   .write = nvme_write,
                                   .stat  = nvme_stat};

static int nvme_init_device(opt_pci_enum_t *pci_enum) {
    NVME_LOG("(info) found device " OPT_PCI_ADDR_PRINTF_FMT,
             OPT_PCI_ADDR_PRINTF_VALUES(pci_enum->addr));
    uint16_t msix_entries = opt_pci_msix_init(pci_enum);
    NVME_LOG("(debug) found %zu msix entries", msix_entries);

    com_isr_t *devisr = com_sys_interrupt_allocate(nvme_isr, arch_pci_msi_eoi);
    struct nvme_device *dev = com_mm_slab_alloc(sizeof(struct nvme_device));
    TAILQ_INIT(&dev->queues[0].irq_waiters);
    dev->queues[0].lock = KSPINLOCK_NEW();
    devisr->extra       = dev;

    opt_pci_msix_add(pci_enum, 0, devisr->vec, OPT_PCI_MSIFMT_EDGE_TRIGGER);
    opt_pci_set_command(pci_enum,
                        OPT_PCI_COMMAND_MMIO | OPT_PCI_COMMAND_BUSMASTER |
                            OPT_PCI_COMMAND_IRQDISABLE,
                        OPT_PCI_MASKMODE_SET);

    // Init NVMe (1.0e)
    opt_pci_bar_t bar0 = opt_pci_get_bar(pci_enum, 0);
    uintptr_t     base = bar0.physical;
    nvme_write32(base + NVME_REG32_CC,
                 ~NVME_REG32_CC_EN & nvme_read32(base + NVME_REG32_CC));
    // Wait for reset to complete
    while (NVME_REG32_CSTS_RDY & nvme_read32(base + NVME_REG32_CSTS));

    // Setup admin queue

    // Maximum Queue Entries Supported
    uint16_t mqes     = nvme_read64(base + NVME_REG64_CAP) & 0xFFFF;
    uint32_t qe       = KMIN(mqes, 4096 / 64);
    uint64_t dstrd    = (nvme_read64(base + NVME_REG64_CAP) >> 32) & 0xF;
    void    *asq_phys = com_mm_pmm_alloc_zero();
    void    *acq_phys = com_mm_pmm_alloc_zero();
    nvme_write32(base + NVME_REG32_AQA,
                 ((qe - 1) << 16) |
                     (qe - 1)); // CQ size is at 16, SQ size is at 0
    nvme_write64(base + NVME_REG64_ASQ, (uintptr_t)asq_phys);
    nvme_write64(base + NVME_REG64_ACQ, (uintptr_t)acq_phys);

    // Enable and wait for readiness
    nvme_write32(base + NVME_REG32_CC,
                 NVME_REG32_CC_EN | (6 << NVME_REG32_CC_IOSQES_SHIFT) |
                     (4 << NVME_REG32_CC_IOCQES_SHIFT));
    while (!(NVME_REG32_CSTS_RDY & nvme_read32(base + NVME_REG32_CSTS)));

    struct nvme_queue admin_queue = {0};
    admin_queue.cq                = (void *)ARCH_PHYS_TO_HHDM(acq_phys);
    admin_queue.sq                = (void *)ARCH_PHYS_TO_HHDM(asq_phys);
    admin_queue.qid               = 0;
    admin_queue.qe                = qe;
    admin_queue.sq_doorbell       = base + 0x1000 +
                              ((2 * admin_queue.qid) << (2 + dstrd));
    admin_queue.cq_doorbell = base + 0x1000 +
                              ((2 * admin_queue.qid + 1) << (2 + dstrd));

    void *temp_buf = com_mm_pmm_alloc_zero();

    // issue NS0 Identify command
    struct nvme_identify_sqe ident_in = {.opcode = 0x06,
                                         .nsid   = 1,
                                         .cns    = 0,
                                         .prp1   = (uintptr_t)temp_buf};
    nvme_submit_and_wait(&admin_queue, &ident_in);
    struct nvme_identify_ns *ident = (void *)ARCH_PHYS_TO_HHDM(temp_buf);
    dev->num_blocks                = ident->nsze;
    dev->blocks_shift              = ident->lbaf[ident->flbas & 0b11111].ds;
    dev->size = (uint64_t)dev->num_blocks << dev->blocks_shift;
    NVME_LOG("(debug) %zu blocks, block size %zu, %zu MiB",
             dev->num_blocks,
             1 << dev->blocks_shift,
             dev->size / (1024 * 1024));

    nvme_write32(base + NVME_REG32_INTMC, 0xFFFFFFFF);
    opt_pci_set_command(pci_enum,
                        OPT_PCI_COMMAND_IRQDISABLE,
                        OPT_PCI_MASKMODE_UNSET);
    opt_pci_msix_set_mask(pci_enum, OPT_PCI_MASKMODE_UNSET);

    // create CQ0
    void                     *sq0_phys = com_mm_pmm_alloc_zero();
    void                     *cq0_phys = com_mm_pmm_alloc_zero();
    struct nvme_create_cq_sqe ccq_in   = {.opcode = NVME_OPCODE_ADMIN_CREATE_CQ,
                                          .prp1   = (uintptr_t)cq0_phys,
                                          .qid    = 1,
                                          .qsize  = qe - 1,
                                          .iv     = 0, // msix vector 0
                                          .flags  = NVME_CQ_CREATE_FLAGS_IEN |
                                                   NVME_CQ_CREATE_FLAGS_PC};
    nvme_submit_and_wait(&admin_queue, &ccq_in);
    // create SQ0
    struct nvme_create_sq_sqe csq_in = {.opcode = NVME_OPCODE_ADMIN_CREATE_SQ,
                                        .prp1   = (uintptr_t)sq0_phys,
                                        .qid    = 1,
                                        .cqid   = 1,
                                        .qsize  = qe - 1,
                                        .flags  = NVME_SQ_CREATE_FLAGS_PC};
    nvme_submit_and_wait(&admin_queue, &csq_in);
    com_mm_pmm_free(temp_buf);

    dev->queues[0].cq          = (void *)ARCH_PHYS_TO_HHDM(cq0_phys);
    dev->queues[0].sq          = (void *)ARCH_PHYS_TO_HHDM(sq0_phys);
    dev->queues[0].qid         = 1;
    dev->queues[0].qe          = qe;
    dev->queues[0].sq_doorbell = base + 0x1000 +
                                 ((2 * dev->queues[0].qid) << (2 + dstrd));
    dev->queues[0].cq_doorbell = base + 0x1000 +
                                 ((2 * dev->queues[0].qid + 1) << (2 + dstrd));

    // Init devfs
    char devname[32];
    int  namelen = snprintf(
        devname,
        32,
        "nvme%zu",
        __atomic_fetch_add(&NextDriveId, 1, __ATOMIC_SEQ_CST));
    com_vnode_t *nvme_vn;
    com_fs_devfs_register(&nvme_vn, NULL, devname, namelen, &NVMEDevOps, dev);

    return 0;
}

static opt_pci_dev_driver_t NVMEDriver = {
    .wildcard      = {.clazz    = OPT_PCI_CLASS_STORAGE,
                      .subclass = OPT_PCI_SUBCLASS_STORAGE_NVM},
    .wildcard_mask = OPT_PCI_QUERYMASK_CLASS | OPT_PCI_QUERYMASK_SUBCLASS,
    .init_dev      = nvme_init_device};

int opt_nvme_init(void) {
    KLOG("initializing nvme");
    return opt_pci_install_driver(&NVMEDriver);
}
