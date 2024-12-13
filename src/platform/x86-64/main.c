/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <stdint.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

static arch_cpu_t BaseCpu = {0};
//
// USER_DATA volatile const char *Proc1Message = "Hello from process 1!\n";
//
// USER_DATA volatile const char *Proc2Message = "Hello from process 2!\n";
//
// USER_TEXT void proc1entry(void) {
//   while (1) {
//     asm volatile("movq $0x0, %%rax\n"
//                  "movq %0, %%rdi\n"
//                  "int $0x80\n"
//                  :
//                  : "b"(Proc1Message)
//                  : "%rax", "%rdi", "%rcx", "%rdx", "memory");
//   }
// }
//
// USER_TEXT void proc2entry(void) {
//   while (1) {
//     asm volatile("movq $0x0, %%rax\n"
//                  "movq %0, %%rdi\n"
//                  "int $0x80\n"
//                  :
//                  : "b"(Proc2Message)
//                  : "%rax", "%rdi", "%rcx", "%rdx", "memory");
//   }
// }
//
// USED static void sched(com_isr_t *isr, arch_context_t *ctx) {
//   (void)isr;
//   com_sys_sched_run(ctx);
// }

#include <libaio.h>
struct dummyfs_node {
  char name[100];
  union {
    struct dummyfs_node *directory;
    char                 buf[100];
  };
  com_vnode_t *vnode;
  bool         present;
};

extern com_vfs_ops_t   DummyfsOps;
extern com_vnode_ops_t DummyfsNodeOps;

static int kstrcmp(const char *str1, const char *str2, size_t max) {
  for (size_t i = 0; 0 != *str1 && 0 != *str2 && i < max; i++) {
    if (*str1 != *str2) {
      return 1;
    }

    str1++;
    str2++;
  }

  return 0;
}

static int dummyfs_mount(com_vfs_t **out, com_vnode_t *mountpoint) {
  DEBUG("mounting dummyfs in /");
  com_vfs_t *dummyfs        = com_mm_slab_alloc(sizeof(com_vfs_t));
  dummyfs->mountpoint       = mountpoint;
  dummyfs->ops              = &DummyfsOps;
  com_vnode_t         *root = com_mm_slab_alloc(sizeof(com_vnode_t));
  struct dummyfs_node *dfs_root =
      com_mm_slab_alloc(sizeof(struct dummyfs_node));
  root->type    = COM_VNODE_TYPE_DIR;
  root->isroot  = true;
  root->ops     = &DummyfsNodeOps;
  root->extra   = dfs_root;
  root->num_ref = 1;
  root->vfs     = dummyfs;
  dummyfs->root = root;

  dfs_root->vnode   = root;
  dfs_root->present = true;
  dfs_root->name[0] = 0;
  void *rootbuf     = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  kmemset(rootbuf, ARCH_PAGE_SIZE, 0);
  dfs_root->directory = rootbuf;

  if (NULL != mountpoint) {
    mountpoint->mountpointof = dummyfs;
    mountpoint->type         = COM_VNODE_TYPE_DIR;
  }

  *out = dummyfs;
  return 0;
}

static int dummyfs_vget(com_vnode_t **out, com_vfs_t *vfs, void *inode) {
  DEBUG("running dummyfs vget for inode %x", inode);
  (void)vfs;
  struct dummyfs_node *dn = inode;
  *out                    = dn->vnode;
  return 0;
}

static int dummyfs_create(com_vnode_t **out,
                          com_vnode_t  *dir,
                          const char   *name,
                          size_t        namelen,
                          uintmax_t     attr) {
  com_vnode_t *vn = com_mm_slab_alloc(sizeof(com_vnode_t));

  vn->vfs     = dir->vfs;
  vn->ops     = dir->ops;
  vn->type    = COM_VNODE_TYPE_FILE;
  vn->isroot  = false;
  vn->num_ref = 1;

  struct dummyfs_node *dirbuf = ((struct dummyfs_node *)dir->extra)->directory;
  struct dummyfs_node *first_free = NULL;

  for (size_t i = 0; i < ARCH_PAGE_SIZE / sizeof(struct dummyfs_node) - 1;
       i++) {
    if (!dirbuf[i].present) {
      DEBUG("dummyfs create: file created at index %u", i);
      first_free          = &dirbuf[i];
      first_free->present = true;
      break;
    }
  }

  ASSERT(NULL != first_free);

  first_free->present = true;
  kmemcpy(first_free->name, (void *)name, namelen);
  first_free->name[namelen] = 0;
  vn->extra                 = first_free;
  first_free->vnode         = vn;

  *out = vn;
  return 0;
}

static int dummyfs_write(com_vnode_t *node,
                         void        *buf,
                         size_t       buflen,
                         uintmax_t    off,
                         uintmax_t    flags) {
  (void)flags;
  struct dummyfs_node *dn      = node->extra;
  uint8_t             *bytebuf = buf;

  for (size_t i = off; i < 100 && i - off < buflen + off; i++) {
    dn->buf[i] = bytebuf[i - off];
  }

  return 0;
}

static int dummyfs_read(void        *buf,
                        size_t       buflen,
                        com_vnode_t *node,
                        uintmax_t    off,
                        uintmax_t    flags) {
  (void)flags;
  struct dummyfs_node *dn      = node->extra;
  uint8_t             *bytebuf = buf;

  for (size_t i = off; i < 100 && i - off < buflen; i++) {
    bytebuf[i - off] = dn->buf[i];
  }

  return 0;
}

static int dummyfs_lookup(com_vnode_t **out,
                          com_vnode_t  *dir,
                          const char   *name,
                          size_t        len) {
  struct dummyfs_node *inode  = ((struct dummyfs_node *)dir->extra);
  struct dummyfs_node *dirbuf = inode->directory;
  DEBUG("running dummyfs lookup on root=%u name=%s", dir->isroot, inode->name);
  struct dummyfs_node *found = NULL;
  size_t               max   = 100;
  if (len < max) {
    max = len;
  }

  for (size_t i = 0; i < ARCH_PAGE_SIZE / sizeof(struct dummyfs_node) - 1;
       i++) {
    DEBUG("dummyfs lookup: attempting index %u", i);
    if (dirbuf[i].present && 0 == kstrcmp(dirbuf[i].name, name, max)) {
      DEBUG("found: name=%s", dirbuf[i].name);
      found          = &dirbuf[i];
      found->present = true;
      break;
    }
  }

  ASSERT(NULL != found);

  *out = found->vnode;
  return 0;
}

com_vfs_ops_t DummyfsOps =
    (com_vfs_ops_t){.mount = dummyfs_mount, .vget = dummyfs_vget};
com_vnode_ops_t DummyfsNodeOps = (com_vnode_ops_t){.create = dummyfs_create,
                                                   .write  = dummyfs_write,
                                                   .read   = dummyfs_read,
                                                   .lookup = dummyfs_lookup};

void kernel_entry(void) {
  hdr_arch_cpu_set(&BaseCpu);
  TAILQ_INIT(&BaseCpu.sched_queue);
  com_log_set_hook(x86_64_e9_putc);
  x86_64_gdt_init();
  x86_64_idt_init();
  x86_64_idt_reload();
  com_mm_pmm_init();
  arch_mmu_init();

  // pmm tests
  void *a = com_mm_pmm_alloc();
  void *b = com_mm_pmm_alloc();
  DEBUG("a=%x b=%x", a, b);
  com_mm_pmm_free(b);
  void *c = com_mm_pmm_alloc();
  com_mm_pmm_free(a);
  void *d = com_mm_pmm_alloc();
  DEBUG("c=%x d=%x", c, d);
  com_mm_pmm_free(c);
  com_mm_pmm_free(d);

  void *slab_1 = com_mm_slab_alloc(256);
  DEBUG("slab_1=%x", slab_1);

  // x86_64_lapic_bsp_init();
  // x86_64_lapic_init();

  com_sys_syscall_init();
  x86_64_idt_set_user_invocable(0x80);
  // com_sys_interrupt_register(0x30, sched, x86_64_lapic_eoi);

  // arch_mmu_pagetable_t *user_pt = arch_mmu_new_table();
  // void                 *ustack  = (void
  // *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc()); arch_mmu_map(user_pt,
  //              ustack,
  //              (void *)ARCH_HHDM_TO_PHYS(ustack),
  //              ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
  //                  ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  //
  // com_proc_t   *proc = com_sys_proc_new(user_pt, 0);
  // com_thread_t *thread =
  //     com_sys_thread_new(proc, ustack, ARCH_PAGE_SIZE, proc1entry);
  //
  // user_pt = arch_mmu_new_table();
  // ustack  = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  // arch_mmu_map(user_pt,
  //              ustack,
  //              (void *)ARCH_HHDM_TO_PHYS(ustack),
  //              ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
  //                  ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  // com_proc_t   *proc2 = com_sys_proc_new(user_pt, 0);
  // com_thread_t *thread2 =
  //     com_sys_thread_new(proc, ustack, ARCH_PAGE_SIZE, proc2entry);
  //
  // hdr_arch_cpu_get()->ist.rsp0 = (uint64_t)thread->kernel_stack;
  // hdr_arch_cpu_get()->thread   = thread;
  //
  // TAILQ_INSERT_TAIL(&BaseCpu.sched_queue, thread2, threads);
  //
  // arch_mmu_switch(proc->page_table);
  // arch_ctx_trampoline(&thread->ctx);

  com_vfs_t *dummyfs = NULL;
  dummyfs_mount(&dummyfs, NULL);
  com_vnode_t *newfile = NULL;
  DEBUG("creating /myfile.xt");
  com_fs_vfs_create(&newfile, dummyfs->root, "myfile.txt", 10, 0);
  ASSERT(NULL != newfile);
  com_vnode_t *mp = NULL;
  DEBUG("creating /otherfs mountpoint")
  com_fs_vfs_create(&mp, dummyfs->root, "otherfs", 7, 0);
  ASSERT(NULL != mp);
  com_vfs_t *otherfs = NULL;
  DEBUG("mounting otherfs")
  dummyfs_mount(&otherfs, mp);
  ASSERT(NULL != otherfs);
  DEBUG("creating file in /otherfs/file.txt");
  com_vnode_t *otherfile = NULL;
  com_fs_vfs_create(&otherfile, otherfs->root, "file.txt", 8, 0);

  com_vnode_t *samefile = NULL;
  com_fs_vfs_lookup(&samefile, "/myfile.txt", 11, dummyfs->root, dummyfs->root);
  DEBUG("orig=%x, lookup=%x", newfile, samefile);
  ASSERT(newfile == samefile);
  com_fs_vfs_lookup(
      &samefile, "/otherfs/file.txt", 17, dummyfs->root, dummyfs->root);
  DEBUG("orig=%x, lookup=%x", otherfile, samefile);
  ASSERT(otherfile == samefile);
  // com_fs_vfs_lookup(
  //     &samefile, "/otherfs/../myfile.txt", 22, dummyfs->root, dummyfs->root);
  // DEBUG("orig=%x, lookup=%x", newfile, samefile);
  // ASSERT(newfile == samefile);

  DEBUG("writing 'ciao' to /otherfs/myfile.txt");
  com_fs_vfs_write(samefile, "ciao", 4, 0, 0);
  char *buf = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  kmemset(buf, ARCH_PAGE_SIZE, 0);
  com_fs_vfs_read(buf, 5, samefile, 0, 0);
  DEBUG("reading from /otherfs/myfile.txt: %s", buf);

  // intentional page fault
  // *(volatile int *)NULL = 2;
  // asm volatile("int $0x80");
  // *((volatile int *)NULL) = 3;
  while (1)
    ;
}
