#pragma once

#include <arch/info.h>
#include <kernel/com/fs/vfs.h>
#include <linux/fb.h>
#include <sys/types.h>

typedef struct FBDev {
    Framebuffer *fb;
    Vnode *vnode;
    struct fb_fix_screeninfo fscreeninfo;
    struct fb_var_screeninfo vscreeninfo;
} FBDev;

int fbdev_init(FBDev **out_fbdev);
