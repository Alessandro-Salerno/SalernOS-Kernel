#include <arch/cpu.h>
#include <kernel/com/util.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/power.h>

noreturn void power_shutdown(void) {
    // We assert on all for now because it doesn't make much sense to do
    // anything else, since these should only be called byu init after it has
    // done everything it had to do
    verify(uacpi_prepare_sleep(ACPI_SLEEP_S5) == 0);
    cpu_disable_interrupts();
    verify(uacpi_enter_sleep(ACPI_SLEEP_S5) == 0);
}
