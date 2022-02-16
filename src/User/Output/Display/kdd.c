#include "user/Output/Display/kdd.h"
#include "kernelpanic.h"


static framebuffer_t* boundFBO;


static uint32_t* __kdd_pxptr_get__(uint64_t __x, uint64_t __y) {
    SOFTASSERT(boundFBO != NULL, NULL);
    
    return (uint32_t*) {
        (__x * boundFBO->_BytesPerPixel                               ) +
        (__y * boundFBO->_BytesPerPixel * boundFBO->_PixelsPerScanLine) +
        boundFBO->_BaseAddress
    };
}

void kernel_kdd_fbo_bind(framebuffer_t* __fbo) {
    boundFBO = __fbo;
}

void kernel_kdd_fbo_clear(uint32_t __clearcolor) {
    SOFTASSERT(boundFBO != NULL, RETVOID);

    uint64_t _64bit_color = __clearcolor | ((uint64_t)(__clearcolor) << 32);
    for (uint64_t _i = 0; _i < boundFBO->_BufferSize; _i += 8)
        *(uint64_t*)((uint64_t)(boundFBO->_BaseAddress) + _i) = _64bit_color;
}

framebuffer_t* kernel_kdd_fbo_get() {
    return boundFBO;
}

uint32_t kernel_kdd_pxcolor_translate(uint8_t __r, uint8_t __g, uint8_t __b, uint8_t __a) {
    return __a << 24 |
           __r << 16 |
           __g << 8  |
           __b       ;
}

uint32_t kernel_kdd_pxcolor_get(uint32_t __x, uint32_t __y) {
    return *(__kdd_pxptr_get__(__x, __y));
}

void kernel_kdd_pxcolor_set(uint32_t __x, uint32_t __y, uint32_t __color) {
    *(__kdd_pxptr_get__(__x, __y)) = __color;
}
