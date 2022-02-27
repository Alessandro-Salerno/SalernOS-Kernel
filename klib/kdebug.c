#include "User/Output/Text/textrenderer.h"
#include "User/Output/Display/kdd.h"
#include <kstdio.h>
#include "kdebug.h"

#define INFO_FGCOLOR kernel_kdd_pxcolor_translate(0, 0, 0, 255)
#define INFO_BGCOLOR kernel_kdd_pxcolor_translate(255, 255, 255, 255)
#define INFO_TEXT    "DEBUG"

#define ERR_FGCOLOR  kernel_kdd_pxcolor_translate(255, 255, 255, 255)
#define ERR_BGCOLOR  kernel_kdd_pxcolor_translate(255, 0, 0, 255)
#define ERR_TEXT     "ERROR"

#define WARN_FGCOLOR kernel_kdd_pxcolor_translate(0, 0, 0, 255)
#define WARN_BGCOLOR kernel_kdd_pxcolor_translate(255, 255, 0, 255)
#define WARN_TEXT    "WARNING"

#define OK_FGCOLOR   kernel_kdd_pxcolor_translate(0, 0, 0, 255)
#define OK_BGCOLOR   kernel_kdd_pxcolor_translate(0, 255, 0, 255)
#define OK_TEXT      "OK"


static void __putmsg__(uint32_t __color, uint32_t __backcolor, const char* __type, const char* __msg) {
    uint32_t _color,
             _backcolor;

    uint32_t _xoff,
             _yoff;

    kernel_text_info_get(&_color, &_backcolor, &_xoff, &_yoff, NULL);
    kernel_text_info_set(__color, __backcolor, _xoff, _yoff);

    kprintf(__type);
    kprintf(": ");
    kprintf(__msg);
    kprintf("\n");

    kernel_text_info_get(NULL, NULL, &_xoff, &_yoff, NULL);
    kernel_text_info_set(_color, _backcolor, _xoff, _yoff);
}

void kloginfo(const char* __msg) {
    __putmsg__(
        INFO_FGCOLOR, INFO_BGCOLOR,
        INFO_TEXT, __msg
    );
}

void klogerr(const char* __msg) {
    __putmsg__(
        ERR_FGCOLOR, ERR_BGCOLOR,
        ERR_TEXT, __msg
    );
}

void klogwarn(const char* __msg) {
    __putmsg__(
        WARN_FGCOLOR, WARN_BGCOLOR,
        WARN_TEXT, __msg
    );
}

void klogok(const char* __msg) {
    __putmsg__(
        OK_FGCOLOR, OK_BGCOLOR,
        OK_TEXT, __msg
    );
}
