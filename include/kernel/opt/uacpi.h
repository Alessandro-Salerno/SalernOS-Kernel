#include <arch/info.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/util.h>

#ifndef HAVE_OPT_UACPI
#error "cannot include this header without having opt/uacpi"
#endif

// NOTE: these types are definedi n uACPI, but the kernel's option system tries
// to keep things separate, and expose a consistent API to common and
// platform-specific code. Thus these types are redefined here. The uACPI option
// code in src/opt/uacpi/glue.c uses _Static_assert to ensure that these types
// match those from uACPI
typedef void *UACPIHandle;
typedef usize UACPISize;
typedef u8 UACPIUint8;
typedef u16 UACPIUint16;
typedef u32 UACPIUint32;
typedef u64 UACPIUint64;
typedef i8 UACPIInt8;
typedef i16 UACPIInt16;
typedef i32 UACPIInt32;
typedef i64 UACPIInt64;
typedef uptr UACPIPhysAddr;
typedef uptr UACPIIOAddr;

int arch_uacpi_io_map(UACPIIOAddr base, UACPISize len, UACPIHandle *out_handle);
void arch_uacpi_io_unmap(UACPIHandle handle);
int arch_uacpi_io_read8(UACPIHandle handle, UACPISize offset,
                        UACPIUint8 *out_value);
int arch_uacpi_io_read16(UACPIHandle handle, UACPISize offset,
                         UACPIUint16 *out_value);
int arch_uacpi_io_read32(UACPIHandle handle, UACPISize offset,
                         UACPIUint32 *out_value);
int arch_uacpi_io_write8(UACPIHandle handle, UACPISize offset,
                         UACPIUint8 value);
int arch_uacpi_io_write16(UACPIHandle handle, UACPISize offset,
                          UACPIUint16 value);
int arch_uacpi_io_write32(UACPIHandle handle, UACPISize offset,
                          UACPIUint32 value);
int arch_uacpi_set_irq(UACPIUint32 irq, InterruptHandler *isr);
int arch_uacpi_mask_irq(UACPIUint32 irq);
struct Cpu;
struct Cpu *arch_uacpi_work_cpu(void);
void arch_uacpi_irq_eoi(InterruptHandler *isr);
int arch_uacpi_early_init(void);

int uacpi_init(UACPIUint64 flags);
