#include <kernel/opt/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

#include "uacpi_util.h"

int acpi_impl_find_table_by_signature(const char *signature,
                                      ACPITable *out_acpi_table) {
    uacpi_table tbl;
    uacpi_status ret = uacpi_table_find_by_signature(signature, &tbl);
    if (ret != UACPI_STATUS_OK) goto end;

    out_acpi_table->phys_addr = tbl.ptr;
    out_acpi_table->virt_addr = (void *)tbl.virt_addr;
end:
    return uacpi_util_status_to_posix(ret);
}
