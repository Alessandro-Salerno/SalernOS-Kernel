#include <kernel/com/io/log.h>
#include <vendor/printf.h>

// This is used by the printf implementation to write to the log
void _putchar(char character) {
    log_putc_nolock(character);
}
