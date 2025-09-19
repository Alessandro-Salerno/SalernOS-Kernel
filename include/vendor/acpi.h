#pragma once

#include <assert.h>
#include <stdint.h>

#define ASSERT_SIZE(t, v) \
    _Static_assert(sizeof(t) == (v), "unexpected size mismatch")

#define PACKED(decl) decl __attribute__((packed));

/*
 * -----------------------------------------------------
 * Common structures provided by the ACPI specification
 * -----------------------------------------------------
 */

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_RSDT_SIGNATURE "RSDT"
#define ACPI_XSDT_SIGNATURE "XSDT"
#define ACPI_MADT_SIGNATURE "APIC"
#define ACPI_FADT_SIGNATURE "FACP"
#define ACPI_FACS_SIGNATURE "FACS"
#define ACPI_MCFG_SIGNATURE "MCFG"
#define ACPI_HPET_SIGNATURE "HPET"
#define ACPI_SRAT_SIGNATURE "SRAT"
#define ACPI_SLIT_SIGNATURE "SLIT"
#define ACPI_DSDT_SIGNATURE "DSDT"
#define ACPI_SSDT_SIGNATURE "SSDT"
#define ACPI_PSDT_SIGNATURE "PSDT"
#define ACPI_ECDT_SIGNATURE "ECDT"
#define ACPI_RHCT_SIGNATURE "RHCT"

#define ACPI_AS_ID_SYS_MEM       0x00
#define ACPI_AS_ID_SYS_IO        0x01
#define ACPI_AS_ID_PCI_CFG_SPACE 0x02
#define ACPI_AS_ID_EC            0x03
#define ACPI_AS_ID_SMBUS         0x04
#define ACPI_AS_ID_SYS_CMOS      0x05
#define ACPI_AS_ID_PCI_BAR_TGT   0x06
#define ACPI_AS_ID_IPMI          0x07
#define ACPI_AS_ID_GP_IO         0x08
#define ACPI_AS_ID_GENERIC_SBUS  0x09
#define ACPI_AS_ID_PCC           0x0A
#define ACPI_AS_ID_FFH           0x7F
#define ACPI_AS_ID_OEM_BASE      0xC0
#define ACPI_AS_ID_OEM_END       0xFF

#define ACPI_ACCESS_UD    0
#define ACPI_ACCESS_BYTE  1
#define ACPI_ACCESS_WORD  2
#define ACPI_ACCESS_DWORD 3
#define ACPI_ACCESS_QWORD 4

PACKED(struct acpi_gas {
    uint8_t  address_space_id;
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  access_size;
    uint64_t address;
})
ASSERT_SIZE(struct acpi_gas, 12);

PACKED(struct acpi_rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_addr;

    // vvvv available if .revision >= 2.0 only
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  extended_checksum;
    uint8_t  rsvd[3];
})
ASSERT_SIZE(struct acpi_rsdp, 36);

PACKED(struct acpi_sdt_hdr {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
})
ASSERT_SIZE(struct acpi_sdt_hdr, 36);

PACKED(struct acpi_rsdt {
    struct acpi_sdt_hdr hdr;
    uint32_t            entries[];
})

PACKED(struct acpi_xsdt {
    struct acpi_sdt_hdr hdr;
    uint64_t            entries[];
})

PACKED(struct acpi_entry_hdr {
    /*
     * - acpi_madt_entry_type for the APIC table
     * - acpi_srat_entry_type for the SRAT table
     */
    uint8_t type;
    uint8_t length;
})

// acpi_madt->flags
#define ACPI_PCAT_COMPAT (1 << 0)

enum acpi_madt_entry_type {
    ACPI_MADT_ENTRY_TYPE_LAPIC                      = 0,
    ACPI_MADT_ENTRY_TYPE_IOAPIC                     = 1,
    ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE  = 2,
    ACPI_MADT_ENTRY_TYPE_NMI_SOURCE                 = 3,
    ACPI_MADT_ENTRY_TYPE_LAPIC_NMI                  = 4,
    ACPI_MADT_ENTRY_TYPE_LAPIC_ADDRESS_OVERRIDE     = 5,
    ACPI_MADT_ENTRY_TYPE_IOSAPIC                    = 6,
    ACPI_MADT_ENTRY_TYPE_LSAPIC                     = 7,
    ACPI_MADT_ENTRY_TYPE_PLATFORM_INTERRUPT_SOURCES = 8,
    ACPI_MADT_ENTRY_TYPE_LOCAL_X2APIC               = 9,
    ACPI_MADT_ENTRY_TYPE_LOCAL_X2APIC_NMI           = 0xA,
    ACPI_MADT_ENTRY_TYPE_GICC                       = 0xB,
    ACPI_MADT_ENTRY_TYPE_GICD                       = 0xC,
    ACPI_MADT_ENTRY_TYPE_GIC_MSI_FRAME              = 0xD,
    ACPI_MADT_ENTRY_TYPE_GICR                       = 0xE,
    ACPI_MADT_ENTRY_TYPE_GIC_ITS                    = 0xF,
    ACPI_MADT_ENTRY_TYPE_MULTIPROCESSOR_WAKEUP      = 0x10,
    ACPI_MADT_ENTRY_TYPE_CORE_PIC                   = 0x11,
    ACPI_MADT_ENTRY_TYPE_LIO_PIC                    = 0x12,
    ACPI_MADT_ENTRY_TYPE_HT_PIC                     = 0x13,
    ACPI_MADT_ENTRY_TYPE_EIO_PIC                    = 0x14,
    ACPI_MADT_ENTRY_TYPE_MSI_PIC                    = 0x15,
    ACPI_MADT_ENTRY_TYPE_BIO_PIC                    = 0x16,
    ACPI_MADT_ENTRY_TYPE_LPC_PIC                    = 0x17,
    ACPI_MADT_ENTRY_TYPE_RINTC                      = 0x18,
    ACPI_MADT_ENTRY_TYPE_IMSIC                      = 0x19,
    ACPI_MADT_ENTRY_TYPE_APLIC                      = 0x1A,
    ACPI_MADT_ENTRY_TYPE_PLIC                       = 0x1B,
    ACPI_MADT_ENTRY_TYPE_RESERVED                   = 0x1C, // 0x1C..0x7F
    ACPI_MADT_ENTRY_TYPE_OEM                        = 0x80, // 0x80..0xFF
};

PACKED(struct acpi_madt {
    struct acpi_sdt_hdr   hdr;
    uint32_t              local_interrupt_controller_address;
    uint32_t              flags;
    struct acpi_entry_hdr entries[];
})
ASSERT_SIZE(struct acpi_madt, 44);

/*
 * - acpi_madt_lapic->flags
 * - acpi_madt_lsapic->flags
 * - acpi_madt_x2apic->flags
 */
#define ACPI_PIC_ENABLED        (1 << 0)
#define ACPI_PIC_ONLINE_CAPABLE (1 << 1)

PACKED(struct acpi_madt_lapic {
    struct acpi_entry_hdr hdr;
    uint8_t               uid;
    uint8_t               id;
    uint32_t              flags;
})
ASSERT_SIZE(struct acpi_madt_lapic, 8);

PACKED(struct acpi_madt_ioapic {
    struct acpi_entry_hdr hdr;
    uint8_t               id;
    uint8_t               rsvd;
    uint32_t              address;
    uint32_t              gsi_base;
})
ASSERT_SIZE(struct acpi_madt_ioapic, 12);

/*
 * - acpi_madt_interrupt_source_override->flags
 * - acpi_madt_nmi_source->flags
 * - acpi_madt_lapic_nmi->flags
 * - acpi_madt_platform_interrupt_source->flags
 * - acpi_madt_x2apic_nmi->flags
 */
#define ACPI_MADT_POLARITY_MASK        0b11
#define ACPI_MADT_POLARITY_CONFORMING  0b00
#define ACPI_MADT_POLARITY_ACTIVE_HIGH 0b01
#define ACPI_MADT_POLARITY_ACTIVE_LOW  0b11

#define ACPI_MADT_TRIGGERING_MASK       0b1100
#define ACPI_MADT_TRIGGERING_CONFORMING 0b0000
#define ACPI_MADT_TRIGGERING_EDGE       0b0100
#define ACPI_MADT_TRIGGERING_LEVEL      0b1100

PACKED(struct acpi_madt_interrupt_source_override {
    struct acpi_entry_hdr hdr;
    uint8_t               bus;
    uint8_t               source;
    uint32_t              gsi;
    uint16_t              flags;
})
ASSERT_SIZE(struct acpi_madt_interrupt_source_override, 10);

PACKED(struct acpi_madt_nmi_source {
    struct acpi_entry_hdr hdr;
    uint16_t              flags;
    uint32_t              gsi;
})
ASSERT_SIZE(struct acpi_madt_nmi_source, 8);

PACKED(struct acpi_madt_lapic_nmi {
    struct acpi_entry_hdr hdr;
    uint8_t               uid;
    uint16_t              flags;
    uint8_t               lint;
})
ASSERT_SIZE(struct acpi_madt_lapic_nmi, 6);

PACKED(struct acpi_madt_lapic_address_override {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd;
    uint64_t              address;
})
ASSERT_SIZE(struct acpi_madt_lapic_address_override, 12);

PACKED(struct acpi_madt_iosapic {
    struct acpi_entry_hdr hdr;
    uint8_t               id;
    uint8_t               rsvd;
    uint32_t              gsi_base;
    uint64_t              address;
})
ASSERT_SIZE(struct acpi_madt_iosapic, 16);

PACKED(struct acpi_madt_lsapic {
    struct acpi_entry_hdr hdr;
    uint8_t               acpi_id;
    uint8_t               id;
    uint8_t               eid;
    uint8_t               reserved[3];
    uint32_t              flags;
    uint32_t              uid;
    char                  uid_string[];
})
ASSERT_SIZE(struct acpi_madt_lsapic, 16);

// acpi_madt_platform_interrupt_source->platform_flags
#define ACPI_CPEI_PROCESSOR_OVERRIDE (1 << 0)

PACKED(struct acpi_madt_platform_interrupt_source {
    struct acpi_entry_hdr hdr;
    uint16_t              flags;
    uint8_t               type;
    uint8_t               processor_id;
    uint8_t               processor_eid;
    uint8_t               iosapic_vector;
    uint32_t              gsi;
    uint32_t              platform_flags;
})
ASSERT_SIZE(struct acpi_madt_platform_interrupt_source, 16);

PACKED(struct acpi_madt_x2apic {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd;
    uint32_t              id;
    uint32_t              flags;
    uint32_t              uid;
})
ASSERT_SIZE(struct acpi_madt_x2apic, 16);

PACKED(struct acpi_madt_x2apic_nmi {
    struct acpi_entry_hdr hdr;
    uint16_t              flags;
    uint32_t              uid;
    uint8_t               lint;
    uint8_t               reserved[3];
})
ASSERT_SIZE(struct acpi_madt_x2apic_nmi, 12);

// acpi_madt_gicc->flags
#define ACPI_GICC_ENABLED                         (1 << 0)
#define ACPI_GICC_PERF_INTERRUPT_MODE             (1 << 1)
#define ACPI_GICC_VGIC_MAINTENANCE_INTERRUPT_MODE (1 << 2)
#define ACPI_GICC_ONLINE_CAPABLE                  (1 << 3)

// ACPI_GICC_*_INTERRUPT_MODE
#define ACPI_GICC_TRIGGERING_EDGE  1
#define ACPI_GICC_TRIGGERING_LEVEL 0

PACKED(struct acpi_madt_gicc {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd0;
    uint32_t              interface_number;
    uint32_t              acpi_id;
    uint32_t              flags;
    uint32_t              parking_protocol_version;
    uint32_t              perf_interrupt_gsiv;
    uint64_t              parked_address;
    uint64_t              address;
    uint64_t              gicv;
    uint64_t              gich;
    uint32_t              vgic_maitenante_interrupt;
    uint64_t              gicr_base_address;
    uint64_t              mpidr;
    uint8_t               power_efficiency_class;
    uint8_t               rsvd1;
    uint16_t              spe_overflow_interrupt;
    uint16_t              trbe_interrupt;
})
ASSERT_SIZE(struct acpi_madt_gicc, 82);

PACKED(struct acpi_madt_gicd {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd0;
    uint32_t              id;
    uint64_t              address;
    uint32_t              system_vector_base;
    uint8_t               gic_version;
    uint8_t               reserved1[3];
})
ASSERT_SIZE(struct acpi_madt_gicd, 24);

// acpi_madt_gic_msi_frame->flags
#define ACPI_SPI_SELECT (1 << 0)

PACKED(struct acpi_madt_gic_msi_frame {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd;
    uint32_t              id;
    uint64_t              address;
    uint32_t              flags;
    uint16_t              spi_count;
    uint16_t              spi_base;
})
ASSERT_SIZE(struct acpi_madt_gic_msi_frame, 24);

PACKED(struct acpi_madt_gicr {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd;
    uint64_t              address;
    uint32_t              length;
})
ASSERT_SIZE(struct acpi_madt_gicr, 16);

PACKED(struct acpi_madt_gic_its {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd0;
    uint32_t              id;
    uint64_t              address;
    uint32_t              rsvd1;
})
ASSERT_SIZE(struct acpi_madt_gic_its, 20);

PACKED(struct acpi_madt_multiprocessor_wakeup {
    struct acpi_entry_hdr hdr;
    uint16_t              mailbox_version;
    uint32_t              rsvd;
    uint64_t              mailbox_address;
})
ASSERT_SIZE(struct acpi_madt_multiprocessor_wakeup, 16);

#define ACPI_CORE_PIC_ENABLED (1 << 0)

PACKED(struct acpi_madt_core_pic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint32_t              acpi_id;
    uint32_t              id;
    uint32_t              flags;
})
ASSERT_SIZE(struct acpi_madt_core_pic, 15);

PACKED(struct acpi_madt_lio_pic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint64_t              address;
    uint16_t              size;
    uint16_t              cascade_vector;
    uint64_t              cascade_vector_mapping;
})
ASSERT_SIZE(struct acpi_madt_lio_pic, 23);

PACKED(struct acpi_madt_ht_pic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint64_t              address;
    uint16_t              size;
    uint64_t              cascade_vector;
})
ASSERT_SIZE(struct acpi_madt_ht_pic, 21);

PACKED(struct acpi_madt_eio_pic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint8_t               cascade_vector;
    uint8_t               node;
    uint64_t              node_map;
})
ASSERT_SIZE(struct acpi_madt_eio_pic, 13);

PACKED(struct acpi_madt_msi_pic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint64_t              address;
    uint32_t              start;
    uint32_t              count;
})
ASSERT_SIZE(struct acpi_madt_msi_pic, 19);

PACKED(struct acpi_madt_bio_pic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint64_t              address;
    uint16_t              size;
    uint16_t              hardware_id;
    uint16_t              gsi_base;
})
ASSERT_SIZE(struct acpi_madt_bio_pic, 17);

PACKED(struct acpi_madt_lpc_pic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint64_t              address;
    uint16_t              size;
    uint16_t              cascade_vector;
})
ASSERT_SIZE(struct acpi_madt_lpc_pic, 15);

PACKED(struct acpi_madt_rintc {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint8_t               rsvd;
    uint32_t              flags;
    uint64_t              hart_id;
    uint32_t              uid;
    uint32_t              ext_intc_id;
    uint64_t              address;
    uint32_t              size;
})
ASSERT_SIZE(struct acpi_madt_rintc, 36);

PACKED(struct acpi_madt_imsic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint8_t               rsvd;
    uint32_t              flags;
    uint16_t              num_ids;
    uint16_t              num_guest_ids;
    uint8_t               guest_index_bits;
    uint8_t               hart_index_bits;
    uint8_t               group_index_bits;
    uint8_t               group_index_shift;
})
ASSERT_SIZE(struct acpi_madt_imsic, 16);

PACKED(struct acpi_madt_aplic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint8_t               id;
    uint32_t              flags;
    uint64_t              hardware_id;
    uint16_t              idc_count;
    uint16_t              sources_count;
    uint32_t              gsi_base;
    uint64_t              address;
    uint32_t              size;
})
ASSERT_SIZE(struct acpi_madt_aplic, 36);

PACKED(struct acpi_madt_plic {
    struct acpi_entry_hdr hdr;
    uint8_t               version;
    uint8_t               id;
    uint64_t              hardware_id;
    uint16_t              sources_count;
    uint16_t              max_priority;
    uint32_t              flags;
    uint32_t              size;
    uint64_t              address;
    uint32_t              gsi_base;
})
ASSERT_SIZE(struct acpi_madt_plic, 36);

enum acpi_srat_entry_type {
    ACPI_SRAT_ENTRY_TYPE_PROCESSOR_AFFINITY         = 0,
    ACPI_SRAT_ENTRY_TYPE_MEMORY_AFFINITY            = 1,
    ACPI_SRAT_ENTRY_TYPE_X2APIC_AFFINITY            = 2,
    ACPI_SRAT_ENTRY_TYPE_GICC_AFFINITY              = 3,
    ACPI_SRAT_ENTRY_TYPE_GIC_ITS_AFFINITY           = 4,
    ACPI_SRAT_ENTRY_TYPE_GENERIC_INITIATOR_AFFINITY = 5,
    ACPI_SRAT_ENTRY_TYPE_GENERIC_PORT_AFFINITY      = 6,
    ACPI_SRAT_ENTRY_TYPE_RINTC_AFFINITY             = 7,
};

PACKED(struct acpi_srat {
    struct acpi_sdt_hdr   hdr;
    uint32_t              rsvd0;
    uint64_t              rsvd1;
    struct acpi_entry_hdr entries[];
})
ASSERT_SIZE(struct acpi_srat, 48);

/*
 * acpi_srat_processor_affinity->flags
 * acpi_srat_x2apic_affinity->flags
 */
#define ACPI_SRAT_PROCESSOR_ENABLED (1 << 0)

PACKED(struct acpi_srat_processor_affinity {
    struct acpi_entry_hdr hdr;
    uint8_t               proximity_domain_low;
    uint8_t               id;
    uint32_t              flags;
    uint8_t               eid;
    uint8_t               proximity_domain_high[3];
    uint32_t              clock_domain;
})
ASSERT_SIZE(struct acpi_srat_processor_affinity, 16);

// acpi_srat_memory_affinity->flags
#define ACPI_SRAT_MEMORY_ENABLED      (1 << 0)
#define ACPI_SRAT_MEMORY_HOTPLUGGABLE (1 << 1)
#define ACPI_SRAT_MEMORY_NON_VOLATILE (1 << 2)

PACKED(struct acpi_srat_memory_affinity {
    struct acpi_entry_hdr hdr;
    uint32_t              proximity_domain;
    uint16_t              rsvd0;
    uint64_t              address;
    uint64_t              length;
    uint32_t              rsvd1;
    uint32_t              flags;
    uint64_t              rsdv2;
})
ASSERT_SIZE(struct acpi_srat_memory_affinity, 40);

PACKED(struct acpi_srat_x2apic_affinity {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd0;
    uint32_t              proximity_domain;
    uint32_t              id;
    uint32_t              flags;
    uint32_t              clock_domain;
    uint32_t              rsvd1;
})
ASSERT_SIZE(struct acpi_srat_x2apic_affinity, 24);

// acpi_srat_gicc_affinity->flags
#define ACPI_SRAT_GICC_ENABLED (1 << 0)

PACKED(struct acpi_srat_gicc_affinity {
    struct acpi_entry_hdr hdr;
    uint32_t              proximity_domain;
    uint32_t              uid;
    uint32_t              flags;
    uint32_t              clock_domain;
})
ASSERT_SIZE(struct acpi_srat_gicc_affinity, 18);

PACKED(struct acpi_srat_gic_its_affinity {
    struct acpi_entry_hdr hdr;
    uint32_t              proximity_domain;
    uint16_t              rsvd;
    uint32_t              id;
})
ASSERT_SIZE(struct acpi_srat_gic_its_affinity, 12);

// acpi_srat_generic_affinity->flags
#define ACPI_GENERIC_AFFINITY_ENABLED           (1 << 0)
#define ACPI_GENERIC_AFFINITY_ARCH_TRANSACTIONS (1 << 1)

PACKED(struct acpi_srat_generic_affinity {
    struct acpi_entry_hdr hdr;
    uint8_t               rsvd0;
    uint8_t               handle_type;
    uint32_t              proximity_domain;
    uint8_t               handle[16];
    uint32_t              flags;
    uint32_t              rsvd1;
})
ASSERT_SIZE(struct acpi_srat_generic_affinity, 32);

// acpi_srat_rintc_affinity->flags
#define ACPI_SRAT_RINTC_AFFINITY_ENABLED (1 << 0)

PACKED(struct acpi_srat_rintc_affinity {
    struct acpi_entry_hdr hdr;
    uint16_t              rsvd;
    uint32_t              proximity_domain;
    uint32_t              uid;
    uint32_t              flags;
    uint32_t              clock_domain;
})
ASSERT_SIZE(struct acpi_srat_rintc_affinity, 20);

PACKED(struct acpi_slit {
    struct acpi_sdt_hdr hdr;
    uint64_t            num_localities;
    uint8_t             matrix[];
})
ASSERT_SIZE(struct acpi_slit, 44);

/*
 * acpi_gtdt->el*_flags
 * acpi_gtdt_timer_entry->physical_flags
 * acpi_gtdt_timer_entry->virtual_flags
 * acpi_gtdt_watchdog->flags
 */
#define ACPI_GTDT_TRIGGERING       (1 << 0)
#define ACPI_GTDT_TRIGGERING_EDGE  1
#define ACPI_GTDT_TRIGGERING_LEVEL 0

/*
 * acpi_gtdt->el*_flags
 * acpi_gtdt_timer_entry->physical_flags
 * acpi_gtdt_timer_entry->virtual_flags
 * acpi_gtdt_watchdog->flags
 */
#define ACPI_GTDT_POLARITY             (1 << 1)
#define ACPI_GTDT_POLARITY_ACTIVE_LOW  1
#define ACPI_GTDT_POLARITY_ACTIVE_HIGH 0

// acpi_gtdt->el*_flags
#define ACPI_GTDT_ALWAYS_ON_CAPABLE (1 << 2)

PACKED(struct acpi_gtdt {
    struct acpi_sdt_hdr hdr;
    uint64_t            cnt_control_base;
    uint32_t            rsvd;
    uint32_t            el1_secure_gsiv;
    uint32_t            el1_secure_flags;
    uint32_t            el1_non_secure_gsiv;
    uint32_t            el1_non_secure_flags;
    uint32_t            el1_virtual_gsiv;
    uint32_t            el1_virtual_flags;
    uint32_t            el2_gsiv;
    uint32_t            el2_flags;
    uint64_t            cnt_read_base;
    uint32_t            platform_timer_count;
    uint32_t            platform_timer_offset;

    // revision >= 3
    uint32_t el2_virtual_gsiv;
    uint32_t el2_virtual_flags;
})
ASSERT_SIZE(struct acpi_gtdt, 104);

enum acpi_gtdt_entry_type {
    ACPI_GTDT_ENTRY_TYPE_TIMER    = 0,
    ACPI_GTDT_ENTRY_TYPE_WATCHDOG = 1,
};

PACKED(struct acpi_gtdt_entry_hdr {
    uint8_t  type;
    uint16_t length;
})

PACKED(struct acpi_gtdt_timer {
    struct acpi_gtdt_entry_hdr hdr;
    uint8_t                    rsvd;
    uint64_t                   cnt_ctl_base;
    uint32_t                   timer_count;
    uint32_t                   timer_offset;
})
ASSERT_SIZE(struct acpi_gtdt_timer, 20);

// acpi_gtdt_timer_entry->common_flags
#define ACPI_GTDT_TIMER_ENTRY_SECURE            (1 << 0)
#define ACPI_GTDT_TIMER_ENTRY_ALWAYS_ON_CAPABLE (1 << 1)

PACKED(struct acpi_gtdt_timer_entry {
    uint8_t  frame_number;
    uint8_t  rsvd[3];
    uint64_t cnt_base;
    uint64_t el0_cnt_base;
    uint32_t physical_gsiv;
    uint32_t physical_flags;
    uint32_t virtual_gsiv;
    uint32_t virtual_flags;
    uint32_t common_flags;
})
ASSERT_SIZE(struct acpi_gtdt_timer_entry, 40);

// acpi_gtdt_watchdog->flags
#define ACPI_GTDT_WATCHDOG_SECURE (1 << 2)

PACKED(struct acpi_gtdt_watchdog {
    struct acpi_gtdt_entry_hdr hdr;
    uint8_t                    rsvd;
    uint64_t                   refresh_frame;
    uint64_t                   control_frame;
    uint32_t                   gsiv;
    uint32_t                   flags;
})
ASSERT_SIZE(struct acpi_gtdt_watchdog, 28);

// acpi_fdt->iapc_flags
#define ACPI_IA_PC_LEGACY_DEVS  (1 << 0)
#define ACPI_IA_PC_8042         (1 << 1)
#define ACPI_IA_PC_NO_VGA       (1 << 2)
#define ACPI_IA_PC_NO_MSI       (1 << 3)
#define ACPI_IA_PC_NO_PCIE_ASPM (1 << 4)
#define ACPI_IA_PC_NO_CMOS_RTC  (1 << 5)

// acpi_fdt->flags
#define ACPI_WBINVD                    (1 << 0)
#define ACPI_WBINVD_FLUSH              (1 << 1)
#define ACPI_PROC_C1                   (1 << 2)
#define ACPI_P_LVL2_UP                 (1 << 3)
#define ACPI_PWR_BUTTON                (1 << 4)
#define ACPI_SLP_BUTTON                (1 << 5)
#define ACPI_FIX_RTC                   (1 << 6)
#define ACPI_RTC_S4                    (1 << 7)
#define ACPI_TMR_VAL_EXT               (1 << 8)
#define ACPI_DCK_CAP                   (1 << 9)
#define ACPI_RESET_REG_SUP             (1 << 10)
#define ACPI_SEALED_CASE               (1 << 11)
#define ACPI_HEADLESS                  (1 << 12)
#define ACPI_CPU_SW_SLP                (1 << 13)
#define ACPI_PCI_EXP_WAK               (1 << 14)
#define ACPI_USE_PLATFORM_CLOCK        (1 << 15)
#define ACPI_S4_RTC_STS_VALID          (1 << 16)
#define ACPI_REMOTE_POWER_ON_CAPABLE   (1 << 17)
#define ACPI_FORCE_APIC_CLUSTER_MODEL  (1 << 18)
#define ACPI_FORCE_APIC_PHYS_DEST_MODE (1 << 19)
#define ACPI_HW_REDUCED_ACPI           (1 << 20)
#define ACPI_LOW_POWER_S0_IDLE_CAPABLE (1 << 21)

// acpi_fdt->arm_flags
#define ACPI_ARM_PSCI_COMPLIANT (1 << 0)
#define ACPI_ARM_PSCI_USE_HVC   (1 << 1)

PACKED(struct acpi_fadt {
    struct acpi_sdt_hdr hdr;
    uint32_t            firmware_ctrl;
    uint32_t            dsdt;
    uint8_t             int_model;
    uint8_t             preferred_pm_profile;
    uint16_t            sci_int;
    uint32_t            smi_cmd;
    uint8_t             acpi_enable;
    uint8_t             acpi_disable;
    uint8_t             s4bios_req;
    uint8_t             pstate_cnt;
    uint32_t            pm1a_evt_blk;
    uint32_t            pm1b_evt_blk;
    uint32_t            pm1a_cnt_blk;
    uint32_t            pm1b_cnt_blk;
    uint32_t            pm2_cnt_blk;
    uint32_t            pm_tmr_blk;
    uint32_t            gpe0_blk;
    uint32_t            gpe1_blk;
    uint8_t             pm1_evt_len;
    uint8_t             pm1_cnt_len;
    uint8_t             pm2_cnt_len;
    uint8_t             pm_tmr_len;
    uint8_t             gpe0_blk_len;
    uint8_t             gpe1_blk_len;
    uint8_t             gpe1_base;
    uint8_t             cst_cnt;
    uint16_t            p_lvl2_lat;
    uint16_t            p_lvl3_lat;
    uint16_t            flush_size;
    uint16_t            flush_stride;
    uint8_t             duty_offset;
    uint8_t             duty_width;
    uint8_t             day_alrm;
    uint8_t             mon_alrm;
    uint8_t             century;
    uint16_t            iapc_boot_arch;
    uint8_t             rsvd;
    uint32_t            flags;
    struct acpi_gas     reset_reg;
    uint8_t             reset_value;
    uint16_t            arm_boot_arch;
    uint8_t             fadt_minor_verison;
    uint64_t            x_firmware_ctrl;
    uint64_t            x_dsdt;
    struct acpi_gas     x_pm1a_evt_blk;
    struct acpi_gas     x_pm1b_evt_blk;
    struct acpi_gas     x_pm1a_cnt_blk;
    struct acpi_gas     x_pm1b_cnt_blk;
    struct acpi_gas     x_pm2_cnt_blk;
    struct acpi_gas     x_pm_tmr_blk;
    struct acpi_gas     x_gpe0_blk;
    struct acpi_gas     x_gpe1_blk;
    struct acpi_gas     sleep_control_reg;
    struct acpi_gas     sleep_status_reg;
    uint64_t            hypervisor_vendor_identity;
})
ASSERT_SIZE(struct acpi_fadt, 276);

// acpi_facs->flags
#define ACPI_S4BIOS_F               (1 << 0)
#define ACPI_64BIT_WAKE_SUPPORTED_F (1 << 1)

// acpi_facs->ospm_flags
#define ACPI_64BIT_WAKE_F (1 << 0)

struct acpi_facs {
    char     signature[4];
    uint32_t length;
    uint32_t hardware_signature;
    uint32_t firmware_waking_vector;
    uint32_t global_lock;
    uint32_t flags;
    uint64_t x_firmware_waking_vector;
    uint8_t  version;
    char     rsvd0[3];
    uint32_t ospm_flags;
    char     rsvd1[24];
};
ASSERT_SIZE(struct acpi_facs, 64);

PACKED(struct acpi_mcfg_allocation {
    uint64_t address;
    uint16_t segment;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t rsvd;
})
ASSERT_SIZE(struct acpi_mcfg_allocation, 16);

PACKED(struct acpi_mcfg {
    struct acpi_sdt_hdr         hdr;
    uint64_t                    rsvd;
    struct acpi_mcfg_allocation entries[];
})
ASSERT_SIZE(struct acpi_mcfg, 44);

// acpi_hpet->block_id
#define ACPI_HPET_PCI_VENDOR_ID_SHIFT                    16
#define ACPI_HPET_LEGACY_REPLACEMENT_IRQ_ROUTING_CAPABLE (1 << 15)
#define ACPI_HPET_COUNT_SIZE_CAP                         (1 << 13)
#define ACPI_HPET_NUMBER_OF_COMPARATORS_SHIFT            8
#define ACPI_HPET_NUMBER_OF_COMPARATORS_MASK             0b11111
#define ACPI_HPET_HARDWARE_REV_ID_MASK                   0b11111111

// acpi_hpet->flags
#define ACPI_HPET_PAGE_PROTECTION_MASK 0b11
#define ACPI_HPET_PAGE_NO_PROTECTION   0
#define ACPI_HPET_PAGE_4K_PROTECTED    1
#define ACPI_HPET_PAGE_64K_PROTECTED   2

PACKED(struct acpi_hpet {
    struct acpi_sdt_hdr hdr;
    uint32_t            block_id;
    struct acpi_gas     address;
    uint8_t             number;
    uint16_t            min_clock_tick;
    uint8_t             flags;
})
ASSERT_SIZE(struct acpi_hpet, 56);

// PM1{a,b}_STS
#define ACPI_PM1_STS_TMR_STS_IDX         0
#define ACPI_PM1_STS_BM_STS_IDX          4
#define ACPI_PM1_STS_GBL_STS_IDX         5
#define ACPI_PM1_STS_PWRBTN_STS_IDX      8
#define ACPI_PM1_STS_SLPBTN_STS_IDX      9
#define ACPI_PM1_STS_RTC_STS_IDX         10
#define ACPI_PM1_STS_IGN0_IDX            11
#define ACPI_PM1_STS_PCIEXP_WAKE_STS_IDX 14
#define ACPI_PM1_STS_WAKE_STS_IDX        15

#define ACPI_PM1_STS_TMR_STS_MASK    (1 << ACPI_PM1_STS_TMR_STS_IDX)
#define ACPI_PM1_STS_BM_STS_MASK     (1 << ACPI_PM1_STS_BM_STS_IDX)
#define ACPI_PM1_STS_GBL_STS_MASK    (1 << ACPI_PM1_STS_GBL_STS_IDX)
#define ACPI_PM1_STS_PWRBTN_STS_MASK (1 << ACPI_PM1_STS_PWRBTN_STS_IDX)
#define ACPI_PM1_STS_SLPBTN_STS_MASK (1 << ACPI_PM1_STS_SLPBTN_STS_IDX)
#define ACPI_PM1_STS_RTC_STS_MASK    (1 << ACPI_PM1_STS_RTC_STS_IDX)
#define ACPI_PM1_STS_IGN0_MASK       (1 << ACPI_PM1_STS_IGN0_IDX)
#define ACPI_PM1_STS_PCIEXP_WAKE_STS_MASK \
    (1 << ACPI_PM1_STS_PCIEXP_WAKE_STS_IDX)
#define ACPI_PM1_STS_WAKE_STS_MASK (1 << ACPI_PM1_STS_WAKE_STS_IDX)

#define ACPI_PM1_STS_CLEAR 1

// PM1{a,b}_EN
#define ACPI_PM1_EN_TMR_EN_IDX          0
#define ACPI_PM1_EN_GBL_EN_IDX          5
#define ACPI_PM1_EN_PWRBTN_EN_IDX       8
#define ACPI_PM1_EN_SLPBTN_EN_IDX       9
#define ACPI_PM1_EN_RTC_EN_IDX          10
#define ACPI_PM1_EN_PCIEXP_WAKE_DIS_IDX 14

#define ACPI_PM1_EN_TMR_EN_MASK          (1 << ACPI_PM1_EN_TMR_EN_IDX)
#define ACPI_PM1_EN_GBL_EN_MASK          (1 << ACPI_PM1_EN_GBL_EN_IDX)
#define ACPI_PM1_EN_PWRBTN_EN_MASK       (1 << ACPI_PM1_EN_PWRBTN_EN_IDX)
#define ACPI_PM1_EN_SLPBTN_EN_MASK       (1 << ACPI_PM1_EN_SLPBTN_EN_IDX)
#define ACPI_PM1_EN_RTC_EN_MASK          (1 << ACPI_PM1_EN_RTC_EN_IDX)
#define ACPI_PM1_EN_PCIEXP_WAKE_DIS_MASK (1 << ACPI_PM1_EN_PCIEXP_WAKE_DIS_IDX)

// PM1{a,b}_CNT_BLK
#define ACPI_PM1_CNT_SCI_EN_IDX  0
#define ACPI_PM1_CNT_BM_RLD_IDX  1
#define ACPI_PM1_CNT_GBL_RLS_IDX 2
#define ACPI_PM1_CNT_RSVD0_IDX   3
#define ACPI_PM1_CNT_RSVD1_IDX   4
#define ACPI_PM1_CNT_RSVD2_IDX   5
#define ACPI_PM1_CNT_RSVD3_IDX   6
#define ACPI_PM1_CNT_RSVD4_IDX   7
#define ACPI_PM1_CNT_RSVD5_IDX   8
#define ACPI_PM1_CNT_IGN0_IDX    9
#define ACPI_PM1_CNT_SLP_TYP_IDX 10
#define ACPI_PM1_CNT_SLP_EN_IDX  13
#define ACPI_PM1_CNT_RSVD6_IDX   14
#define ACPI_PM1_CNT_RSVD7_IDX   15

#define ACPI_SLP_TYP_MAX 0x7

#define ACPI_PM1_CNT_SCI_EN_MASK  (1 << ACPI_PM1_CNT_SCI_EN_IDX)
#define ACPI_PM1_CNT_BM_RLD_MASK  (1 << ACPI_PM1_CNT_BM_RLD_IDX)
#define ACPI_PM1_CNT_GBL_RLS_MASK (1 << ACPI_PM1_CNT_GBL_RLS_IDX)
#define ACPI_PM1_CNT_SLP_TYP_MASK (ACPI_SLP_TYP_MAX << ACPI_PM1_CNT_SLP_TYP_IDX)
#define ACPI_PM1_CNT_SLP_EN_MASK  (1 << ACPI_PM1_CNT_SLP_EN_IDX)

/*
 * SCI_EN is not in this mask even though the spec says it must be preserved.
 * This is because it's known to be bugged on some hardware that relies on
 * software writing 1 to it after resume (as indicated by a similar comment in
 * ACPICA)
 */
#define ACPI_PM1_CNT_PRESERVE_MASK                                   \
    ((1 << ACPI_PM1_CNT_RSVD0_IDX) | (1 << ACPI_PM1_CNT_RSVD1_IDX) | \
     (1 << ACPI_PM1_CNT_RSVD2_IDX) | (1 << ACPI_PM1_CNT_RSVD3_IDX) | \
     (1 << ACPI_PM1_CNT_RSVD4_IDX) | (1 << ACPI_PM1_CNT_RSVD5_IDX) | \
     (1 << ACPI_PM1_CNT_IGN0_IDX) | (1 << ACPI_PM1_CNT_RSVD6_IDX) |  \
     (1 << ACPI_PM1_CNT_RSVD7_IDX))

// PM2_CNT
#define ACPI_PM2_CNT_ARB_DIS_IDX  0
#define ACPI_PM2_CNT_ARB_DIS_MASK (1 << ACPI_PM2_CNT_ARB_DIS_IDX)

// All bits are reserved but this first one
#define ACPI_PM2_CNT_PRESERVE_MASK (~((uint64_t)ACPI_PM2_CNT_ARB_DIS_MASK))

// SLEEP_CONTROL_REG
#define ACPI_SLP_CNT_RSVD0_IDX   0
#define ACPI_SLP_CNT_IGN0_IDX    1
#define ACPI_SLP_CNT_SLP_TYP_IDX 2
#define ACPI_SLP_CNT_SLP_EN_IDX  5
#define ACPI_SLP_CNT_RSVD1_IDX   6
#define ACPI_SLP_CNT_RSVD2_IDX   7

#define ACPI_SLP_CNT_SLP_TYP_MASK (ACPI_SLP_TYP_MAX << ACPI_SLP_CNT_SLP_TYP_IDX)
#define ACPI_SLP_CNT_SLP_EN_MASK  (1 << ACPI_SLP_CNT_SLP_EN_IDX)

#define ACPI_SLP_CNT_PRESERVE_MASK                                  \
    ((1 << ACPI_SLP_CNT_RSVD0_IDX) | (1 << ACPI_SLP_CNT_IGN0_IDX) | \
     (1 << ACPI_SLP_CNT_RSVD1_IDX) | (1 << ACPI_SLP_CNT_RSVD2_IDX))

// SLEEP_STATUS_REG
#define ACPI_SLP_STS_WAK_STS_IDX 7

#define ACPI_SLP_STS_WAK_STS_MASK (1 << ACPI_SLP_STS_WAK_STS_IDX)

// All bits are reserved but this last one
#define ACPI_SLP_STS_PRESERVE_MASK (~((uint64_t)ACPI_SLP_STS_WAK_STS_MASK))

#define ACPI_SLP_STS_CLEAR 1

PACKED(struct acpi_dsdt {
    struct acpi_sdt_hdr hdr;
    uint8_t             definition_block[];
})

PACKED(struct acpi_ssdt {
    struct acpi_sdt_hdr hdr;
    uint8_t             definition_block[];
})

/*
 * ACPI 6.5 specification:
 * Bit [0] - Set if the device is present.
 * Bit [1] - Set if the device is enabled and decoding its resources.
 * Bit [2] - Set if the device should be shown in the UI.
 * Bit [3] - Set if the device is functioning properly (cleared if device
 *           failed its diagnostics).
 * Bit [4] - Set if the battery is present.
 */
#define ACPI_STA_RESULT_DEVICE_PRESENT         (1 << 0)
#define ACPI_STA_RESULT_DEVICE_ENABLED         (1 << 1)
#define ACPI_STA_RESULT_DEVICE_SHOWN_IN_UI     (1 << 2)
#define ACPI_STA_RESULT_DEVICE_FUNCTIONING     (1 << 3)
#define ACPI_STA_RESULT_DEVICE_BATTERY_PRESENT (1 << 4)

#define ACPI_REG_DISCONNECT 0
#define ACPI_REG_CONNECT    1

PACKED(struct acpi_ecdt {
    struct acpi_sdt_hdr hdr;
    struct acpi_gas     ec_control;
    struct acpi_gas     ec_data;
    uint32_t            uid;
    uint8_t             gpe_bit;
    char                ec_id[];
})
ASSERT_SIZE(struct acpi_ecdt, 65);

PACKED(struct acpi_rhct_hdr {
    uint16_t type;
    uint16_t length;
    uint16_t revision;
})
ASSERT_SIZE(struct acpi_rhct_hdr, 6);

// acpi_rhct->flags
#define ACPI_TIMER_CANNOT_WAKE_CPU (1 << 0)

PACKED(struct acpi_rhct {
    struct acpi_sdt_hdr  hdr;
    uint32_t             flags;
    uint64_t             timebase_frequency;
    uint32_t             node_count;
    uint32_t             nodes_offset;
    struct acpi_rhct_hdr entries[];
})
ASSERT_SIZE(struct acpi_rhct, 56);

enum acpi_rhct_entry_type {
    ACPI_RHCT_ENTRY_TYPE_ISA_STRING = 0,
    ACPI_RHCT_ENTRY_TYPE_CMO        = 1,
    ACPI_RHCT_ENTRY_TYPE_MMU        = 2,
    ACPI_RHCT_ENTRY_TYPE_HART_INFO  = 65535,
};

PACKED(struct acpi_rhct_isa_string {
    struct acpi_rhct_hdr hdr;
    uint16_t             length;
    uint8_t              isa[];
})
ASSERT_SIZE(struct acpi_rhct_isa_string, 8);

PACKED(struct acpi_rhct_cmo {
    struct acpi_rhct_hdr hdr;
    uint8_t              rsvd;
    uint8_t              cbom_size;
    uint8_t              cbop_size;
    uint8_t              cboz_size;
})
ASSERT_SIZE(struct acpi_rhct_cmo, 10);

enum acpi_rhct_mmu_type {
    ACPI_RHCT_MMU_TYPE_SV39 = 0,
    ACPI_RHCT_MMU_TYPE_SV48 = 1,
    ACPI_RHCT_MMU_TYPE_SV57 = 2,
};

PACKED(struct acpi_rhct_mmu {
    struct acpi_rhct_hdr hdr;
    uint8_t              rsvd;
    uint8_t              type;
})
ASSERT_SIZE(struct acpi_rhct_mmu, 8);

PACKED(struct acpi_rhct_hart_info {
    struct acpi_rhct_hdr hdr;
    uint16_t             offset_count;
    uint32_t             uid;
    uint32_t             offsets[];
})
ASSERT_SIZE(struct acpi_rhct_hart_info, 12);

#define ACPI_LARGE_ITEM (1 << 7)

#define ACPI_SMALL_ITEM_NAME_IDX    3
#define ACPI_SMALL_ITEM_NAME_MASK   0xF
#define ACPI_SMALL_ITEM_LENGTH_MASK 0x7

#define ACPI_LARGE_ITEM_NAME_MASK 0x7F

// Small items
#define ACPI_RESOURCE_IRQ             0x04
#define ACPI_RESOURCE_DMA             0x05
#define ACPI_RESOURCE_START_DEPENDENT 0x06
#define ACPI_RESOURCE_END_DEPENDENT   0x07
#define ACPI_RESOURCE_IO              0x08
#define ACPI_RESOURCE_FIXED_IO        0x09
#define ACPI_RESOURCE_FIXED_DMA       0x0A
#define ACPI_RESOURCE_VENDOR_TYPE0    0x0E
#define ACPI_RESOURCE_END_TAG         0x0F

// Large items
#define ACPI_RESOURCE_MEMORY24                0x01
#define ACPI_RESOURCE_GENERIC_REGISTER        0x02
#define ACPI_RESOURCE_VENDOR_TYPE1            0x04
#define ACPI_RESOURCE_MEMORY32                0x05
#define ACPI_RESOURCE_FIXED_MEMORY32          0x06
#define ACPI_RESOURCE_ADDRESS32               0x07
#define ACPI_RESOURCE_ADDRESS16               0x08
#define ACPI_RESOURCE_EXTENDED_IRQ            0x09
#define ACPI_RESOURCE_ADDRESS64               0x0A
#define ACPI_RESOURCE_ADDRESS64_EXTENDED      0x0B
#define ACPI_RESOURCE_GPIO_CONNECTION         0x0C
#define ACPI_RESOURCE_PIN_FUNCTION            0x0D
#define ACPI_RESOURCE_SERIAL_CONNECTION       0x0E
#define ACPI_RESOURCE_PIN_CONFIGURATION       0x0F
#define ACPI_RESOURCE_PIN_GROUP               0x10
#define ACPI_RESOURCE_PIN_GROUP_FUNCTION      0x11
#define ACPI_RESOURCE_PIN_GROUP_CONFIGURATION 0x12
#define ACPI_RESOURCE_CLOCK_INPUT             0x13

/*
 * Resources as encoded by the raw AML byte stream.
 * For decode API & human usable structures refer to uacpi/resources.h
 */
PACKED(struct acpi_small_item { uint8_t type_and_length; })
ASSERT_SIZE(struct acpi_small_item, 1);

PACKED(struct acpi_resource_irq {
    struct acpi_small_item common;
    uint16_t               irq_mask;
    uint8_t                flags;
})
ASSERT_SIZE(struct acpi_resource_irq, 4);

PACKED(struct acpi_resource_dma {
    struct acpi_small_item common;
    uint8_t                channel_mask;
    uint8_t                flags;
})
ASSERT_SIZE(struct acpi_resource_dma, 3);

PACKED(struct acpi_resource_start_dependent {
    struct acpi_small_item common;
    uint8_t                flags;
})
ASSERT_SIZE(struct acpi_resource_start_dependent, 2);

PACKED(struct acpi_resource_end_dependent { struct acpi_small_item common; })
ASSERT_SIZE(struct acpi_resource_end_dependent, 1);

PACKED(struct acpi_resource_io {
    struct acpi_small_item common;
    uint8_t                information;
    uint16_t               minimum;
    uint16_t               maximum;
    uint8_t                alignment;
    uint8_t                length;
})
ASSERT_SIZE(struct acpi_resource_io, 8);

PACKED(struct acpi_resource_fixed_io {
    struct acpi_small_item common;
    uint16_t               address;
    uint8_t                length;
})
ASSERT_SIZE(struct acpi_resource_fixed_io, 4);

PACKED(struct acpi_resource_fixed_dma {
    struct acpi_small_item common;
    uint16_t               request_line;
    uint16_t               channel;
    uint8_t                transfer_width;
})
ASSERT_SIZE(struct acpi_resource_fixed_dma, 6);

PACKED(struct acpi_resource_vendor_defined_type0 {
    struct acpi_small_item common;
    uint8_t                byte_data[];
})
ASSERT_SIZE(struct acpi_resource_vendor_defined_type0, 1);

PACKED(struct acpi_resource_end_tag {
    struct acpi_small_item common;
    uint8_t                checksum;
})
ASSERT_SIZE(struct acpi_resource_end_tag, 2);

PACKED(struct acpi_large_item {
    uint8_t  type;
    uint16_t length;
})
ASSERT_SIZE(struct acpi_large_item, 3);

PACKED(struct acpi_resource_memory24 {
    struct acpi_large_item common;
    uint8_t                information;
    uint16_t               minimum;
    uint16_t               maximum;
    uint16_t               alignment;
    uint16_t               length;
})
ASSERT_SIZE(struct acpi_resource_memory24, 12);

PACKED(struct acpi_resource_vendor_defined_type1 {
    struct acpi_large_item common;
    uint8_t                byte_data[];
})
ASSERT_SIZE(struct acpi_resource_vendor_defined_type1, 3);

PACKED(struct acpi_resource_memory32 {
    struct acpi_large_item common;
    uint8_t                information;
    uint32_t               minimum;
    uint32_t               maximum;
    uint32_t               alignment;
    uint32_t               length;
})
ASSERT_SIZE(struct acpi_resource_memory32, 20);

PACKED(struct acpi_resource_fixed_memory32 {
    struct acpi_large_item common;
    uint8_t                information;
    uint32_t               address;
    uint32_t               length;
})
ASSERT_SIZE(struct acpi_resource_fixed_memory32, 12);

PACKED(struct acpi_resource_address {
    struct acpi_large_item common;
    uint8_t                type;
    uint8_t                flags;
    uint8_t                type_flags;
})
ASSERT_SIZE(struct acpi_resource_address, 6);

PACKED(struct acpi_resource_address64 {
    struct acpi_resource_address common;
    uint64_t                     granularity;
    uint64_t                     minimum;
    uint64_t                     maximum;
    uint64_t                     translation_offset;
    uint64_t                     length;
})
ASSERT_SIZE(struct acpi_resource_address64, 46);

PACKED(struct acpi_resource_address32 {
    struct acpi_resource_address common;
    uint32_t                     granularity;
    uint32_t                     minimum;
    uint32_t                     maximum;
    uint32_t                     translation_offset;
    uint32_t                     length;
})
ASSERT_SIZE(struct acpi_resource_address32, 26);

PACKED(struct acpi_resource_address16 {
    struct acpi_resource_address common;
    uint16_t                     granularity;
    uint16_t                     minimum;
    uint16_t                     maximum;
    uint16_t                     translation_offset;
    uint16_t                     length;
})
ASSERT_SIZE(struct acpi_resource_address16, 16);

PACKED(struct acpi_resource_address64_extended {
    struct acpi_resource_address common;
    uint8_t                      revision_id;
    uint8_t                      rsvd;
    uint64_t                     granularity;
    uint64_t                     minimum;
    uint64_t                     maximum;
    uint64_t                     translation_offset;
    uint64_t                     length;
    uint64_t                     attributes;
})
ASSERT_SIZE(struct acpi_resource_address64_extended, 56);

PACKED(struct acpi_resource_extended_irq {
    struct acpi_large_item common;
    uint8_t                flags;
    uint8_t                num_irqs;
    uint32_t               irqs[];
})
ASSERT_SIZE(struct acpi_resource_extended_irq, 5);

PACKED(struct acpi_resource_generic_register {
    struct acpi_large_item common;
    uint8_t                address_space_id;
    uint8_t                bit_width;
    uint8_t                bit_offset;
    uint8_t                access_size;
    uint64_t               address;
})
ASSERT_SIZE(struct acpi_resource_generic_register, 15);

PACKED(struct acpi_resource_gpio_connection {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint8_t                type;
    uint16_t               general_flags;
    uint16_t               connection_flags;
    uint8_t                pull_configuration;
    uint16_t               drive_strength;
    uint16_t               debounce_timeout;
    uint16_t               pin_table_offset;
    uint8_t                source_index;
    uint16_t               source_offset;
    uint16_t               vendor_data_offset;
    uint16_t               vendor_data_length;
})
ASSERT_SIZE(struct acpi_resource_gpio_connection, 23);

#define ACPI_SERIAL_TYPE_I2C  1
#define ACPI_SERIAL_TYPE_SPI  2
#define ACPI_SERIAL_TYPE_UART 3
#define ACPI_SERIAL_TYPE_CSI2 4
#define ACPI_SERIAL_TYPE_MAX  ACPI_SERIAL_TYPE_CSI2

PACKED(struct acpi_resource_serial {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint8_t                source_index;
    uint8_t                type;
    uint8_t                flags;
    uint16_t               type_specific_flags;
    uint8_t                type_specific_revision_id;
    uint16_t               type_data_length;
})
ASSERT_SIZE(struct acpi_resource_serial, 12);

PACKED(struct acpi_resource_serial_i2c {
    struct acpi_resource_serial common;
    uint32_t                    connection_speed;
    uint16_t                    slave_address;
})
ASSERT_SIZE(struct acpi_resource_serial_i2c, 18);

PACKED(struct acpi_resource_serial_spi {
    struct acpi_resource_serial common;
    uint32_t                    connection_speed;
    uint8_t                     data_bit_length;
    uint8_t                     phase;
    uint8_t                     polarity;
    uint16_t                    device_selection;
})
ASSERT_SIZE(struct acpi_resource_serial_spi, 21);

PACKED(struct acpi_resource_serial_uart {
    struct acpi_resource_serial common;
    uint32_t                    baud_rate;
    uint16_t                    rx_fifo;
    uint16_t                    tx_fifo;
    uint8_t                     parity;
    uint8_t                     lines_enabled;
})
ASSERT_SIZE(struct acpi_resource_serial_uart, 22);

PACKED(struct acpi_resource_serial_csi2 { struct acpi_resource_serial common; })
ASSERT_SIZE(struct acpi_resource_serial_csi2, 12);

PACKED(struct acpi_resource_pin_function {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint16_t               flags;
    uint8_t                pull_configuration;
    uint16_t               function_number;
    uint16_t               pin_table_offset;
    uint8_t                source_index;
    uint16_t               source_offset;
    uint16_t               vendor_data_offset;
    uint16_t               vendor_data_length;
})
ASSERT_SIZE(struct acpi_resource_pin_function, 18);

PACKED(struct acpi_resource_pin_configuration {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint16_t               flags;
    uint8_t                type;
    uint32_t               value;
    uint16_t               pin_table_offset;
    uint8_t                source_index;
    uint16_t               source_offset;
    uint16_t               vendor_data_offset;
    uint16_t               vendor_data_length;
})
ASSERT_SIZE(struct acpi_resource_pin_configuration, 20);

PACKED(struct acpi_resource_pin_group {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint16_t               flags;
    uint16_t               pin_table_offset;
    uint16_t               source_lable_offset;
    uint16_t               vendor_data_offset;
    uint16_t               vendor_data_length;
})
ASSERT_SIZE(struct acpi_resource_pin_group, 14);

PACKED(struct acpi_resource_pin_group_function {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint16_t               flags;
    uint16_t               function;
    uint8_t                source_index;
    uint16_t               source_offset;
    uint16_t               source_lable_offset;
    uint16_t               vendor_data_offset;
    uint16_t               vendor_data_length;
})
ASSERT_SIZE(struct acpi_resource_pin_group_function, 17);

PACKED(struct acpi_resource_pin_group_configuration {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint16_t               flags;
    uint8_t                type;
    uint32_t               value;
    uint8_t                source_index;
    uint16_t               source_offset;
    uint16_t               source_lable_offset;
    uint16_t               vendor_data_offset;
    uint16_t               vendor_data_length;
})
ASSERT_SIZE(struct acpi_resource_pin_group_configuration, 20);

PACKED(struct acpi_resource_clock_input {
    struct acpi_large_item common;
    uint8_t                revision_id;
    uint16_t               flags;
    uint16_t               divisor;
    uint32_t               numerator;
    uint8_t                source_index;
})
ASSERT_SIZE(struct acpi_resource_clock_input, 13);
