#include "user/Output/Text/textman.h"
#include "kernelpanic.h"
#include <kmem.h>


static textbuffer_t boundTBO;


void kernel_ktm_tbo_bind(textbuffer_t __textbuff) {
    boundTBO = __textbuff;
}

textbuffer_t kernel_ktm_tbo_get() {
    return boundTBO;
}

void kernel_ktm_tbo_clear() {
    SOFTASSERT(boundTBO._Buffer != NULL, RETVOID);
    memset(boundTBO._Buffer, boundTBO._BufferSize, 0);
}

void kernel_ktm_text_shl(uint64_t __positions) {
    SOFTASSERT(boundTBO._Buffer != NULL, RETVOID);
    kernel_panic_assert(__positions < boundTBO._BufferSize, "");

    for (uint64_t _pos = __positions; _pos < boundTBO._BufferSize; _pos++)
        boundTBO._Buffer[_pos - __positions] = boundTBO._Buffer[_pos];

    boundTBO._EndIndex -= __positions;
}
