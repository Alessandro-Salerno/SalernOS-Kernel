#pragma once

#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <kernel/platform/info.h>

#include <salernos/syscall_str.h>

typedef struct {
    uptr entry;
    usize phdr;
    usize phent_sz;
    usize phent_num;
    char interpreter_path[64];
    usize interpreter_len;
} ELFData;

int elf64_load(const char *exec_path, usize exec_path_len, Vnode *root,
               Vnode *cwd, uptr virt_off, VMMContext *vmm_context,
               ELFData *out_elf_data);

// argv and envp are either kernel arrays of kernel strings or user arrays of
// user strings
int elf64_prepare_stack(VMMContext *vmm_ctx, ELFData elf_data,
                        usize stack_end_virt, usize stack_len,
                        const struct syscall_str *argv, usize argc,
                        const struct syscall_str *envp, usize envc,
                        uptr *out_stack_ptr);
int elf64_prepare_proc(const char *path, usize pathlen,
                       const struct syscall_str *argv, usize argc,
                       const struct syscall_str *envp, usize envc, Proc *proc,
                       Context *ctx, VMMContext **out_vmm_context);
