#ifndef SALERNOS_CORE_KERNEL_DISPLAY_DRIVER
#define SALERNOS_CORE_KERNEL_DISPLAY_DRIVER

    #include <kerntypes.h>


    typedef struct Framebuffer {
        void*    _BaseAddress;
        size_t   _BufferSize;
        uint32_t _Width;
        uint32_t _Height;
        uint32_t _PixelsPerScanLine;
        uint8_t  _BytesPerPixel;
    } framebuffer_t;

    typedef struct BitmapFontHeader {
        uint8_t _Magic[2];
        uint8_t _Mode;
        uint8_t _CharSize;
    } bmpfonthdr_t;

    typedef struct bmpfont_t {
        bmpfonthdr_t* _Header;
        void*         _Buffer;
    } bmpfont_t;

    typedef struct Point {
        uint32_t x;
        uint32_t y;
    } point_t;


    /****************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ****************************************************************************************************/
    void            kernel_kdd_fbo_bind           (framebuffer_t* __fbo);
    void            kernel_kdd_fbo_clear          (uint32_t __clearcolor);
    framebuffer_t*  kernel_kdd_fbo_get            ();
    
    uint32_t        kernel_kdd_pxcolor_translate  (uint8_t  __r, uint8_t  __g, uint8_t __b, uint8_t __a);
    uint32_t        kernel_kdd_pxcolor_get        (uint32_t __x, uint32_t __y);
    void            kernel_kdd_pxcolor_set        (uint32_t __x, uint32_t __y, uint32_t __color);

#endif
