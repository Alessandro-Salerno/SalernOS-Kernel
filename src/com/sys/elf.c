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

#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
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

enum elf_phdr_type {
    PT_LOAD   = 1,
    PT_INTERP = 3,
    PT_PHDR   = 6
};

static int load(uintptr_t          vaddr,
                struct elf_phdr   *phdr,
                com_vnode_t       *elf_file,
                com_vmm_context_t *vmm_context) {
    size_t   idx = 0;
    intmax_t fsz = phdr->file_sz;

    for (uintptr_t cur = vaddr; cur < (vaddr + phdr->mem_sz);) {
        intmax_t misalign = cur & (ARCH_PAGE_SIZE - 1);
        intmax_t rem      = ARCH_PAGE_SIZE - misalign;

        void *phys_page  = com_mm_pmm_alloc_zero();
        void *kvirt_page = (void *)(ARCH_PHYS_TO_HHDM(phys_page) + misalign);

        com_mm_vmm_map(vmm_context,
                       (void *)(cur - misalign),
                       phys_page,
                       ARCH_PAGE_SIZE,
                       COM_MM_VMM_FLAGS_NONE,
                       ARCH_MMU_FLAGS_USER | ARCH_MMU_FLAGS_READ |
                           ARCH_MMU_FLAGS_WRITE);

        if (fsz > 0) {
            size_t len = KMIN(rem, fsz);

            int ret = com_fs_vfs_read(kvirt_page,
                                      len,
                                      NULL,
                                      elf_file,
                                      phdr->off + idx,
                                      0);
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

int com_sys_elf64_load(com_elf_data_t    *out,
                       const char        *exec_path,
                       size_t             exec_path_len,
                       com_vnode_t       *root,
                       com_vnode_t       *cwd,
                       uintptr_t          virt_off,
                       com_vmm_context_t *vmm_context) {
    com_vnode_t *elf_file = NULL;
    int          ret      = com_fs_vfs_lookup(&elf_file,
                                exec_path,
                                exec_path_len,
                                root,
                                cwd,
                                true);
    if (0 != ret) {
        return ret;
    }

    if (E_COM_VNODE_TYPE_FILE != elf_file->type) {
        goto cleanup;
    }

    char   elf_signature[4];
    char   expected_signaturr[4] = {0x7F, 'E', 'L', 'F'};
    size_t signature_bytes       = 0;
    ret                          = com_fs_vfs_read(elf_signature,
                          sizeof(elf_signature),
                          &signature_bytes,
                          elf_file,
                          0,
                          0);
    if (0 != ret) {
        goto cleanup;
    }
    if (sizeof(elf_signature) != signature_bytes ||
        0 !=
            kmemcmp(elf_signature, expected_signaturr, sizeof(elf_signature))) {
        ret = EACCES;
        goto cleanup;
    }

    // Here vnode is already held

    struct elf_header elf_hdr    = {0};
    size_t            bytes_read = 0;
    ret                          = com_fs_vfs_read(&elf_hdr,
                          sizeof(struct elf_header),
                          &bytes_read,
                          elf_file,
                          0,
                          0);
    if (0 != ret) {
        goto cleanup;
    }

    KASSERT(sizeof(struct elf_header) == bytes_read);

    out->phdr      = 0;
    out->phent_sz  = sizeof(struct elf_phdr);
    out->phent_num = elf_hdr.phent_num;

    for (uint16_t i = 0; i < elf_hdr.phent_num; i++) {
        struct elf_phdr phdr = {0};
        uintmax_t phdr_off = elf_hdr.phdrs_off + (i * sizeof(struct elf_phdr));

        ret = com_fs_vfs_read(&phdr,
                              sizeof(struct elf_phdr),
                              &bytes_read,
                              elf_file,
                              phdr_off,
                              0);
        if (0 != ret) {
            goto cleanup;
        }

        uintptr_t vaddr = virt_off + phdr.virt_addr;

        switch (phdr.type) {
            case PT_INTERP:
                KDEBUG("found PT_INTERP at offset %zu", phdr_off);
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
                KDEBUG("found PT_LOAD at offset %zu with segment at offset %zu "
                       "and "
                       "virt %p",
                       phdr_off,
                       phdr.off,
                       phdr.virt_addr);
                ret = load(vaddr, &phdr, elf_file, vmm_context);
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

// CREDIT: vloxei64/ke
// TODO: this is VERY unsafe!! It uses the HHDM to write to the new process's
// stack, but this causes wild memory corruption if auxv contents exceed
// ARCH_PAGE_SIZE bytes
uintptr_t com_sys_elf64_prepare_stack(com_elf_data_t elf_data,
                                      size_t         stack_end_phys,
                                      size_t         stack_end_virt,
                                      char *const    argv[],
                                      char *const    envp[]) {
#define PUSH(x) *(--stackptr) = (x)
    uintptr_t *stackptr       = (uintptr_t *)ARCH_PHYS_TO_HHDM(stack_end_phys),
              *orig           = stackptr;
    size_t envc               = 0;

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

int com_sys_elf64_prepare_proc(com_vmm_context_t **out_vmm_context,
                               const char         *path,
                               char *const         argv[],
                               char *const         env[],
                               com_proc_t         *proc,
                               arch_context_t     *ctx) {
    com_vmm_context_t *new_vmm_ctx = com_mm_vmm_new_context(NULL);
    *out_vmm_context               = new_vmm_ctx;

    com_elf_data_t prog_data = {0};
    int            status    = com_sys_elf64_load(&prog_data,
                                    path,
                                    kstrlen(path),
                                    proc->root,
                                    proc->cwd,
                                    0,
                                    new_vmm_ctx);

    if (0 != status) {
        goto fail;
    }

    uintptr_t entry = prog_data.entry;

    if (0 != prog_data.interpreter_path[0]) {
        com_elf_data_t interp_data = {0};
        status                     = com_sys_elf64_load(&interp_data,
                                    prog_data.interpreter_path,
                                    kstrlen(prog_data.interpreter_path),
                                    proc->root,
                                    proc->cwd,
                                    0x40000000,
                                    new_vmm_ctx);

        if (0 != status) {
            goto fail;
        }

        entry = interp_data.entry;
    }

    uintptr_t stack_end   = 0x60000000;
    uintptr_t stack_start = stack_end - (ARCH_PAGE_SIZE * 64);
    void     *stack_phys  = NULL;

    for (uintptr_t curr = stack_start; curr < stack_end;
         curr += ARCH_PAGE_SIZE) {
        stack_phys = com_mm_pmm_alloc();
        com_mm_vmm_map(new_vmm_ctx,
                       (void *)curr,
                       stack_phys,
                       ARCH_PAGE_SIZE,
                       COM_MM_VMM_FLAGS_NONE,
                       ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                           ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
    }

    *ctx                = (arch_context_t){0};
    uintptr_t stack_ptr = com_sys_elf64_prepare_stack(prog_data,
                                                      (uintptr_t)stack_phys +
                                                          ARCH_PAGE_SIZE,
                                                      stack_end,
                                                      argv,
                                                      env);
    ARCH_CONTEXT_THREAD_SET((*ctx), stack_ptr, 0, entry);
    return 0;

fail:
    com_mm_vmm_destroy_context(new_vmm_ctx);
    return status;
}
