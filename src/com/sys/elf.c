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

#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/elf.h>
#include <kernel/platform/mmu.h>
#include <lib/mem.h>
#include <lib/str.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AT_NULL   0
#define AT_IGNORE 1
#define AT_EXECFD 2
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_FLAGS  8
#define AT_ENTRY  9
#define AT_NOTELF 10
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID

struct elf_header {
  uint32_t  magic;
  uint8_t   bits;
  uint8_t   endianness;
  uint8_t   ehdr_ver;
  uint8_t   abi;
  uint64_t  unused;
  uint16_t  type;
  uint16_t  isa;
  uint32_t  elf_ver;
  uintptr_t entry;
  uintptr_t phdrs_off;
  uintptr_t shdrs_off;
  uint32_t  flags;
  uint16_t  ehdr_sz;
  uint16_t  phent_sz;
  uint16_t  phent_num;
  uint16_t  shent_sz;
  uint16_t  shent_num;
  uint16_t  shdr_str_idx;
} __attribute__((packed));

struct elf_phdr {
  uint32_t  type;
  uint32_t  flags;
  uintptr_t off;
  uintptr_t virt_addr;
  uintptr_t phys_addr;
  uint64_t  file_sz;
  uint64_t  mem_sz;
  uint64_t  align;
};

enum elf_phdr_type { PT_LOAD = 1, PT_INTERP = 3, PT_PHDR = 6 };

static int load(uintptr_t             vaddr,
                struct elf_phdr      *phdr,
                COM_FS_VFS_VNODE_t          *elf_file,
                arch_mmu_pagetable_t *pt) {
  size_t   idx = 0;
  uint64_t fsz = phdr->file_sz;

  for (uintptr_t cur = vaddr; cur < (vaddr + phdr->mem_sz);) {
    size_t misalign = cur & (ARCH_PAGE_SIZE - 1);
    size_t rem      = ARCH_PAGE_SIZE - misalign;

    void *phys_page  = com_mm_pmm_alloc();
    void *kvirt_page = (void *)(ARCH_PHYS_TO_HHDM(phys_page) + misalign);
    arch_mmu_map(pt,
                 (void *)(cur - misalign),
                 phys_page,
                 ARCH_MMU_FLAGS_USER | ARCH_MMU_FLAGS_READ |
                     ARCH_MMU_FLAGS_WRITE); // TODO: NO_EXEC for data?

    if (0 < fsz) {
      size_t len = fsz;
      if (rem < len) {
        len = rem;
      }

      int ret =
          com_fs_vfs_read(kvirt_page, len, NULL, elf_file, phdr->off + idx, 0);
      if (0 != ret) {
        return ret;
      }
    }

    cur += rem;
    fsz -= rem;
    idx += rem;
  }

  return 0;
}

int com_sys_elf64_load(com_elf_data_t       *out,
                       const char           *exec_path,
                       size_t                exec_path_len,
                       COM_FS_VFS_VNODE_t          *root,
                       COM_FS_VFS_VNODE_t          *cwd,
                       uintptr_t             virt_off,
                       arch_mmu_pagetable_t *pt) {
  COM_FS_VFS_VNODE_t *elf_file = NULL;
  int ret = com_fs_vfs_lookup(&elf_file, exec_path, exec_path_len, root, cwd);
  if (0 != ret) {
    return ret;
  }

  // Here vnode is already held

  struct elf_header elf_hdr    = {0};
  size_t            bytes_read = 0;
  ret                          = com_fs_vfs_read(
      &elf_hdr, sizeof(struct elf_header), &bytes_read, elf_file, 0, 0);
  if (0 != ret) {
    goto cleanup;
  }

  ASSERT(sizeof(struct elf_header) == bytes_read);

  out->phdr      = 0;
  out->phent_sz  = sizeof(struct elf_phdr);
  out->phent_num = elf_hdr.phent_num;

  for (uint16_t i = 0; i < elf_hdr.phent_num; i++) {
    struct elf_phdr phdr = {0};
    uintmax_t phdr_off   = elf_hdr.phdrs_off + (i * sizeof(struct elf_phdr));

    ret = com_fs_vfs_read(
        &phdr, sizeof(struct elf_phdr), &bytes_read, elf_file, phdr_off, 0);
    if (0 != ret) {
      goto cleanup;
    }

    uintptr_t vaddr = virt_off + phdr.virt_addr;

    switch (phdr.type) {
    case PT_INTERP:
      ret = com_fs_vfs_read(out->interpreter_path,
                            phdr.file_sz,
                            &bytes_read,
                            elf_file,
                            phdr.off,
                            0);
      if (0 != ret) {
        goto cleanup;
      }
      break;

    case PT_PHDR:
      out->phdr = vaddr;
      break;

    case PT_LOAD:
      ret = load(vaddr, &phdr, elf_file, pt);
      if (0 != ret) {
        goto cleanup;
      }
      break;
    }
  }

  out->entry = virt_off + elf_hdr.entry;
cleanup:
  COM_FS_VFS_VNODE_RELEASE(elf_file);
  return ret;
}

// TODO: credit this, taken from ke (probably taken from somwhere else)
uintptr_t com_sys_elf64_prepare_stack(com_elf_data_t elf_data,
                                      size_t         stack_end_phys,
                                      size_t         stack_end_virt,
                                      char *const    argv[],
                                      char *const    envp[]) {
#define PUSH(x) *(--stackptr) = (x)
  uintptr_t *stackptr         = (uintptr_t *)ARCH_PHYS_TO_HHDM(stack_end_phys),
            *orig             = stackptr;
  size_t envc                 = 0;

  for (; envp[envc]; envc++) {
    size_t len = kstrlen(envp[envc]) + 1;
    stackptr   = (uintptr_t *)((uintptr_t)stackptr - len);
    kmemcpy(stackptr, envp[envc], len);
  }

  size_t argc = 0;
  for (; argv[argc]; argc++) {
    size_t len = kstrlen(argv[argc]) + 1;
    stackptr   = (uintptr_t *)((uintptr_t)stackptr - len);
    kmemcpy(stackptr, argv[argc], len);
  }

  stackptr = (uintptr_t *)((uintptr_t)stackptr & (~0xF));
  if (((argc + envc + 1) & 1) != 0) {
    stackptr--;
  }

  PUSH(0);
  PUSH(0);

  PUSH(elf_data.entry);
  PUSH(AT_ENTRY);

  PUSH(elf_data.phdr);
  PUSH(AT_PHDR);

  PUSH(elf_data.phent_sz);
  PUSH(AT_PHENT);

  PUSH(elf_data.phent_num);
  PUSH(AT_PHNUM);

  uintptr_t off = 0;

  PUSH(0);
  stackptr -= envc;
  for (size_t i = 0; i < envc; i++) {
    stackptr[i] = stack_end_virt - (off += kstrlen(envp[i]) + 1);
  }

  PUSH(0);
  stackptr -= argc;
  for (size_t i = 0; i < argc; i++) {
    stackptr[i] = stack_end_virt - (off += kstrlen(argv[i]) + 1);
  }

  PUSH(argc);
  return stack_end_virt - ((uintptr_t)orig - (uintptr_t)stackptr);
#undef PUSH
}
