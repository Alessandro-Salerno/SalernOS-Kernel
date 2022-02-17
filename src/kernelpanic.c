#include "kernelpanic.h"
#include "User/Output/Text/kernelshell.h"
#include "User/Output/Text/textrenderer.h"


void kernel_panic_throw(const char* __message) {
    kernel_text_reinitialize(
        kernel_kdd_pxcolor_translate(255, 255, 255, 255),
        kernel_kdd_pxcolor_translate(47, 1, 97, 255),
        0, 0
    );
    
    kernel_shell_printf(":-( SalernOS Kernel Panic\n");
    kernel_shell_printf("Execution of the SalernOS Kernel has been halted due to a critical system fault, please read the following lines for more information.\n\n");
    kernel_shell_printf(__message);

    while (1);
}

static char formatMessage[128];
void kernel_panic_format(panicinfo_t __info) {
    
}

void kernel_panic_assert(uint8_t __cond, const char* __message) {
    SOFTASSERT(!(__cond), RETVOID);
    kernel_panic_throw(__message);
}
