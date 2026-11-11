#include <arch/cpu.h>
#include <arch/info.h>
#include <asm/ioctls.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/dev/gfx/fbdev.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/console.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/util.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <linux/fb.h>
#include <sys/mman.h>

static int fbdev_ioctl(void *devdata, u64 op, void *buf) {
    FBDev *fbdev = devdata;

    if (op == FBIOGET_FSCREENINFO) {
        return memcpy_to_user(buf, &fbdev->fscreeninfo,
                              sizeof(fbdev->fscreeninfo));
    } else if (op == FBIOGET_VSCREENINFO) {
        return memcpy_to_user(buf, &fbdev->vscreeninfo,
                              sizeof(fbdev->vscreeninfo));
    } else if (op == FBIOPUT_VSCREENINFO) {
        struct fb_var_screeninfo vscreeninfo;
        int ret = memcpy_from_user(&vscreeninfo, buf, sizeof(vscreeninfo));
        if (ret == 0) fbdev->vscreeninfo = vscreeninfo;
        return ret;
    } else if (op == FBIOPUTCMAP || op == FBIOBLANK) {
        return 0;
    }

    return ENOSYS;
}

static int fbdev_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_blksize = 0;
    out_stat->st_mode = 0777;
    return 0;
}

static int fbdev_mmap(void *devdata, VMMContext *vmm_context, void *hint,
                      usize size, int vmm_flags, MMUFlags mmu_flags, u64 off,
                      void **out_addr) {
    (void)off;

    FBDev *fbdev = devdata;
    void *phys = (void *)HHDM_TO_PHYS(fbdev->fb->address);

    *out_addr = vmm_map(vmm_context, hint, phys, size, vmm_flags,
                        mmu_flags | MMU_FLAGS_WC);
    return (*out_addr == NULL) ? ENOMEM : 0;
}

static DevOps g_fbdev_ops = {.type = VNODE_TYPE_CHARDEV,
                             .ioctl = fbdev_ioctl,
                             .mmap = fbdev_mmap,
                             .stat = fbdev_stat};

int fbdev_init(FBDev **out_fbdev) {
    log("initializing /dev/fb0 fbdev");
    Framebuffer *fb = info_get_fb();
    FBDev *fbdev = slab_alloc(sizeof(FBDev));
    fbdev->fb = fb;
    int ret = devfs_register(NULL, "fb0", 3, &g_fbdev_ops, fbdev,
                             &fbdev->vnode);
    if (ret != 0) goto end;

    fbdev->fscreeninfo.smem_len = fb->pitch *
        fb->height; /* Length of frame buffer mem */
    fbdev->fscreeninfo.type = FB_TYPE_PACKED_PIXELS; /* see FB_TYPE_* */
    fbdev->fscreeninfo.type_aux = 0; /* Interleave for interleaved Planes */
    fbdev->fscreeninfo.visual = FB_VISUAL_TRUECOLOR; /* see FB_VISUAL_* */
    fbdev->fscreeninfo.xpanstep = 0;  /* zero if no hardware panning  */
    fbdev->fscreeninfo.ypanstep = 0;  /* zero if no hardware panning  */
    fbdev->fscreeninfo.ywrapstep = 0; /* zero if no hardware ywrap    */
    fbdev->fscreeninfo.line_length = fb->pitch; /* length of a line in bytes */
    fbdev->fscreeninfo.mmio_len = fb->pitch *
        fb->height;                      /* Length of Memory Mapped I/O  */
    fbdev->fscreeninfo.accel = 0;        /* Indicate to driver which	*/
    fbdev->fscreeninfo.capabilities = 0; /* see FB_CAP_* */

    fbdev->vscreeninfo.xres = fbdev->vscreeninfo.xres_virtual = fb->width;
    fbdev->vscreeninfo.yres = fbdev->vscreeninfo.yres_virtual = fb->height;
    fbdev->vscreeninfo.bits_per_pixel = fb->bpp;
    fbdev->vscreeninfo.red = (struct fb_bitfield){.offset = fb->red_mask_shift,
                                                  .length = fb->red_mask_size};
    fbdev->vscreeninfo.green = (struct fb_bitfield){
        .offset = fb->green_mask_shift,
        .length = fb->green_mask_size};
    fbdev->vscreeninfo.blue = (struct fb_bitfield){
        .offset = fb->blue_mask_shift,
        .length = fb->blue_mask_size};
    fbdev->vscreeninfo.activate = FB_ACTIVATE_NOW;
    fbdev->vscreeninfo.vmode = FB_VMODE_NONINTERLACED;
    fbdev->vscreeninfo.width = -1;
    fbdev->vscreeninfo.height = -1;

end:
    if (out_fbdev != NULL) *out_fbdev = fbdev;

    return ret;
}
