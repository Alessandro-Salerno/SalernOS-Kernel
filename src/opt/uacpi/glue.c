/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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

#include <arch/context.h>
#include <assert.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/opt/acpi.h>
#include <kernel/opt/pci.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/mmu.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <uacpi/acpi.h>
#include <uacpi/status.h>
#include <uacpi/types.h>

#include "uacpi_util.h"
#include <uacpi/kernel_api.h>
#include <uacpi/platform/arch_helpers.h>

#define UACPI_GLUE_DEFER_TIME (KNANOS_PER_SEC / 10UL)

_Static_assert(__builtin_types_compatible_p(uacpi_handle, opt_uacpi_handle_t),
               "uacpi_handle must be the same as opt_uacpi_handle_t");
_Static_assert(__builtin_types_compatible_p(uacpi_size, opt_uacpi_size_t),
               "uacpi_size must be the same as opt_uacpi_size_t");
_Static_assert(__builtin_types_compatible_p(uacpi_u8, opt_uacpi_u8_t),
               "uacpi_u8 must be the same as opt_uacpi_u8_t");
_Static_assert(__builtin_types_compatible_p(uacpi_u16, opt_uacpi_u16_t),
               "uacpi_u16 must be the same as opt_uacpi_u16_t");
_Static_assert(__builtin_types_compatible_p(uacpi_u32, opt_uacpi_u32_t),
               "uacpi_u32 must be the same as opt_uacpi_u32_t");
_Static_assert(__builtin_types_compatible_p(uacpi_u64, opt_uacpi_u64_t),
               "uacpi_u64 must be the same as opt_uacpi_u64_t");
_Static_assert(__builtin_types_compatible_p(uacpi_i8, opt_uacpi_i8_t),
               "uacpi_i8 must be the same as opt_uacpi_i8_t");
_Static_assert(__builtin_types_compatible_p(uacpi_i16, opt_uacpi_i16_t),
               "uacpi_i16 must be the same as opt_uacpi_i16_t");
_Static_assert(__builtin_types_compatible_p(uacpi_i32, opt_uacpi_i32_t),
               "uacpi_i32 must be the same as opt_uacpi_i32_t");
_Static_assert(__builtin_types_compatible_p(uacpi_i64, opt_uacpi_i64_t),
               "uacpi_i64 must be the same as opt_uacpi_i64_t");
_Static_assert(__builtin_types_compatible_p(uacpi_phys_addr,
                                            opt_uacpi_phys_addr_t),
               "uacpi_phys_addr must be the same as opt_uacpi_phys_addr_t");
_Static_assert(__builtin_types_compatible_p(uacpi_io_addr, opt_uacpi_io_addr_t),
               "uacpi_io_addr must be the same as opt_uacpi_phys_io_t");

struct uacpi_glue_isr_extra {
    uacpi_interrupt_handler handler;
    uacpi_handle            ctx;
};

struct uacpi_glue_global_waitlist {
    com_waitlist_t waitlist;
    size_t         events_left;
};

struct uacpi_glue_wrok {
    uacpi_work_handler handler;
    uacpi_handle       ctx;
};

static struct uacpi_glue_global_waitlist InFlightInterruptWaitlist = {0};
static struct uacpi_glue_global_waitlist ScheduledWorkWaitlist     = {0};

static void uacpi_glue_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)ctx;
    struct uacpi_glue_isr_extra *extra = isr->extra;

    __atomic_add_fetch(&InFlightInterruptWaitlist.events_left,
                       1,
                       __ATOMIC_SEQ_CST);

    extra->handler(extra->ctx);

    // Decrease the number and notify waiters if needed
    if (0 == __atomic_fetch_add(&InFlightInterruptWaitlist.events_left,
                                -1,
                                __ATOMIC_SEQ_CST)) {
        com_sys_sched_notify_all(&InFlightInterruptWaitlist.waitlist);
    }

    com_mm_slab_free(extra, sizeof(*extra));
}

// Returns the PHYSICAL address of the RSDP structure via *out_rsdp_address.
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    arch_rsdp_t *rsdp = arch_info_get_rsdp();
    if (NULL == rsdp) {
        return UACPI_STATUS_UNIMPLEMENTED;
    }

    *out_rsdp_address = (uacpi_phys_addr)(ARCH_HHDM_TO_PHYS(rsdp->address));
    return UACPI_STATUS_OK;
}

/*
 * Map a physical memory range starting at 'addr' with length 'len', and return
 * a virtual address that can be used to access it.
 *
 * NOTE: 'addr' may be misaligned, in this case the host is expected to round it
 *       down to the nearest page-aligned boundary and map that, while making
 *       sure that at least 'len' bytes are still mapped starting at 'addr'. The
 *       return value preserves the misaligned offset.
 *
 *       Example for uacpi_kernel_map(0x1ABC, 0xF00):
 *           1. Round down the 'addr' we got to the nearest page boundary.
 *              Considering a PAGE_SIZE of 4096 (or 0x1000), 0x1ABC rounded down
 *              is 0x1000, offset within the page is 0x1ABC - 0x1000 => 0xABC
 *           2. Requested 'len' is 0xF00 bytes, but we just rounded the address
 *              down by 0xABC bytes, so add those on top. 0xF00 + 0xABC =>
 * 0x19BC
 *           3. Round up the final 'len' to the nearest PAGE_SIZE boundary, in
 *              this case 0x19BC is 0x2000 bytes (2 pages if PAGE_SIZE is 4096)
 *           4. Call the VMM to map the aligned address 0x1000 (from step 1)
 *              with length 0x2000 (from step 3). Let's assume the returned
 *              virtual address for the mapping is 0xF000.
 *           5. Add the original offset within page 0xABC (from step 1) to the
 *              resulting virtual address 0xF000 + 0xABC => 0xFABC. Return it
 *              to uACPI.
 */
void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    return com_mm_vmm_map(NULL,
                          NULL,
                          (void *)addr,
                          len,
                          COM_MM_VMM_FLAGS_NOHINT | COM_MM_VMM_FLAGS_PHYSICAL,
                          ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE);
}

/*
 * Unmap a virtual memory range at 'addr' with a length of 'len' bytes.
 *
 * NOTE: 'addr' may be misaligned, see the comment above 'uacpi_kernel_map'.
 *       Similar steps to uacpi_kernel_map can be taken to retrieve the
 *       virtual address originally returned by the VMM for this mapping
 *       as well as its true length.
 */
void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    (void)addr;
    (void)len;
}

void uacpi_kernel_log(uacpi_log_level log_level, const uacpi_char *msg) {
    const char *lvlstr = "n/a";

    switch (log_level) {
        case UACPI_LOG_DEBUG:
            lvlstr = "debug";
            break;
        case UACPI_LOG_TRACE:
            lvlstr = "trace";
            break;
        case UACPI_LOG_INFO:
            lvlstr = "info";
            break;
        case UACPI_LOG_WARN:
            lvlstr = "warn";
            break;
        case UACPI_LOG_ERROR:
            lvlstr = "error";
            break;
        default:
            break;
    }

#if CONFIG_LOG_LEVEL < CONST_LOG_LEVEL_DEBUG
    (void)lvlstr;
    (void)msg;
#endif

    UACPI_UTIL_LOG("(%s) %s", lvlstr, msg);
}

/*
 * Convenience initialization/deinitialization hooks that will be called by
 * uACPI automatically when appropriate if compiled-in.
 */
#ifdef UACPI_KERNEL_INITIALIZATION
/*
 * This API is invoked for each initialization level so that appropriate parts
 * of the host kernel and/or glue code can be initialized at different stages.
 *
 * uACPI API that triggers calls to uacpi_kernel_initialize and the respective
 * 'current_init_lvl' passed to the hook at that stage:
 * 1. uacpi_initialize() -> UACPI_INIT_LEVEL_EARLY
 * 2. uacpi_namespace_load() -> UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED
 * 3. (start of) uacpi_namespace_initialize() ->
 * UACPI_INIT_LEVEL_NAMESPACE_LOADED
 * 4. (end of) uacpi_namespace_initialize() ->
 * UACPI_INIT_LEVEL_NAMESPACE_INITIALIZED
 */
uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl) {
    switch (current_init_lvl) {
        case UACPI_INIT_LEVEL_EARLY:
            COM_SYS_THREAD_WAITLIST_INIT(&InFlightInterruptWaitlist.waitlist);
            COM_SYS_THREAD_WAITLIST_INIT(&ScheduledWorkWaitlist.waitlist);
            break;
        default:
            break;
    }

    return UACPI_STATUS_OK;
}

void uacpi_kernel_deinitialize(void) {
}
#endif

/*
 * Open a PCI device at 'address' for reading & writing.
 *
 * Note that this must be able to open any arbitrary PCI device, not just those
 * detected during kernel PCI enumeration, since the following pattern is
 * relatively common in AML firmware:
 *    Device (THC0)
 *    {
 *        // Device at 00:10.06
 *        Name (_ADR, 0x00100006)  // _ADR: Address
 *
 *        OperationRegion (THCR, PCI_Config, Zero, 0x0100)
 *        Field (THCR, ByteAcc, NoLock, Preserve)
 *        {
 *            // Vendor ID field in the PCI configuration space
 *            VDID,   32
 *        }
 *
 *        // Check if the device at 00:10.06 actually exists, that is reading
 *        // from its configuration space returns something other than 0xFFs.
 *        If ((VDID != 0xFFFFFFFF))
 *        {
 *            // Actually create the rest of the device's body if it's present
 *            // in the system, otherwise skip it.
 *        }
 *    }
 *
 * The handle returned via 'out_handle' is used to perform IO on the
 * configuration space of the device.
 */
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address,
                                          uacpi_handle     *out_handle) {
    opt_pci_addr_t *kernel_addr = com_mm_slab_alloc(sizeof(*kernel_addr));
    kernel_addr->segment        = address.segment;
    kernel_addr->bus            = address.bus;
    kernel_addr->device         = address.device;
    kernel_addr->function       = address.function;

    *out_handle = (uacpi_handle)kernel_addr;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    com_mm_slab_free(handle, sizeof(opt_pci_addr_t));
}

/*
 * Read & write the configuration space of a previously open PCI device.
 */
uacpi_status uacpi_kernel_pci_read8(uacpi_handle device,
                                    uacpi_size   offset,
                                    uacpi_u8    *value) {
    opt_pci_addr_t *addr = (void *)device;
    *value               = opt_pci_read8(addr, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device,
                                     uacpi_size   offset,
                                     uacpi_u16   *value) {
    opt_pci_addr_t *addr = (void *)device;
    *value               = opt_pci_read16(addr, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device,
                                     uacpi_size   offset,
                                     uacpi_u32   *value) {
    opt_pci_addr_t *addr = (void *)device;
    *value               = opt_pci_read32(addr, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device,
                                     uacpi_size   offset,
                                     uacpi_u8     value) {
    opt_pci_addr_t *addr = (void *)device;
    opt_pci_write8(addr, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device,
                                      uacpi_size   offset,
                                      uacpi_u16    value) {
    opt_pci_addr_t *addr = (void *)device;
    opt_pci_write16(addr, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device,
                                      uacpi_size   offset,
                                      uacpi_u32    value) {
    opt_pci_addr_t *addr = (void *)device;
    opt_pci_write32(addr, offset, value);
    return UACPI_STATUS_OK;
}

/*
 * Map a SystemIO address at [base, base + len) and return a kernel-implemented
 * handle that can be used for reading and writing the IO range.
 *
 * NOTE: The x86 architecture uses the in/out family of instructions
 *       to access the SystemIO address space.
 */
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base,
                                 uacpi_size    len,
                                 uacpi_handle *out_handle) {
    return uacpi_util_posix_to_status(arch_uacpi_io_map(out_handle, base, len));
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    arch_uacpi_io_unmap(handle);
}

/*
 * Read/Write the IO range mapped via uacpi_kernel_io_map
 * at a 0-based 'offset' within the range.
 *
 * NOTE:
 * The x86 architecture uses the in/out family of instructions
 * to access the SystemIO address space.
 *
 * You are NOT allowed to break e.g. a 4-byte access into four 1-byte accesses.
 * Hardware ALWAYS expects accesses to be of the exact width.
 */
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle,
                                   uacpi_size   offset,
                                   uacpi_u8    *out_value) {
    return uacpi_util_posix_to_status(
        arch_uacpi_io_read8(out_value, handle, offset));
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle,
                                    uacpi_size   offset,
                                    uacpi_u16   *out_value) {
    return uacpi_util_posix_to_status(
        arch_uacpi_io_read16(out_value, handle, offset));
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle,
                                    uacpi_size   offset,
                                    uacpi_u32   *out_value) {
    return uacpi_util_posix_to_status(
        arch_uacpi_io_read32(out_value, handle, offset));
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle,
                                    uacpi_size   offset,
                                    uacpi_u8     in_value) {
    return uacpi_util_posix_to_status(
        arch_uacpi_io_write8(handle, offset, in_value));
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle,
                                     uacpi_size   offset,
                                     uacpi_u16    in_value) {
    return uacpi_util_posix_to_status(
        arch_uacpi_io_write16(handle, offset, in_value));
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle,
                                     uacpi_size   offset,
                                     uacpi_u32    in_value) {
    return uacpi_util_posix_to_status(
        arch_uacpi_io_write32(handle, offset, in_value));
}

/*
 * Allocate a block of memory of 'size' bytes.
 * The contents of the allocated memory are unspecified.
 */
void *uacpi_kernel_alloc(uacpi_size size) {
    // sizeof(uacpi_size) is likely 8, but we keep slab alignemnt guarantees
    uacpi_size alloc_size = size + 16;
    void      *buffer     = NULL;

    if (alloc_size < ARCH_PAGE_SIZE) {
        buffer = com_mm_slab_alloc(alloc_size);
        goto end;
    }

    size_t num_pages = (alloc_size + (ARCH_PAGE_SIZE - 1)) / ARCH_PAGE_SIZE;
    buffer = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_many(num_pages));

end:
    *(uacpi_size *)buffer = alloc_size;
    return (char *)buffer + 16;
}

#ifdef UACPI_NATIVE_ALLOC_ZEROED
/*
 * Allocate a block of memory of 'size' bytes.
 * The returned memory block is expected to be zero-filled.
 */
void *uacpi_kernel_alloc_zeroed(uacpi_size size) {
    // sizeof(uacpi_size) is likely 8, but we keep slab alignemnt guarantees
    uacpi_size alloc_size = size + 16;
    void      *buffer     = NULL;

    if (alloc_size < ARCH_PAGE_SIZE) {
        buffer = com_mm_slab_alloc(alloc_size);
        goto end;
    }

    size_t num_pages = (alloc_size + (ARCH_PAGE_SIZE - 1)) / ARCH_PAGE_SIZE;
    buffer = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_many_zero(num_pages));

end:
    *(uacpi_size *)buffer = alloc_size;
    return (char *)buffer + 16;
}
#endif

/*
 * Free a previously allocated memory block.
 *
 * 'mem' might be a NULL pointer. In this case, the call is assumed to be a
 * no-op.
 *
 * An optionally enabled 'size_hint' parameter contains the size of the original
 * allocation. Note that in some scenarios this incurs additional cost to
 * calculate the object size.
 */
#ifndef UACPI_SIZED_FREES
void uacpi_kernel_free(void *mem) {
    if (NULL == mem) {
        return;
    }

    void      *base      = (char *)mem - 16;
    uacpi_size size_hint = *(uacpi_size *)base;

    if (size_hint < ARCH_PAGE_SIZE) {
        com_mm_slab_free(base, size_hint);
        return;
    }

    size_t num_pages = (size_hint + (ARCH_PAGE_SIZE - 1)) / ARCH_PAGE_SIZE;
    com_mm_pmm_free_many((void *)ARCH_HHDM_TO_PHYS(base), num_pages);
}
#else
void uacpi_kernel_free(void *mem, uacpi_size size_hint) {
}
#endif

/*
 * Returns the number of nanosecond ticks elapsed since boot,
 * strictly monotonic.
 */
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    return ARCH_CPU_GET_TIME();
}

/*
 * Spin for N microseconds.
 */
void uacpi_kernel_stall(uacpi_u8 usec) {
    uintmax_t start      = ARCH_CPU_GET_TIME();
    uintmax_t usec_to_ns = (uintmax_t)usec * 1000UL;
    uintmax_t end        = start + usec_to_ns;

    while (ARCH_CPU_GET_TIME() < end) {
        ARCH_CPU_PAUSE();
    }
}

/*
 * Sleep for N milliseconds.
 */
void uacpi_kernel_sleep(uacpi_u64 msec) {
}

/*
 * Create/free an opaque non-recursive kernel mutex object.
 */
uacpi_handle uacpi_kernel_create_mutex(void) {
    kmutex_t *mutex = com_mm_slab_alloc(sizeof(*mutex));
    KMUTEX_INIT(mutex);
    return mutex;
}
void uacpi_kernel_free_mutex(uacpi_handle handle) {
    com_mm_slab_free(handle, sizeof(kmutex_t));
}

/*
 * Create/free an opaque kernel (semaphore-like) event object.
 */
uacpi_handle uacpi_kernel_create_event(void) {
}

void uacpi_kernel_free_event(uacpi_handle handle) {
}

/*
 * Returns a unique identifier of the currently executing thread.
 *
 * The returned thread id cannot be UACPI_THREAD_ID_NONE.
 */
uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL != curr_thread) {
        return (uacpi_thread_id)(uintmax_t)curr_thread->tid;
    }
    return (uacpi_thread_id)curr_thread;
}

/*
 * Try to acquire the mutex with a millisecond timeout.
 *
 * The timeout value has the following meanings:
 * 0x0000 - Attempt to acquire the mutex once, in a non-blocking manner
 * 0x0001...0xFFFE - Attempt to acquire the mutex for at least 'timeout'
 *                   milliseconds
 * 0xFFFF - Infinite wait, block until the mutex is acquired
 *
 * The following are possible return values:
 * 1. UACPI_STATUS_OK - successful acquire operation
 * 2. UACPI_STATUS_TIMEOUT - timeout reached while attempting to acquire (or the
 *                           single attempt to acquire was not successful for
 *                           calls with timeout=0)
 * 3. Any other value - signifies a host internal error and is treated as such
 */
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle,
                                        uacpi_u16    timeout) {
    kmutex_t *mutex = (void *)handle;

    if (0 == timeout) {
        if (kmutex_try_acquire(mutex)) {
            return UACPI_STATUS_OK;
        }
        return UACPI_STATUS_TIMEOUT;
    }

    // TODO: add check for timed wait
    kmutex_acquire(mutex);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    kmutex_release((kmutex_t *)handle);
}

/*
 * Try to wait for an event (counter > 0) with a millisecond timeout.
 * A timeout value of 0xFFFF implies infinite wait.
 *
 * The internal counter is decremented by 1 if wait was successful.
 *
 * A successful wait is indicated by returning UACPI_TRUE.
 */
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
}

/*
 * Signal the event object by incrementing its internal counter by 1.
 *
 * This function may be used in interrupt contexts.
 */
void uacpi_kernel_signal_event(uacpi_handle handle) {
}

/*
 * Reset the event counter to 0.
 */
void uacpi_kernel_reset_event(uacpi_handle handle) {
}

/*
 * Handle a firmware request.
 *
 * Currently either a Breakpoint or Fatal operators.
 */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req) {
    switch (req->type) {
        case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
            break;
        case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
            KDEBUG("acpi: fatal firmware error: type=%d code=%d arg=%d",
                   req->fatal.type,
                   req->fatal.code,
                   req->fatal.arg)
            break;
        default:
            return UACPI_STATUS_UNIMPLEMENTED;
    }

    return UACPI_STATUS_OK;
}

/*
 * Install an interrupt handler at 'irq', 'ctx' is passed to the provided
 * handler for every invocation.
 *
 * 'out_irq_handle' is set to a kernel-implemented value that can be used to
 * refer to this handler from other API.
 */
uacpi_status
uacpi_kernel_install_interrupt_handler(uacpi_u32               irq,
                                       uacpi_interrupt_handler handler,
                                       uacpi_handle            ctx,
                                       uacpi_handle           *out_irq_handle) {
    com_isr_t *isr = com_sys_interrupt_allocate(uacpi_glue_isr,
                                                arch_uacpi_irq_eoi);
    if (NULL == isr) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    struct uacpi_glue_isr_extra *extra = com_mm_slab_alloc(sizeof(*extra));
    extra->handler                     = handler;
    extra->ctx                         = ctx;
    isr->extra                         = extra;
    *out_irq_handle                    = isr;
    return uacpi_util_posix_to_status(arch_uacpi_set_irq(irq, isr));
}

/*
 * Uninstall an interrupt handler. 'irq_handle' is the value returned via
 * 'out_irq_handle' during installation.
 */
uacpi_status
uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler,
                                         uacpi_handle            irq_handle) {
    (void)handler;
    com_isr_t *isr = (void *)irq_handle;
    com_sys_interrupt_free(isr);
    return UACPI_STATUS_OK;
}

/*
 * Create/free a kernel spinlock object.
 *
 * Unlike other types of locks, spinlocks may be used in interrupt contexts.
 */
uacpi_handle uacpi_kernel_create_spinlock(void) {
    kspinlock_t *spinlock = com_mm_slab_alloc(sizeof(kspinlock_t));
    *spinlock             = KSPINLOCK_NEW();
    return (uacpi_handle)spinlock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    kspinlock_t *spinlock = (void *)handle;
    KASSERT(!KSPINLOCK_IS_HELD(spinlock));
    com_mm_slab_free(spinlock, sizeof(*spinlock));
}

/*
 * Lock/unlock helpers for spinlocks.
 *
 * These are expected to disable interrupts, returning the previous state of cpu
 * flags, that can be used to possibly re-enable interrupts if they were enabled
 * before.
 *
 * Note that lock is infalliable.
 */
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    kspinlock_t *spinlock = (void *)handle;
    kspinlock_acquire(spinlock);
    return 0; // we use critsec counter, at least for now
}

void uacpi_kernel_unlock_spinlock(uacpi_handle    handle,
                                  uacpi_cpu_flags cpu_flags) {
    (void)cpu_flags;
    kspinlock_t *spinlock = (void *)handle;
    kspinlock_release(spinlock);
}

/*
 * Schedules deferred work for execution.
 * Might be invoked from an interrupt context.
 */
uacpi_status uacpi_kernel_schedule_work(uacpi_work_type    type,
                                        uacpi_work_handler handler,
                                        uacpi_handle       ctx) {
    struct uacpi_glue_wrok *work = com_mm_slab_alloc(sizeof(*work));
    work->handler                = handler;
    work->ctx                    = ctx;

    switch (type) {
        case UACPI_WORK_GPE_EXECUTION:
            break;
        case UACPI_WORK_NOTIFICATION:
            break;
        default:
            return UACPI_STATUS_UNIMPLEMENTED;
    }

    return UACPI_STATUS_OK;
}

/*
 * Waits for two types of work to finish:
 * 1. All in-flight interrupts installed via
 * uacpi_kernel_install_interrupt_handler
 * 2. All work scheduled via uacpi_kernel_schedule_work
 *
 * Note that the waits must be done in this order specifically.
 */
uacpi_status uacpi_kernel_wait_for_work_completion(void) {
}
