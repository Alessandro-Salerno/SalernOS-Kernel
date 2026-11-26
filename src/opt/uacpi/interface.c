#include <kernel/com/sys/powerman.h>
#include <kernel/com/util.h>
#include <kernel/opt/uacpi.h>
#include <uacpi/event.h>
#include <uacpi/sleep.h>
#include <uacpi/uacpi.h>

#include "uacpi_util.h"

static uacpi_interrupt_ret generic_event_handler(uacpi_handle ctx) {
    typedef int EventHandlerFn(void);
    int posix_ret = ((EventHandlerFn *)ctx)();
    if (posix_ret == 0) return UACPI_INTERRUPT_HANDLED;
    else return UACPI_INTERRUPT_NOT_HANDLED;
}

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
    if (unlikely(ret != UACPI_STATUS_OK)) goto end;

    ret = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON,
                                            generic_event_handler,
                                            powerman_request_shutdown);

end:
    return uacpi_util_status_to_posix(ret);
}

int uacpi_prepare_sleep(ACPISleepState state) {
    return uacpi_util_status_to_posix(
        uacpi_prepare_for_sleep_state(sleep_state_to_uacpi(state)));
}

int uacpi_enter_sleep(ACPISleepState state) {
    return uacpi_util_status_to_posix(
        uacpi_enter_sleep_state(sleep_state_to_uacpi(state)));
}
