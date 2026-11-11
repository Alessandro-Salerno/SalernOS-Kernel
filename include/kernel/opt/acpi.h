#pragma once

#include <arch/info.h>

#ifndef HAVE_OPT_ACPI
#error "cannot include this header without having opt/acpi"
#endif

#ifndef ACPI_IMPLEMENTATION
#error "cannot use opt/acpi without having an acpi implementation"
#endif

typedef struct ACPITable {
    void *phys_addr;
    void *virt_addr;
} ACPITable;

// these functions are implemennted by platforms using this option
RSDPInfo *info_get_rsdp(void);

// these functions are implemented by the option and used by platformes
int acpi_impl_find_table_by_signature(const char *signature,
                                      ACPITable *out_acpi_table);

// these functions are implemented by the ACPI option itself
int acpi_init_pci(void);
