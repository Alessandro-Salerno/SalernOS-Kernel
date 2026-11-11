#pragma once

#include <kernel/com/sys/interrupt.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

#ifndef HAVE_OPT_PCI
#error "cannot include this header without having opt/pci"
#endif

#define PCI_BAR_BASE 0x10

// 2 byte fields
#define PCI_CONFIG_VENDOR      0x0
#define PCI_CONFIG_DEVICEID    0x2
#define PCI_CONFIG_COMMAND     0x4
#define PCI_COMMAND_IO         0x1
#define PCI_COMMAND_MMIO       0x2
#define PCI_COMMAND_BUSMASTER  0x4
#define PCI_COMMAND_IRQDISABLE 0x400
#define PCI_CONFIG_STATUS      0x6
#define PCI_STATUS_HASCAP      0x10

// 1 byte fields
#define PCI_CONFIG_REVISION               0x8
#define PCI_CONFIG_PROGIF                 0x9
#define PCI_CONFIG_SUBCLASS               0xa
#define PCI_CONFIG_CLASS                  0xb
#define PCI_CONFIG_HEADERTYPE             0xe
#define PCI_HEADERTYPE_MULTIFUNCTION_MASK 0x80
#define PCI_HEADERTYPE_MASK               0x7f
#define PCI_HEADERTYPE_STANDARD           0
#define PCI_CONFIG_CAP                    0x34
#define PCI_CAP_MSI                       0x5
#define PCI_CAP_VENDORSPECIFIC            0x9
#define PCI_CAP_MSIX                      0x11

// PCI device classes
#define PCI_CLASS_STORAGE 0x1

// PCI device subclasses
#define PCI_SUBCLASS_STORAGE_NVM 0x8

#define PCI_CAPOFF_ABSENT 0

#define PCI_MASKMODE_SET      1
#define PCI_MASKMODE_UNSET    2
#define PCI_MASKMODE_OVERRIDE 3

#define PCI_MSIFMT_EDGE_TRIGGER 1
#define PCI_MSIFMT_DEASSERT     2

#define PCI_QUERYMASK_NONE     0
#define PCI_QUERYMASK_CLASS    1
#define PCI_QUERYMASK_SUBCLASS 2
#define PCI_QUERYMASK_PROGIF   4
#define PCI_QUERYMASK_VENDOR   8
#define PCI_QUERYMASK_DEVICEID 16
#define PCI_QUERYMASK_REVISION 32

#define PCI_ADDR_PRINTF_FMT          "%02X:%02X:%02X"
#define PCI_ADDR_PRINTF_VALUES(addr) (addr).bus, (addr).device, (addr).function

typedef struct PCIAddr {
    u32 segment;
    u32 bus;
    u32 device;
    u32 function;
} PCIAddr;

typedef struct PCIQuery {
    u8 clazz;
    u8 subclass;
    u8 progif;
    u16 vendor;
    u16 deviceid;
    u8 revision;
} PCIQuery;

typedef struct PCIOverrides {
    u8 (*read8)(const PCIAddr *addr, usize off);
    u16 (*read16)(const PCIAddr *addr, usize off);
    u32 (*read32)(const PCIAddr *addr, usize off);
    void (*write8)(const PCIAddr *addr, usize off, u8 val);
    void (*write16)(const PCIAddr *addr, usize off, u16 val);
    void (*write32)(const PCIAddr *addr, usize off, u32 val);
} PCIOverrides;

typedef struct PCICap {
    bool exists;
    u8 offset;
} PCICap;

typedef struct PCIBar {
    uptr virtual;
    uptr physical;
    usize length;
    bool mmio;
    bool is64bits;
    bool prefetchable;
} PCIBar;

typedef struct PCIEnum {
    LIST_ENTRY(PCIEnum) tqe_enums;
    PCIAddr addr;
    PCIQuery info;
    PCICap msi;
    PCICap msix;
    PCIBar bar[6];
    union {
        struct {
            u32 bir;
            u32 table_offset;
            u32 pbir;
            u32 pba_offset;
            u16 num_entries;
        } msix;
    } irq;
} PCIEnum;

typedef struct PCIDevDriver {
    PCIQuery wildcard;
    int wildcard_mask;
    int (*init_dev)(PCIEnum *e);
} PCIDevDriver;

// abstraction
void pci_register_driver(PCIDevDriver driver);
u8 pci_read8(const PCIAddr *addr, usize off);
u16 pci_read16(const PCIAddr *addr, usize off);
u32 pci_read32(const PCIAddr *addr, usize off);
void pci_write8(const PCIAddr *addr, usize off, u8 val);
void pci_write16(const PCIAddr *addr, usize off, u16 val);
void pci_write32(const PCIAddr *addr, usize off, u32 val);

// PCI interface
u8 pci_get_capability_offset(PCIEnum *e, u8 cap, int max_loop);
void pci_set_command(PCIEnum *e, u16 mask, int mask_mode);
PCIBar pci_get_bar(PCIEnum *e, usize index);
void pci_msix_add(PCIEnum *e, u8 msixvec, u64 intvec, int flags);
void pci_msix_set_mask(PCIEnum *e, int mask_mode);
u16 pci_msix_init(PCIEnum *e);

// driver interface
int pci_install_driver(PCIDevDriver *driver);

// setup functions
void pci_set_overrides(PCIOverrides *overrides);
void pci_enumate(u32 start_bus, u32 end_bus);

// platform interface functions
u64 pci_msi_format_message(u32 *data, u64 intvec, int flags);
void pci_msi_eoi(InterruptHandler *isr);

static inline u8 pci_enum_read8(const PCIEnum *e, usize off) {
    return pci_read8(&e->addr, off);
}

static inline u16 pci_enum_read16(const PCIEnum *e, usize off) {
    return pci_read16(&e->addr, off);
}

static inline u32 pci_enum_read32(const PCIEnum *e, usize off) {
    return pci_read32(&e->addr, off);
}

static inline void pci_enum_write16(const PCIEnum *e, usize off, u16 v) {
    pci_write16(&e->addr, off, v);
}

static inline void pci_enum_write32(const PCIEnum *e, usize off, u32 v) {
    pci_write32(&e->addr, off, v);
}
