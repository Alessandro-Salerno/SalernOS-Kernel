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

#include <arch/cpu.h>
#include <arch/info.h>
#include <asm/ioctls.h>
#include <errno.h>
#include <kernel/com/dev/gfx/fbdev.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/console.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <lib/util.h>
#include <linux/fb.h>
#include <sys/mman.h>

static int fbdev_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_fbdev_t *fbdev = devdata;

    if (FBIOGET_FSCREENINFO == op) {
        *(struct fb_fix_screeninfo *)buf = fbdev->fscreeninfo;
        return 0;
    } else if (FBIOGET_VSCREENINFO == op) {
        *(struct fb_var_screeninfo *)buf = fbdev->vscreeninfo;
        return 0;
    } else if (FBIOPUT_VSCREENINFO == op) {
        fbdev->vscreeninfo = *(struct fb_var_screeninfo *)buf;
        return 0;
    } else if (FBIOPUTCMAP == op || FBIOBLANK == op) {
        return 0;
    }

    return ENOSYS;
}

static int fbdev_stat(struct stat *out, void *devdata) {
    out->st_blksize = 0;
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0777;
    out->st_mode |= S_IFCHR;
    return 0;
}

static int fbdev_mmap(void             **out,
                      void              *devdata,
                      com_vmm_context_t *vmm_context,
                      void              *hint,
                      size_t             size,
                      int                vmm_flags,
                      arch_mmu_flags_t   mmu_flags,
                      uintmax_t          off) {
    (void)off;

    com_fbdev_t *fbdev = devdata;
    void        *phys  = (void *)ARCH_HHDM_TO_PHYS(fbdev->fb->address);

    *out = com_mm_vmm_map(vmm_context,
                          hint,
                          phys,
                          size,
                          vmm_flags,
                          mmu_flags | ARCH_MMU_FLAGS_WC);
    return 0;
}

static com_dev_ops_t FbdevOps = {.ioctl = fbdev_ioctl,
                                 .mmap  = fbdev_mmap,
                                 .stat  = fbdev_stat};

int com_dev_gfx_fbdev_init(com_fbdev_t **out) {
    KLOG("initializing /dev/fb0 fbdev")
    arch_framebuffer_t *fb    = arch_info_get_fb();
    com_fbdev_t        *fbdev = com_mm_slab_alloc(sizeof(com_fbdev_t));
    fbdev->fb                 = fb;
    int ret                   = com_fs_devfs_register(&fbdev->vnode,
                                    NULL,
                                    "fb0",
                                    3,
                                    &FbdevOps,
                                    fbdev);
    if (0 != ret) {
        goto end;
    }

    fbdev->fscreeninfo.smem_len = fb->pitch *
                                  fb->height; /* Length of frame buffer mem */
    fbdev->fscreeninfo.type        = FB_TYPE_PACKED_PIXELS; /* see FB_TYPE_* */
    fbdev->fscreeninfo.type_aux    = 0; /* Interleave for interleaved Planes */
    fbdev->fscreeninfo.visual      = FB_VISUAL_TRUECOLOR; /* see FB_VISUAL_* */
    fbdev->fscreeninfo.xpanstep    = 0; /* zero if no hardware panning  */
    fbdev->fscreeninfo.ypanstep    = 0; /* zero if no hardware panning  */
    fbdev->fscreeninfo.ywrapstep   = 0; /* zero if no hardware ywrap    */
    fbdev->fscreeninfo.line_length = fb->pitch; /* length of a line in bytes */
    fbdev->fscreeninfo.mmio_len    = fb->pitch *
                                  fb->height; /* Length of Memory Mapped I/O  */
    fbdev->fscreeninfo.accel        = 0;      /* Indicate to driver which	*/
    fbdev->fscreeninfo.capabilities = 0;      /* see FB_CAP_* */

    fbdev->vscreeninfo.xres = fbdev->vscreeninfo.xres_virtual = fb->width;
    fbdev->vscreeninfo.yres = fbdev->vscreeninfo.yres_virtual = fb->height;
    fbdev->vscreeninfo.bits_per_pixel                         = fb->bpp;
    fbdev->vscreeninfo.red = (struct fb_bitfield){.offset = fb->red_mask_shift,
                                                  .length = fb->red_mask_size};
    fbdev->vscreeninfo.green = (struct fb_bitfield){
        .offset = fb->green_mask_shift,
        .length = fb->green_mask_size};
    fbdev->vscreeninfo.blue = (struct fb_bitfield){
        .offset = fb->blue_mask_shift,
        .length = fb->blue_mask_size};
    fbdev->vscreeninfo.activate = FB_ACTIVATE_NOW;
    fbdev->vscreeninfo.vmode    = FB_VMODE_NONINTERLACED;
    fbdev->vscreeninfo.width    = -1;
    fbdev->vscreeninfo.height   = -1;

end:
    if (NULL != out) {
        *out = fbdev;
    }

    return ret;
}
