#pragma once

#include <errno.h>
#include <kernel/com/util.h>
#include <kernel/opt/acpi.h>
#include <uacpi/sleep.h>
#include <uacpi/uacpi.h>

#define UACPI_UTIL_LOG(...) log_opt("UACPI", __VA_ARGS__)

used static inline uacpi_status uacpi_util_posix_to_status(int e) {
    switch (e) {
        case 0:
            return UACPI_STATUS_OK;
        case ENOSYS:
        case EOPNOTSUPP:
#if ENOTSUP != EOPNOTSUPP
        case ENOTSUP:
#endif
            return UACPI_STATUS_UNIMPLEMENTED;
        case EINVAL:
            return UACPI_STATUS_INVALID_ARGUMENT;
        case ENOMEM:
            return UACPI_STATUS_OUT_OF_MEMORY;
        case EPERM:
        case EFAULT:
            return UACPI_STATUS_DENIED;
        default:
            assert(!"unsupported error");
    }
}

used static int uacpi_util_status_to_posix(uacpi_status status) {
    switch (status) {
        case UACPI_STATUS_OK:
            return 0;
        case UACPI_STATUS_UNIMPLEMENTED:
            return ENOSYS;
        case UACPI_STATUS_INVALID_ARGUMENT:
            return EINVAL;
        case UACPI_STATUS_OUT_OF_MEMORY:
            return ENOMEM;
        case UACPI_STATUS_DENIED:
            return EPERM;
        default:
            log_debug("unknown uacpi status %d", status);
            assert(!"unknown uacpi status");
    }
}

static inline uacpi_sleep_state sleep_state_to_uacpi(ACPISleepState state) {
    switch (state) {
        case ACPI_SLEEP_S0:
            return UACPI_SLEEP_STATE_S0;
        case ACPI_SLEEP_S1:
            return UACPI_SLEEP_STATE_S1;
        case ACPI_SLEEP_S2:
            return UACPI_SLEEP_STATE_S2;
        case ACPI_SLEEP_S3:
            return UACPI_SLEEP_STATE_S3;
        case ACPI_SLEEP_S4:
            return UACPI_SLEEP_STATE_S4;
        case ACPI_SLEEP_S5:
            return UACPI_SLEEP_STATE_S5;
        default:
            assert(!"invalid uacpi sleep state");
    }
}
