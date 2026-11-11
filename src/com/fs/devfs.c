#include <errno.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/util.h>

struct DevFSDev {
    DevOps *devops;
    void *devdata;
};

static Vfs *g_devfs = NULL;

static struct DevFSDev *dev_of(Vnode *node) { return tmpfs_private(node); }

// VNODE OPS

static int devfs_open(Vnode *node, Vnode **out_vnode) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->open == NULL) {
        vfs_vnode_hold(node);
        *out_vnode = node;
        return 0;
    }

    return dev->devops->open(dev->devdata, out_vnode);
}

static int devfs_close(Vnode *node) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->close == NULL) return 0;

    return dev->devops->close(dev->devdata);
}

static void devfs_inactive(Vnode *node) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->inactive != NULL) dev->devops->inactive(dev->devdata);

    slab_free(dev, sizeof(struct DevFSDev));
    tmpfs_inactive(node);
}

static int devfs_read(void *buf, usize buflen, Vnode *node, u64 off, u64 flags,
                      usize *out_bytes_read) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->read == NULL) return ENOSYS;

    return dev->devops->read(buf, buflen, dev->devdata, off, flags,
                             out_bytes_read);
}

static int devfs_write(Vnode *node, void *buf, usize buflen, u64 off, u64 flags,
                       usize *out_bytes_written) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->write == NULL) return ENOSYS;

    return dev->devops->write(dev->devdata, buf, buflen, off, flags,
                              out_bytes_written);
}

static int devfs_ioctl(Vnode *node, u64 op, void *buf) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->ioctl == NULL) return ENOSYS;

    return dev->devops->ioctl(dev->devdata, op, buf);
}

static int devfs_isatty(Vnode *node) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->isatty == NULL) return ENOSYS;

    return dev->devops->isatty(dev->devdata);
}

static int devfs_seekable(Vnode *node) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->seekable == NULL) return 0;

    return dev->devops->seekable(dev->devdata);
}

static int devfs_stat(Vnode *node, struct stat *out_stat) {
    struct DevFSDev *dev = dev_of(node);
    int ret = dev->devops->stat != NULL
        ? dev->devops->stat(dev->devdata, out_stat)
        : ENOSYS;
    if (ret == 0 || ret == ENOSYS) out_stat->st_nlink = tmpfs_nlink(node);
    return ret;
}

static int devfs_poll_head(Vnode *node, struct PollHead **out_poll_head) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->poll_head == NULL) return ENOSYS;

    return dev->devops->poll_head(dev->devdata, out_poll_head);
}

static int devfs_poll(Vnode *node, short events, short *out_revents) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->poll == NULL) return ENOSYS;

    return dev->devops->poll(dev->devdata, events, out_revents);
}

static int devfs_mmap(Vnode *node, VMMContext *vmm_context, void *hint,
                      usize size, int vmm_flags, MMUFlags mmu_flags, u64 off,
                      void **out_addr) {
    struct DevFSDev *dev = dev_of(node);
    if (dev->devops->mmap == NULL) return ENOSYS;

    return dev->devops->mmap(dev->devdata, vmm_context, hint, size, vmm_flags,
                             mmu_flags, off, out_addr);
}

static VnodeOps g_devfs_node_ops = {.open = devfs_open,
                                    .close = devfs_close,
                                    .inactive = devfs_inactive,
                                    .read = devfs_read,
                                    .write = devfs_write,
                                    .ioctl = devfs_ioctl,
                                    .isatty = devfs_isatty,
                                    .seekable = devfs_seekable,
                                    .stat = devfs_stat,
                                    .poll_head = devfs_poll_head,
                                    .poll = devfs_poll,
                                    .mmap = devfs_mmap};

// INTERFACE

static int make_dev_attr(TmpFSSpecial *special, DevOps *devops, void *devdata,
                         VnodeAttr *out_attr, struct DevFSDev **out_dev) {
    assert(devops->type == VNODE_TYPE_CHARDEV ||
           devops->type == VNODE_TYPE_BLOCKDEV);

    *out_dev = slab_alloc(sizeof(struct DevFSDev));
    if (*out_dev == NULL) return ENOMEM;

    (*out_dev)->devops = devops;
    (*out_dev)->devdata = devdata;
    special->ops = &g_devfs_node_ops;
    special->private = *out_dev;
    *out_attr = VFS_ATTR(devops->type);
    out_attr->fsdata = special;
    return 0;
}

int devfs_register(Vnode *dir, const char *name, usize namelen, DevOps *devops,
                   void *devdata, Vnode **out_vnode) {
    VnodeAttr attr;
    TmpFSSpecial special;
    struct DevFSDev *dev;
    int ret = make_dev_attr(&special, devops, devdata, &attr, &dev);
    if (ret != 0) return ret;

    Vnode *node = NULL;
    ret = vfs_create(dir != NULL ? dir : g_devfs->root, name, namelen, &attr,
                     &node);
    if (ret != 0) {
        slab_free(dev, sizeof(struct DevFSDev));
        return ret;
    }

    if (out_vnode != NULL) *out_vnode = node;
    else vfs_vnode_rel(node);

    return 0;
}

int devfs_register_anonymous(DevOps *devops, void *devdata, Vnode **out_vnode) {
    VnodeAttr attr;
    TmpFSSpecial special;
    struct DevFSDev *dev;
    int ret = make_dev_attr(&special, devops, devdata, &attr, &dev);
    if (ret != 0) return ret;

    ret = tmpfs_new_orphan(g_devfs, &attr, out_vnode);
    if (ret != 0) slab_free(dev, sizeof(struct DevFSDev));

    return ret;
}

int devfs_mkdir(Vnode *parent, const char *name, usize namelen,
                Vnode **out_vnode) {
    return vfs_create(parent != NULL ? parent : g_devfs->root, name, namelen,
                      &VFS_ATTR(VNODE_TYPE_DIR), out_vnode);
}

void *devfs_get_data(Vnode *node) { return dev_of(node)->devdata; }

int devfs_init(Vfs *rootfs, Vfs **out_vfs) {
    log("mounting devfs in /dev/");
    *out_vfs = NULL;
    Vnode *dir = NULL;
    int ret = vfs_create(rootfs->root, "dev", 3, &VFS_ATTR(VNODE_TYPE_DIR),
                         &dir);

    // The initrd may already have /dev
    if (ret == EEXIST) {
        ret = vfs_lookup("dev", 3, rootfs->root, rootfs->root, 0, &dir);
    }
    if (ret != 0) return ret;

    Vfs *devfs = NULL;
    ret = tmpfs_mount(dir, &devfs);
    vfs_vnode_rel(dir);
    if (ret != 0) return ret;

    // Never dropped, so devfs is never unmounted
    vfs_vnode_hold(devfs->root);
    g_devfs = devfs;
    *out_vfs = devfs;
    return 0;
}
