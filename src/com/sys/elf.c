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
#include <fcntl.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
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
    size_t           idx          = 0;
    ssize_t          fsz          = phdr->file_sz;
    ssize_t          mem_sz       = phdr->mem_sz;
    bool             vaddr_mapped = false; // if *vaddr is ok
    arch_mmu_flags_t mmu_flags    = ARCH_MMU_FLAGS_USER | ARCH_MMU_FLAGS_READ |
                                    ARCH_MMU_FLAGS_WRITE;

    if (0 != vaddr % ARCH_PAGE_SIZE && fsz > 0) {
        ssize_t misalign    = vaddr % ARCH_PAGE_SIZE;
        ssize_t rem         = ARCH_PAGE_SIZE - misalign;
        ssize_t mapping_len = KMIN(rem, fsz);
        void   *phys_page   = com_mm_pmm_alloc();
        com_mm_vmm_map(vmm_context,
                       (void *)(vaddr - misalign),
                       phys_page,
                       mapping_len,
                       COM_MM_VMM_FLAGS_NONE,
                       mmu_flags);
        com_fs_vfs_read((void *)(ARCH_PHYS_TO_HHDM(phys_page) + misalign),
                        mapping_len,
                        NULL,
                        elf_file,
                        phdr->off,
                        O_RDONLY);
        vaddr += mapping_len;
        fsz -= mapping_len;
        mem_sz -= mapping_len;
        idx += mapping_len;
        vaddr_mapped = true;
    }

    if (fsz <= 0) {
        goto zero_fill;
    }

    // here vaddr is guaranteed to be page-aligned
    com_fs_vfs_mmap(NULL,
                    elf_file,
                    vmm_context,
                    (void *)vaddr,
                    fsz,
                    COM_MM_VMM_FLAGS_PRIVATE,
                    ARCH_MMU_FLAGS_USER | ARCH_MMU_FLAGS_READ |
                        ARCH_MMU_FLAGS_WRITE,
                    phdr->off + idx);
    vaddr += fsz;
    mem_sz -= fsz;
    vaddr_mapped = true;

zero_fill:
    if (0 != vaddr % ARCH_PAGE_SIZE && mem_sz > 0) {
        ssize_t misalign = vaddr % ARCH_PAGE_SIZE;
        ssize_t rem      = ARCH_PAGE_SIZE - misalign;
        if (vaddr_mapped) {
            ssize_t zero_size = KMIN(mem_sz, rem);
            void   *phys_page = com_mm_vmm_get_physical(vmm_context,
                                                        (void *)vaddr);
            if (NULL == phys_page) {
                return EFAULT;
            }
            // NOTE: the value returned by vmm_get_physical already accounts for
            // in-page offset (misalign)
            kmemset((void *)ARCH_PHYS_TO_HHDM(phys_page), zero_size, 0);
            vaddr += zero_size;
            mem_sz -= zero_size;
        } else {
            vaddr -= misalign;
            mem_sz += misalign;
            // will be mapped below
        }
    }

    if (mem_sz <= 0) {
        return 0;
    }

    com_mm_vmm_map(vmm_context,
                   (void *)vaddr,
                   NULL,
                   mem_sz,
                   COM_MM_VMM_FLAGS_ALLOCATE,
                   mmu_flags);

    return 0;
}

int com_sys_elf64_load(com_elf_data_t    *out,
                       const char        *exec_path,
                       size_t             exec_path_len,
                       com_vnode_t       *root,
                       com_vnode_t       *cwd,
                       uintptr_t          virt_off,
                       com_vmm_context_t *vmm_context) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_ELF_LOAD);
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

    struct elf_header elf_hdr    = {0};
    size_t            bytes_read = 0;
    ret                          = com_fs_vfs_read(&elf_hdr,
                                                   sizeof(struct elf_header),
                                                   &bytes_read,
                                                   elf_file,
                                                   0,
                                                   O_RDONLY);
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
                              O_RDONLY);
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
    com_sys_profiler_end_function(&profiler_data);
    return ret;
}

static int push_buffer(com_vmm_context_t *vmm_ctx,
                       uintptr_t          stack_virt_end,
                       size_t             stack_len,
                       size_t            *stack_off,
                       void             **stack_page,
                       const void        *buf,
                       size_t             len) {
    if (*stack_off > stack_len || len > stack_len - *stack_off) {
        return E2BIG;
    }

    const unsigned char *str = buf;
    const unsigned char *end = str + len;

    while (len > 0) {
        // stack_off grows toward higher values, but the stack itself grows
        // downwards and we access pages in ascending address order. So we get
        // the offset into the current page by modulo, and subtracting it from
        // the page size gives us the stack pointer/cursor into the page, which
        // also happens to be the remaining bytes in the page (by definition,
        // it's the distance from the page's base address)
        size_t stack_aligned_off = *stack_off % ARCH_PAGE_SIZE;
        size_t stack_page_cur    = ARCH_PAGE_SIZE - stack_aligned_off;
        size_t page_rem          = stack_page_cur;
        size_t can_write         = KMIN(len, page_rem);

        *stack_off += can_write;
        size_t write_at = stack_page_cur - can_write;
        kmemcpy((unsigned char *)*stack_page + write_at,
                end - can_write,
                can_write);

        end -= can_write;
        len -= can_write;

        if (can_write == page_rem && *stack_off < stack_len) {
            void *res = com_mm_vmm_get_physical(
                vmm_ctx,
                (void *)(stack_virt_end - *stack_off - 1));
            if (NULL == res) {
                return EFAULT;
            }
            *stack_page = (void *)ARCH_PHYS_TO_HHDM(
                (uintptr_t)res & ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
        }
    }

    return 0;
}

static int push_string(com_vmm_context_t *vmm_ctx,
                       uintptr_t          stack_virt_end,
                       size_t             stack_len,
                       size_t            *stack_off,
                       void             **stack_page,
                       const char        *str) {
    return push_buffer(vmm_ctx,
                       stack_virt_end,
                       stack_len,
                       stack_off,
                       stack_page,
                       str,
                       kstrlen(str) + 1);
}

static int push_value(com_vmm_context_t *vmm_ctx,
                      uintptr_t          stack_virt_end,
                      size_t             stack_len,
                      size_t            *stack_off,
                      void             **stack_page,
                      uintptr_t          value) {
    return push_buffer(vmm_ctx,
                       stack_virt_end,
                       stack_len,
                       stack_off,
                       stack_page,
                       &value,
                       sizeof(uintptr_t));
}

static int align_stack(com_vmm_context_t *vmm_ctx,
                       uintptr_t          stack_virt_end,
                       size_t             stack_len,
                       size_t            *stack_off,
                       void             **stack_page) {
    unsigned char dummy[15] = {0};
    uintptr_t     stack_ptr = stack_virt_end - *stack_off;
    size_t        to_align  = stack_ptr & 0xF;
    return push_buffer(vmm_ctx,
                       stack_virt_end,
                       stack_len,
                       stack_off,
                       stack_page,
                       dummy,
                       to_align);
}

int com_sys_elf64_prepare_stack(uintptr_t         *out_stack_ptr,
                                com_vmm_context_t *vmm_ctx,
                                com_elf_data_t     elf_data,
                                size_t             stack_end_virt,
                                size_t             stack_len,
                                char *const        argv[],
                                char *const        envp[]) {
#define PUSH(x)                             \
    {                                       \
        int _r = push_value(vmm_ctx,        \
                            stack_end_virt, \
                            stack_len,      \
                            &stack_off,     \
                            &stack_page,    \
                            x);             \
        if (0 != _r) {                      \
            return _r;                      \
        }                                   \
    }

    KASSERT(0 == stack_end_virt % ARCH_PAGE_SIZE);
    KASSERT(stack_len <= stack_end_virt);
    if (0 == stack_len) {
        return E2BIG;
    }
    void *stack_phys = com_mm_vmm_get_physical(vmm_ctx,
                                               (void *)(stack_end_virt - 1));
    if (NULL == stack_phys) {
        return EFAULT;
    }
    void *stack_page = (void *)ARCH_PHYS_TO_HHDM(
        (uintptr_t)stack_phys & ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
    size_t stack_off = 0;

    size_t envc = 0;
    for (; envp[envc]; envc++) {
        int ret = push_string(vmm_ctx,
                              stack_end_virt,
                              stack_len,
                              &stack_off,
                              &stack_page,
                              envp[envc]);
        if (0 != ret) {
            return ret;
        }
    }

    size_t env_bytes = stack_off;

    size_t argc = 0;
    for (; argv[argc]; argc++) {
        int ret = push_string(vmm_ctx,
                              stack_end_virt,
                              stack_len,
                              &stack_off,
                              &stack_page,
                              argv[argc]);
        if (0 != ret) {
            return ret;
        }
    }

    size_t strings_bytes = stack_off;

    int align_ret = align_stack(vmm_ctx,
                                stack_end_virt,
                                stack_len,
                                &stack_off,
                                &stack_page);
    if (0 != align_ret) {
        return align_ret;
    }
    if (0 != ((envc + argc + 1) & 1)) {
        PUSH(0);
    }

    PUSH(0);
    PUSH(0);

    KASSERT(0 != elf_data.entry);
    KASSERT(0 != elf_data.phdr);
    KASSERT(0 != elf_data.phent_sz);
    KASSERT(0 != elf_data.phent_num);

    PUSH(elf_data.entry);
    PUSH(AT_ENTRY);

    PUSH(elf_data.phdr);
    PUSH(AT_PHDR);

    PUSH(elf_data.phent_sz);
    PUSH(AT_PHENT);

    PUSH(elf_data.phent_num);
    PUSH(AT_PHNUM);

    uintptr_t off = env_bytes;

    PUSH(0);
    for (size_t i = envc; i > 0;) {
        i--;
        PUSH(stack_end_virt - off);
        off -= kstrlen(envp[i]) + 1;
    }

    off = strings_bytes;

    PUSH(0);
    for (size_t i = argc; i > 0;) {
        i--;
        PUSH(stack_end_virt - off);
        off -= kstrlen(argv[i]) + 1;
    }

    PUSH(argc);

    *out_stack_ptr = stack_end_virt - stack_off;
    return 0;
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
        status = com_sys_elf64_load(&interp_data,
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
    size_t    stack_len   = ARCH_PAGE_SIZE * 2048;
    uintptr_t stack_start = stack_end - stack_len;

    com_mm_vmm_map(new_vmm_ctx,
                   (void *)stack_start,
                   NULL,
                   stack_len,
                   COM_MM_VMM_FLAGS_ALLOCATE,
                   ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                       ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);

    *ctx                = (arch_context_t){0};
    uintptr_t stack_ptr = 0;
    status              = com_sys_elf64_prepare_stack(&stack_ptr,
                                                      new_vmm_ctx,
                                                      prog_data,
                                                      stack_end,
                                                      stack_len,
                                                      argv,
                                                      env);
    if (0 != status) {
        goto fail;
    }
    ARCH_CONTEXT_THREAD_SET((*ctx), stack_ptr, 0, entry);
    kstrncpy(proc->executable_path, path, CONFIG_PATH_MAX);
    return 0;

fail:
    com_mm_vmm_destroy_context(new_vmm_ctx);
    return status;
}
