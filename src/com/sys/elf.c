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
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
                com_vnode_t          *elf_file,
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
                       com_vnode_t          *root,
                       com_vnode_t          *cwd,
                       uintptr_t             virt_off,
                       arch_mmu_pagetable_t *pt) {
  com_vnode_t *elf_file = NULL;
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
  COM_VNODE_RELEASE(elf_file);
  return ret;
}