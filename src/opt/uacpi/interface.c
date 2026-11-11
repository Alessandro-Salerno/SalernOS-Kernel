#include <kernel/com/util.h>
#include <kernel/opt/uacpi.h>
#include <uacpi/event.h>
#include <uacpi/uacpi.h>

#include "uacpi_util.h"

int uacpi_init(UACPIUint64 flags) {
    log("initializing uacpi");

    uacpi_status ret = uacpi_initialize(flags);
    if (unlikely(ret != UACPI_STATUS_OK)) goto end;

    int kernel_ret = arch_uacpi_early_init();
    if (kernel_ret != 0) return kernel_ret;

    ret = uacpi_namespace_load();
    if (unlikely(ret != UACPI_STATUS_OK)) goto end;

    ret = uacpi_namespace_initialize();
    if (unlikely(ret != UACPI_STATUS_OK)) goto end;

    ret = uacpi_finalize_gpe_initialization();

end:
    return uacpi_util_status_to_posix(ret);
}
