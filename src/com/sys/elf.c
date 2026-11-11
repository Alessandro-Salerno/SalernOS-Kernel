#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
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

struct ELFHeader {
    u32 magic;
    u8 bits;
    u8 endianness;
    u8 ehdr_ver;
    u8 abi;
    u64 unused;
    u16 type;
    u16 isa;
    u32 elf_ver;
    uptr entry;
    uptr phdrs_off;
    uptr shdrs_off;
    u32 flags;
    u16 ehdr_sz;
    u16 phent_sz;
    u16 phent_num;
    u16 shent_sz;
    u16 shent_num;
    u16 shdr_str_idx;
} packed;

struct ELFPHdr {
    u32 type;
    u32 flags;
    uptr off;
    uptr virt_addr;
    uptr phys_addr;
    u64 file_sz;
    u64 mem_sz;
    u64 align;
};

enum ELFPHdrType {
    PT_LOAD = 1,
    PT_INTERP = 3,
    PT_PHDR = 6
};

static int load(uptr vaddr, struct ELFPHdr *phdr, Vnode *elf_file,
                VMMContext *vmm_context) {
    usize idx = 0;
    isize fsz = phdr->file_sz;
    isize mem_sz = phdr->mem_sz;
    bool vaddr_mapped = false; // if *vaddr is ok
    MMUFlags mmu_flags = MMU_FLAGS_USER | MMU_FLAGS_READ | MMU_FLAGS_WRITE;

    if (vaddr % PAGE_SIZE != 0 && fsz > 0) {
        isize misalign = vaddr % PAGE_SIZE;
        isize rem = PAGE_SIZE - misalign;
        isize mapping_len = min(rem, fsz);
        void *phys_page = pmm_alloc();
        vmm_map(vmm_context, (void *)(vaddr - misalign), phys_page, mapping_len,
                VMM_FLAGS_NONE, mmu_flags);
        vfs_read((void *)(PHYS_TO_HHDM(phys_page) + misalign), mapping_len,
                 elf_file, phdr->off, O_RDONLY, NULL);
        vaddr += mapping_len;
        fsz -= mapping_len;
        mem_sz -= mapping_len;
        idx += mapping_len;
        vaddr_mapped = true;
    }

    if (fsz <= 0) goto zero_fill;

    // here vaddr is guaranteed to be page-aligned
    vfs_mmap(elf_file, vmm_context, (void *)vaddr, fsz, VMM_FLAGS_PRIVATE,
             MMU_FLAGS_USER | MMU_FLAGS_READ | MMU_FLAGS_WRITE, phdr->off + idx,
             NULL);
    vaddr += fsz;
    mem_sz -= fsz;
    vaddr_mapped = true;

zero_fill:
    if (vaddr % PAGE_SIZE != 0 && mem_sz > 0) {
        isize misalign = vaddr % PAGE_SIZE;
        isize rem = PAGE_SIZE - misalign;
        if (vaddr_mapped) {
            isize zero_size = min(mem_sz, rem);
            void *phys_page = vmm_get_physical(vmm_context, (void *)vaddr);
            if (phys_page == NULL) return EFAULT;
            // NOTE: the value returned by vmm_get_physical already accounts for
            // in-page offset (misalign)
            memset((void *)PHYS_TO_HHDM(phys_page), 0, zero_size);
            vaddr += zero_size;
            mem_sz -= zero_size;
        } else {
            vaddr -= misalign;
            mem_sz += misalign;
            // will be mapped below
        }
    }

    if (mem_sz <= 0) return 0;

    vmm_map(vmm_context, (void *)vaddr, NULL, mem_sz, VMM_FLAGS_ALLOCATE,
            mmu_flags);

    return 0;
}

int elf64_load(const char *exec_path, usize exec_path_len, Vnode *root,
               Vnode *cwd, uptr virt_off, VMMContext *vmm_context,
               ELFData *out_elf_data) {
    ProfilerData profiler_data = profiler_start_function(PROFILE_FUNC_ELF_LOAD);
    Vnode *elf_file = NULL;
    int ret = vfs_lookup(exec_path, exec_path_len, root, cwd, VFS_LOOKUP_FOLLOW,
                         &elf_file);
    if (ret != 0) return ret;

    // POSIX.1-2017, exec: EACCES if the file is not a regular file
    if (elf_file->type != VNODE_TYPE_FILE) {
        ret = EACCES;
        goto cleanup;
    }

    char elf_signature[4];
    char expected_signaturr[4] = {0x7F, 'E', 'L', 'F'};
    usize signature_bytes = 0;
    ret = vfs_read(elf_signature, sizeof(elf_signature), elf_file, 0, 0,
                   &signature_bytes);
    if (ret != 0) goto cleanup;
    if (signature_bytes != sizeof(elf_signature) ||
        memcmp(elf_signature, expected_signaturr, sizeof(elf_signature)) != 0) {
        ret = EACCES;
        goto cleanup;
    }

    struct ELFHeader elf_hdr = {0};
    usize bytes_read = 0;
    ret = vfs_read(&elf_hdr, sizeof(struct ELFHeader), elf_file, 0, O_RDONLY,
                   &bytes_read);
    if (ret != 0) goto cleanup;

    if (bytes_read != sizeof(struct ELFHeader)) {
        ret = ENOEXEC;
        goto cleanup;
    }

    out_elf_data->phdr = 0;
    out_elf_data->phent_sz = sizeof(struct ELFPHdr);
    out_elf_data->phent_num = elf_hdr.phent_num;

    for (u16 i = 0; i < elf_hdr.phent_num; i++) {
        struct ELFPHdr phdr = {0};
        u64 phdr_off = elf_hdr.phdrs_off + (i * sizeof(struct ELFPHdr));

        ret = vfs_read(&phdr, sizeof(struct ELFPHdr), elf_file, phdr_off,
                       O_RDONLY, &bytes_read);
        if (ret != 0) goto cleanup;

        uptr vaddr = virt_off + phdr.virt_addr;

        switch (phdr.type) {
            case PT_INTERP:
                log_debug("found PT_INTERP at offset %zu", phdr_off);
                if (phdr.file_sz < 2 ||
                    phdr.file_sz > sizeof(out_elf_data->interpreter_path)) {
                    ret = ENOEXEC;
                    goto cleanup;
                }
                ret = vfs_read(out_elf_data->interpreter_path, phdr.file_sz,
                               elf_file, phdr.off, 0, &bytes_read);
                if (ret != 0) goto cleanup;
                out_elf_data->interpreter_len = phdr.file_sz - 1;
                if (bytes_read != phdr.file_sz ||
                    out_elf_data
                            ->interpreter_path[out_elf_data->interpreter_len] !=
                        '\0' ||
                    memchr(out_elf_data->interpreter_path, '\0',
                           out_elf_data->interpreter_len) != NULL) {
                    ret = ENOEXEC;
                    goto cleanup;
                }
                break;

            case PT_PHDR:
                out_elf_data->phdr = vaddr;
                break;

            case PT_LOAD:
                if (!MMU_RANGE_IS_USER(vaddr, phdr.mem_sz) ||
                    !MMU_RANGE_IS_USER(vaddr, phdr.file_sz)) {
                    ret = ENOEXEC;
                    goto cleanup;
                }
                log_debug(
                    "found PT_LOAD at offset %zu with segment at offset %zu "
                    "and "
                    "virt %p",
                    phdr_off, phdr.off, phdr.virt_addr);
                ret = load(vaddr, &phdr, elf_file, vmm_context);
                if (ret != 0) goto cleanup;
                // Static binaries usually have no PT_PHDR
                if (out_elf_data->phdr == 0 && phdr.off <= elf_hdr.phdrs_off &&
                    elf_hdr.phdrs_off < phdr.off + phdr.file_sz) {
                    out_elf_data->phdr = vaddr + elf_hdr.phdrs_off - phdr.off;
                }
                break;
        }
    }

    out_elf_data->entry = virt_off + elf_hdr.entry;
    if (!MMU_ADDR_IS_USER(out_elf_data->entry)) ret = ENOEXEC;

cleanup:
    vfs_vnode_rel(elf_file);
    profiler_end_function(&profiler_data);
    return ret;
}

// If is_str is set, buf is a string that must not contain NULs
static int push_buffer(VMMContext *vmm_ctx, uptr stack_virt_end,
                       usize stack_len, usize *stack_off, void **stack_page,
                       const void *buf, usize len, bool is_str) {
    if (*stack_off > stack_len || len > stack_len - *stack_off) return E2BIG;

    const unsigned char *str = buf;
    const unsigned char *end = str + len;

    while (len > 0) {
        // stack_off grows toward higher values, but the stack itself grows
        // downwards and we access pages in ascending address order. So we get
        // the offset into the current page by modulo, and subtracting it from
        // the page size gives us the stack pointer/cursor into the page, which
        // also happens to be the remaining bytes in the page (by definition,
        // it's the distance from the page's base address)
        usize stack_aligned_off = *stack_off % PAGE_SIZE;
        usize stack_page_cur = PAGE_SIZE - stack_aligned_off;
        usize page_rem = stack_page_cur;
        usize can_write = min(len, page_rem);

        *stack_off += can_write;
        usize write_at = stack_page_cur - can_write;
        unsigned char *dst = (unsigned char *)*stack_page + write_at;
        int ret = memcpy_maybe_user(dst, end - can_write, can_write);
        if (ret != 0) return ret;
        if (is_str && memchr(dst, '\0', can_write) != NULL) return EINVAL;

        end -= can_write;
        len -= can_write;

        if (can_write == page_rem && *stack_off < stack_len) {
            void *res = vmm_get_physical(
                vmm_ctx, (void *)(stack_virt_end - *stack_off - 1));
            if (res == NULL) return EFAULT;
            *stack_page = (void *)PHYS_TO_HHDM((uptr)res &
                                               ~(uptr)(PAGE_SIZE - 1));
        }
    }

    return 0;
}

// Each entry is read once. A user array must only point to user strings
static int push_string(VMMContext *vmm_ctx, uptr stack_virt_end,
                       usize stack_len, usize *stack_off, void **stack_page,
                       const struct syscall_str *entry, bool user) {
    struct syscall_str str;
    int ret = memcpy_maybe_user(&str, entry, sizeof(str));
    if (ret != 0) return ret;
    if (user && !MMU_RANGE_IS_USER(str.str, str.len)) return EFAULT;

    ret = push_buffer(vmm_ctx, stack_virt_end, stack_len, stack_off, stack_page,
                      "", 1, false);
    if (ret != 0) return ret;

    return push_buffer(vmm_ctx, stack_virt_end, stack_len, stack_off,
                       stack_page, str.str, str.len, true);
}

static int push_value(VMMContext *vmm_ctx, uptr stack_virt_end, usize stack_len,
                      usize *stack_off, void **stack_page, uptr value) {
    return push_buffer(vmm_ctx, stack_virt_end, stack_len, stack_off,
                       stack_page, &value, sizeof(uptr), false);
}

// Pushes a pointer to each string in [lo, hi) of the new stack, lowest first.
// The strings have no NULs of their own, so each NUL ends one of them
static int push_string_ptrs(VMMContext *vmm_ctx, uptr stack_virt_end,
                            usize stack_len, usize *stack_off,
                            void **stack_page, uptr lo, uptr hi) {
    uptr start = lo;

    for (uptr va = lo; va < hi;) {
        usize pgoff = va % PAGE_SIZE;
        usize n = min(hi - va, PAGE_SIZE - pgoff);
        void *phys = vmm_get_physical(vmm_ctx, (void *)va);
        if (phys == NULL) return EFAULT;

        const char *page = (void *)PHYS_TO_HHDM((uptr)phys &
                                                ~(uptr)(PAGE_SIZE - 1));
        const char *cur = page + pgoff;
        const char *end = cur + n;
        const char *nul;
        while ((nul = memchr(cur, '\0', end - cur)) != NULL) {
            int ret = push_value(vmm_ctx, stack_virt_end, stack_len, stack_off,
                                 stack_page, start);
            if (ret != 0) return ret;
            start = va + (nul - (page + pgoff)) + 1;
            cur = nul + 1;
        }

        va += n;
    }

    return 0;
}

static int align_stack(VMMContext *vmm_ctx, uptr stack_virt_end,
                       usize stack_len, usize *stack_off, void **stack_page) {
    unsigned char dummy[15] = {0};
    uptr stack_ptr = stack_virt_end - *stack_off;
    usize to_align = stack_ptr & 0xF;
    return push_buffer(vmm_ctx, stack_virt_end, stack_len, stack_off,
                       stack_page, dummy, to_align, false);
}

int elf64_prepare_stack(VMMContext *vmm_ctx, ELFData elf_data,
                        usize stack_end_virt, usize stack_len,
                        const struct syscall_str *argv, usize argc,
                        const struct syscall_str *envp, usize envc,
                        uptr *out_stack_ptr) {
#define PUSH(x)                                                             \
    {                                                                       \
        int _r = push_value(vmm_ctx, stack_end_virt, stack_len, &stack_off, \
                            &stack_page, x);                                \
        if (_r != 0) {                                                      \
            return _r;                                                      \
        }                                                                   \
    }

    assert(stack_end_virt % PAGE_SIZE == 0);
    assert(stack_len <= stack_end_virt);
    if (stack_len == 0) return E2BIG;
    void *stack_phys = vmm_get_physical(vmm_ctx, (void *)(stack_end_virt - 1));
    if (stack_phys == NULL) return EFAULT;
    void *stack_page = (void *)PHYS_TO_HHDM((uptr)stack_phys &
                                            ~(uptr)(PAGE_SIZE - 1));
    usize stack_off = 0;

    for (usize i = 0; i < envc; i++) {
        int ret = push_string(vmm_ctx, stack_end_virt, stack_len, &stack_off,
                              &stack_page, &envp[i], MMU_ADDR_IS_USER(envp));
        if (ret != 0) return ret;
    }

    usize env_bytes = stack_off;

    for (usize i = 0; i < argc; i++) {
        int ret = push_string(vmm_ctx, stack_end_virt, stack_len, &stack_off,
                              &stack_page, &argv[i], MMU_ADDR_IS_USER(argv));
        if (ret != 0) return ret;
    }

    usize strings_bytes = stack_off;

    int align_ret = align_stack(vmm_ctx, stack_end_virt, stack_len, &stack_off,
                                &stack_page);
    if (align_ret != 0) return align_ret;
    if (((envc + argc + 1) & 1) != 0) {
        PUSH(0);
    }

    PUSH(0);
    PUSH(0);

    if (elf_data.entry == 0 || elf_data.phent_sz == 0 ||
        elf_data.phent_num == 0) {
        return ENOEXEC;
    }

    PUSH(elf_data.entry);
    PUSH(AT_ENTRY);

    PUSH(elf_data.phdr);
    PUSH(AT_PHDR);

    PUSH(elf_data.phent_sz);
    PUSH(AT_PHENT);

    PUSH(elf_data.phent_num);
    PUSH(AT_PHNUM);

    PUSH(0);
    int ptrs_ret = push_string_ptrs(vmm_ctx, stack_end_virt, stack_len,
                                    &stack_off, &stack_page,
                                    stack_end_virt - env_bytes, stack_end_virt);
    if (ptrs_ret != 0) return ptrs_ret;

    PUSH(0);
    ptrs_ret = push_string_ptrs(vmm_ctx, stack_end_virt, stack_len, &stack_off,
                                &stack_page, stack_end_virt - strings_bytes,
                                stack_end_virt - env_bytes);
    if (ptrs_ret != 0) return ptrs_ret;

    PUSH(argc);

    *out_stack_ptr = stack_end_virt - stack_off;
    return 0;
#undef PUSH
}

int elf64_prepare_proc(const char *path, usize pathlen,
                       const struct syscall_str *argv, usize argc,
                       const struct syscall_str *envp, usize envc, Proc *proc,
                       Context *ctx, VMMContext **out_vmm_context) {
    assert(pathlen < sizeof(proc->executable_path));
    VMMContext *new_vmm_ctx = vmm_new_context(NULL);
    *out_vmm_context = new_vmm_ctx;
    Vnode *cwd = proc_get_cwd(proc);

    ELFData prog_data = {0};
    int status = elf64_load(path, pathlen, proc->root, cwd, 0, new_vmm_ctx,
                            &prog_data);

    if (status != 0) goto fail;

    uptr entry = prog_data.entry;

    if (prog_data.interpreter_len != 0) {
        ELFData interp_data = {0};
        status = elf64_load(prog_data.interpreter_path,
                            prog_data.interpreter_len, proc->root, cwd,
                            0x40000000, new_vmm_ctx, &interp_data);

        if (status != 0) goto fail;

        entry = interp_data.entry;
    }

    uptr stack_end = 0x60000000;
    usize stack_len = PAGE_SIZE * 2048;
    uptr stack_start = stack_end - stack_len;

    vmm_map(
        new_vmm_ctx, (void *)stack_start, NULL, stack_len, VMM_FLAGS_ALLOCATE,
        MMU_FLAGS_READ | MMU_FLAGS_WRITE | MMU_FLAGS_NOEXEC | MMU_FLAGS_USER);

    // ctx belongs to the caller, so it is only touched on success
    uptr stack_ptr = 0;
    status = elf64_prepare_stack(new_vmm_ctx, prog_data, stack_end, stack_len,
                                 argv, argc, envp, envc, &stack_ptr);
    if (status != 0) goto fail;
    *ctx = (Context){0};
    CONTEXT_THREAD_SET((*ctx), stack_ptr, 0, entry);
    memcpy(proc->executable_path, path, pathlen);
    proc->executable_path[pathlen] = '\0';
    vfs_vnode_rel(cwd);
    return 0;

fail:
    vfs_vnode_rel(cwd);
    vmm_destroy_context(new_vmm_ctx);
    return status;
}
