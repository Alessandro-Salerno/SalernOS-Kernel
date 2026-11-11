#include <arch/info.h>
#include <errno.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/util.h>
#include <kernel/opt/pci.h>
#include <kernel/platform/mmu.h>

// CREDIT: Mathewnd/Astral
// NOTE: mostly inspired by Astral, also uses some parts from 2022 SalernOS

#define PCI_LOG(fmt, ...) log_opt("PCI", fmt "\n", __VA_ARGS__)
#define PCI_PANIC()       panic(NULL, "pci error");

#define PCI_BAR_IO           1
#define PCI_BAR_TYPE_MASK    0x6
#define PCI_BAR_TYPE_64BIT   0x4
#define PCI_BAR_PREFETCHABLE 0x8

#define PCI_FUNC_EXISTS(bus, device, function) \
    (pci_raw_read32(bus, device, function, 0) != 0xffffffff)
#define PCI_DEV_EXISTS(bus, device) PCI_FUNC_EXISTS(bus, device, 0)

#define PCI_NUM_DEVICES_PER_BUS 32

#define PCI_FOREACH_ENUM_WILDCARD(currptr, wildcard, wildcard_mask) \
    for (*(currptr) = pci_enum_first(wildcard, wildcard_mask);      \
         *(currptr) != NULL;                                        \
         *(currptr) = pci_enum_next(*(currptr), wildcard, wildcard_mask))

struct MSIXMessage {
    u32 addresslow;
    u32 addresshigh;
    u32 data;
    u32 control;
} packed;

static PCIOverrides *g_overrides = NULL;
static LIST_HEAD(, PCIEnum) g_pci_enumerator_list;

static inline bool pci_wildcard_match(PCIEnum *e, PCIQuery wildcard,
                                      int wildcard_mask) {
    bool class_matches = !(wildcard_mask & PCI_QUERYMASK_CLASS) ||
        e->info.clazz == wildcard.clazz;
    bool subclass_matches = !(wildcard_mask & PCI_QUERYMASK_SUBCLASS) ||
        e->info.subclass == wildcard.subclass;
    bool progif_matches = !(wildcard_mask & PCI_QUERYMASK_PROGIF) ||
        e->info.progif == wildcard.progif;
    bool vendor_matches = !(wildcard_mask & PCI_QUERYMASK_VENDOR) ||
        e->info.vendor == wildcard.vendor;
    bool deviceid_matches = !(wildcard_mask & PCI_QUERYMASK_DEVICEID) ||
        e->info.deviceid == wildcard.deviceid;
    bool revision_matches = !(wildcard_mask & PCI_QUERYMASK_REVISION) ||
        e->info.revision == wildcard.revision;

    return class_matches && subclass_matches && progif_matches &&
        vendor_matches && deviceid_matches && revision_matches;
}

static inline PCIEnum *pci_enum_first(PCIQuery wildcard, int wildcard_mask) {
    PCIEnum *e, *_;
    LIST_FOREACH_SAFE(e, &g_pci_enumerator_list, tqe_enums, _) {
        if (pci_wildcard_match(e, wildcard, wildcard_mask)) return e;
    }

    return NULL;
}

static inline PCIEnum *pci_enum_next(PCIEnum *e, PCIQuery wildcard,
                                     int wildcard_mask) {
    for (PCIEnum *next = LIST_NEXT(e, tqe_enums); next != NULL;
         next = LIST_NEXT(next, tqe_enums)) {
        if (pci_wildcard_match(next, wildcard, wildcard_mask)) return next;
    }

    return NULL;
}

static void *pci_map_bar(PCIBar bar) {
    MMUFlags cache_flags = (bar.prefetchable ? MMU_FLAGS_WT : MMU_FLAGS_UC);
    return vmm_map(NULL, NULL, (void *)bar.physical, bar.length,
                   VMM_FLAGS_NOHINT | VMM_FLAGS_PHYSICAL,
                   MMU_FLAGS_READ | MMU_FLAGS_WRITE | MMU_FLAGS_NOEXEC |
                       cache_flags);
}

static inline u8 pci_raw_read8(u32 bus, u32 dev, u32 func, usize off) {
    PCIAddr addr = {.bus = bus, .device = dev, .function = func, .segment = 0};
    return pci_read8(&addr, off);
}

static inline u32 pci_raw_read32(u32 bus, u32 dev, u32 func, usize off) {
    PCIAddr addr = {.bus = bus, .device = dev, .function = func, .segment = 0};
    return pci_read32(&addr, off);
}

static void enumerate_function(u32 bus, u32 dev, u32 func) {
    PCIEnum *e = slab_alloc(sizeof(*e));
    e->addr.bus = bus;
    e->addr.device = dev;
    e->addr.function = func;
    e->addr.segment = 0;
    e->info.clazz = pci_enum_read8(e, PCI_CONFIG_CLASS);
    e->info.subclass = pci_enum_read8(e, PCI_CONFIG_SUBCLASS);
    e->info.progif = pci_enum_read8(e, PCI_CONFIG_PROGIF);
    e->info.vendor = pci_enum_read16(e, PCI_CONFIG_VENDOR);
    e->info.deviceid = pci_enum_read16(e, PCI_CONFIG_DEVICEID);
    e->info.revision = pci_enum_read8(e, PCI_CONFIG_REVISION);

    u8 msix_off = pci_get_capability_offset(e, PCI_CAP_MSIX, 0);
    if (msix_off != PCI_CAPOFF_ABSENT) {
        e->msix.exists = true;
        e->msix.offset = msix_off;
    }

    u8 msi_off = pci_get_capability_offset(e, PCI_CAP_MSI, 0);
    if (msi_off != PCI_CAPOFF_ABSENT) {
        e->msi.exists = true;
        e->msi.offset = msi_off;
    }

    LIST_INSERT_HEAD(&g_pci_enumerator_list, e, tqe_enums);

    PCI_LOG("(info) device: %02X:%02X.%X %02X %02X %04X:%04X (rev = %2X) "
            "(interrupt = %4s)",
            bus, dev, func, e->info.clazz, e->info.subclass, e->info.vendor,
            e->info.deviceid, e->info.revision,
            (e->msix.exists)      ? "msix"
                : (e->msi.exists) ? "msi"
                                  : "n/a");
}

static void enumerate_device(u32 bus, u32 dev) {
    bool is_multifunction = pci_raw_read8(bus, dev, 0, PCI_CONFIG_HEADERTYPE) &
        PCI_HEADERTYPE_MULTIFUNCTION_MASK;
    u32 num_functions = (is_multifunction) ? 8 : 1;

    for (u32 func = 0; func < num_functions; func++) {
        if (PCI_FUNC_EXISTS(bus, dev, func)) enumerate_function(bus, dev, func);
    }
}

// abstraction functions

u8 pci_read8(const PCIAddr *addr, usize off) {
    assert(g_overrides != NULL);
    return g_overrides->read8(addr, off);
}

u16 pci_read16(const PCIAddr *addr, usize off) {
    assert(g_overrides != NULL);
    return g_overrides->read16(addr, off);
}

u32 pci_read32(const PCIAddr *addr, usize off) {
    assert(g_overrides != NULL);
    return g_overrides->read32(addr, off);
}

void pci_write8(const PCIAddr *addr, usize off, u8 val) {
    assert(g_overrides != NULL);
    g_overrides->write8(addr, off, val);
}

void pci_write16(const PCIAddr *addr, usize off, u16 val) {
    assert(g_overrides != NULL);
    g_overrides->write16(addr, off, val);
}

void pci_write32(const PCIAddr *addr, usize off, u32 val) {
    assert(g_overrides != NULL);
    g_overrides->write32(addr, off, val);
}

// PCI interface

u8 pci_get_capability_offset(PCIEnum *e, u8 cap, int max_loop) {
    if (!(pci_enum_read16(e, PCI_CONFIG_STATUS) & PCI_STATUS_HASCAP)) return 0;

    u8 offset = pci_enum_read8(e, PCI_CONFIG_CAP);
    while (offset != 0) {
        if (cap == pci_enum_read8(e, offset) && max_loop-- == 0) break;
        offset = pci_enum_read8(e, offset + 1);
    }

    return offset;
}

void pci_set_command(PCIEnum *e, u16 mask, int mask_mode) {
    u16 command = pci_enum_read16(e, PCI_CONFIG_COMMAND);

    switch (mask_mode) {
        case PCI_MASKMODE_SET:
            command |= mask;
            break;
        case PCI_MASKMODE_UNSET:
            command &= ~mask;
            break;
        case PCI_MASKMODE_OVERRIDE:
            command = mask;
            break;
        default:
            PCI_LOG("(error) unknown mask mode %d", mask_mode);
            PCI_PANIC();
    }

    pci_enum_write16(e, PCI_CONFIG_COMMAND, command);
}

PCIBar pci_get_bar(PCIEnum *e, usize index) {
    if (e->bar[index].length != 0) return e->bar[index];

    PCIBar ret;
    usize off = 0x10 + index * 4;
    u32 bar = pci_enum_read32(e, off);

    if (bar & PCI_BAR_IO) {
        ret.mmio = false;
        ret.prefetchable = false;
        ret.is64bits = false;
        ret.physical = bar & 0xfffffffc;
        pci_enum_write32(e, off, 0xffffffff);
        ret.length = -1;
        pci_enum_write32(e, off, bar);
        goto end;
    }

    ret.mmio = true;
    ret.prefetchable = bar & PCI_BAR_PREFETCHABLE;
    ret.physical = bar & 0xfffffff0;
    ret.length = -1;
    ret.is64bits = (bar & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_64BIT;

    if (ret.is64bits) ret.physical |= (u64)pci_enum_read32(e, off + 4) << 32;

    pci_enum_write32(e, off, 0xffffffff);
    ret.length = ~(pci_enum_read32(e, off) & 0xfffffff0) + 1;
    pci_enum_write32(e, off, bar);
    ret.virtual = (uptr)pci_map_bar(ret);

end:
    e->bar[index] = ret;
    return ret;
}

void pci_msix_set_mask(PCIEnum *e, int mask_mode) {
    u16 msgctl = pci_enum_read16(e, e->msix.offset + 2);

    switch (mask_mode) {
        case PCI_MASKMODE_SET:
            msgctl |= 0x4000;
            break;
        case PCI_MASKMODE_UNSET:
            msgctl &= ~0x4000;
            break;
        default:
            PCI_LOG("(error) unknown mask mode %d", mask_mode);
            PCI_PANIC();
    }

    pci_enum_write16(e, e->msix.offset + 2, msgctl);
}

void pci_msix_add(PCIEnum *e, u8 msixvec, u64 intvec, int flags) {
    assert(e->msix.exists);
    assert(msixvec < e->irq.msix.num_entries);

    // NOTE: here address is 64 bits because we then have to split it into low
    // and high chunks, but this is still portable as long as it is not used on
    // a machine with addresses larger than 64 bits. But I think this is
    // something the PCI people will look into, not me.
    PCIBar bir = pci_get_bar(e, e->irq.msix.bir);
    volatile struct MSIXMessage
        *msix_table = (void *)(bir.virtual + e->irq.msix.table_offset);
    volatile struct MSIXMessage *msix_entry = &msix_table[msixvec];

    u32 data;
    u64 address = pci_msi_format_message(&data, intvec, flags);

    msix_entry->addresslow = address & 0xffffffff;
    msix_entry->addresshigh = (address >> 32) & 0xffffffff;
    msix_entry->data = data;
    msix_entry->control = 0;
}

u16 pci_msix_init(PCIEnum *e) {
    if (!e->msix.exists) return 0;

    u16 msgcntl = pci_enum_read16(e, e->msix.offset + 2);
    msgcntl |= 0xC000;
    pci_enum_write16(e, e->msix.offset + 2, msgcntl);
    e->irq.msix.num_entries = (msgcntl & 0x3ff) + 1;

    u32 bir = pci_enum_read32(e, e->msix.offset + 4);
    e->irq.msix.bir = bir & 0b111;
    e->irq.msix.table_offset = bir & ~(u32)0b111;

    u32 pbir = pci_enum_read32(e, e->msix.offset + 8);
    e->irq.msix.pbir = pbir & 0b111;
    e->irq.msix.pba_offset = pbir & ~(u32)0b111;

    pci_set_command(e, PCI_COMMAND_IRQDISABLE, PCI_MASKMODE_SET);

    return e->irq.msix.num_entries;
}

// driver interface

int pci_install_driver(PCIDevDriver *driver) {
    PCIEnum *e;
    int ret = ENODEV;

    PCI_FOREACH_ENUM_WILDCARD(&e, driver->wildcard, driver->wildcard_mask) {
        driver->init_dev(e);
        ret = 0;
    }

    return ret;
}

// setup functions

void pci_set_overrides(PCIOverrides *overrides) {
    assert(g_overrides == NULL);
    g_overrides = overrides;
    LIST_INIT(&g_pci_enumerator_list);
}

void pci_enumate(u32 start_bus, u32 end_bus) {
    log("enumerating pci");

    for (u32 bus = start_bus; bus < end_bus; bus++) {
        for (u32 dev = 0; dev < PCI_NUM_DEVICES_PER_BUS; dev++) {
            if (PCI_DEV_EXISTS(bus, dev)) enumerate_device(bus, dev);
        }
    }
}
