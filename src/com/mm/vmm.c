#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/sync.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <stdatomic.h>

#define UNMAP_MAX_ARRAY_ON_STACK 64

static VMMContext g_root_context = {0};
static void *g_zero_page = NULL;

static inline VMMContext *vmm_current_context(void) {
    Thread *curr_thread = cpu_get_thread();
    if (curr_thread == NULL || curr_thread->proc == NULL ||
        curr_thread->proc->vmm_context == NULL) {
        return &g_root_context;
    }
    return curr_thread->proc->vmm_context;
}

static inline VMMContext *vmm_ensure_context(VMMContext *context) {
    assert(&g_root_context != context);

    if (context == NULL) return vmm_current_context();

    return context;
}

VMMContext *vmm_new_context(MMUPageTable *pagetable) {
    if (pagetable == NULL) pagetable = mmu_new_table();

    VMMContext *context = slab_alloc(sizeof(VMMContext));
    context->pagetable = pagetable;
    context->lock = SPINLOCK_NEW();
    return context;
}

void vmm_destroy_context(VMMContext *context) {
    assert(context != NULL);
    assert(&g_root_context != context);
    assert(g_root_context.pagetable != context->pagetable);
    assert(context != vmm_current_context());
    assert(context->pagetable != mmu_get_table());
    assert(context->pagetable != NULL);

    // TODO: reimplement this
}

VMMContext *vmm_duplicate_context(VMMContext *context) {
    if (context == NULL) return vmm_new_context(NULL);

    MMUPageTable *new_pt = mmu_duplicate_table(context->pagetable);
    VMMContext *new_vmm_ctx = vmm_new_context(new_pt);
    new_vmm_ctx->anon_pages = context->anon_pages;
    return new_vmm_ctx;
}

static void *vmm_alloc_anon_range(VMMContext *context, usize pages) {
    assert(&g_root_context != context);
    assert(g_root_context.pagetable != context->pagetable);

    usize max_pages = ((uptr)MMU_USERSPACE_END - CONFIG_VMM_ANON_START) /
        PAGE_SIZE;
    void *base = NULL;

    spinlock_acq(&context->lock);
    if (pages <= max_pages - context->anon_pages) {
        base = (void *)(context->anon_pages * PAGE_SIZE +
                        CONFIG_VMM_ANON_START);
        context->anon_pages += pages;
    }
    spinlock_rel(&context->lock);

    return base;
}

void *vmm_map(VMMContext *context, void *virt, void *phys, usize len,
              int vmm_flags, MMUFlags mmu_flags) {
    if (vmm_flags & VMM_FLAGS_ALLOCATE) phys = NULL;

#ifdef MMU_EXTRA_FLAGS_SHARED
    if (vmm_flags & VMM_FLAGS_SHARED) mmu_flags |= MMU_EXTRA_FLAGS_SHARED;
#else
#warning "no support for architectures without MMU_EXTRA_FLAGS_SHARED yet"
#endif

    if (vmm_flags & VMM_FLAGS_ALLOCATE) {
        vmm_flags |= VMM_FLAGS_PRIVATE;
#ifdef MMU_EXTRA_FLAGS_NOCOPY
        mmu_flags |= MMU_EXTRA_FLAGS_NOCOPY;
#else
#warning "no support for architectures without MMU_EXTRA_FLAGS_NOCOPY yet"
#endif
    }

#ifdef MMU_EXTRA_FLAGS_PRIVATE
    if (vmm_flags & VMM_FLAGS_PRIVATE && mmu_flags & MMU_FLAGS_WRITE) {
        mmu_flags &= ~MMU_FLAGS_WRITE;
        mmu_flags |= MMU_EXTRA_FLAGS_PRIVATE;
    }
#else
#warning "no support for architectures without MMU_EXTRA_FLAGS_PRIVATE yet"
#endif

    void *page_paddr = (void *)((uptr)phys & ~(uptr)(PAGE_SIZE - 1));
    uptr page_off = (uptr)phys % PAGE_SIZE;
    usize size_p_off = page_off + len;
    usize size_in_pages = (size_p_off + PAGE_SIZE - 1) / PAGE_SIZE;

    context = vmm_ensure_context(context);

    if (vmm_flags & VMM_FLAGS_NOHINT) {
        if (vmm_flags & VMM_FLAGS_ANONYMOUS) {
            virt = vmm_alloc_anon_range(context, size_in_pages);
            if (virt == NULL) return NULL;
        } else if (vmm_flags & VMM_FLAGS_PHYSICAL) {
            virt = (void *)PHYS_TO_HHDM(page_paddr);
            mmu_flags |= MMU_FLAGS_GLOBAL;
        } else {
            assert(!"unsupported vmm flags");
        }
    } else {
        virt = (void *)((uptr)virt & ~(uptr)(PAGE_SIZE - 1));
    }

    for (usize i = 0; i < size_in_pages; i++) {
        uptr offset = i * PAGE_SIZE;
        void *vaddr = (void *)((uptr)virt + offset);

        void *paddr = (void *)((uptr)page_paddr + offset);
        if (vmm_flags & VMM_FLAGS_ALLOCATE) {
            paddr = g_zero_page;
            pmm_hold(g_zero_page);
        }

        bool success = mmu_map(context->pagetable, vaddr, paddr, mmu_flags);
        assert(success);
    }

    return (void *)((uptr)virt + page_off);
}

void vmm_switch(VMMContext *context) {
    if (context == NULL) context = &g_root_context;

    mmu_switch(context->pagetable);
}

void *vmm_get_physical(VMMContext *context, void *virt_addr) {
    context = vmm_ensure_context(context);
    MMUFlags flags;
    int attr;
    void *phys = mmu_get_physical(context->pagetable, virt_addr, &flags, &attr);
    if (phys == NULL || !(attr & (VMM_FAULT_ATTR_MAP | VMM_FAULT_ATTR_COW))) {
        return phys;
    }

    uptr offset = (uptr)virt_addr & (PAGE_SIZE - 1);
    void *old_page = (void *)((uptr)phys - offset);
    void *virt_page = (void *)((uptr)virt_addr - offset);
    void *new_page = (attr & VMM_FAULT_ATTR_MAP) ? pmm_allocz() : pmm_alloc();
    if (new_page == NULL) return NULL;
    if (attr & VMM_FAULT_ATTR_COW) {
        memcpy((void *)PHYS_TO_HHDM(new_page), (void *)PHYS_TO_HHDM(old_page),
               PAGE_SIZE);
    }
    if (!mmu_map(context->pagetable, virt_page, new_page, flags)) {
        pmm_free(new_page);
        return NULL;
    }
    mmu_invalidate(context->pagetable, virt_page, 1);
    pmm_free(old_page);
    return (void *)((uptr)new_page + offset);
}

void vmm_unmap(VMMContext *context, void *virt, usize len) {
    context = vmm_ensure_context(context);

    uptr page_off = (uptr)virt % PAGE_SIZE;
    usize size_p_off = page_off + len;
    usize size_in_pages = (size_p_off + PAGE_SIZE - 1) / PAGE_SIZE;
    virt = (void *)((uptr)virt & ~(uptr)(PAGE_SIZE - 1));

    void *tmp_phys_array[UNMAP_MAX_ARRAY_ON_STACK];
    void **tmp_phys_buf = tmp_phys_array;
    usize tmp_phys_buf_pages = (size_in_pages * sizeof(void *) + PAGE_SIZE -
                                1) /
        PAGE_SIZE;
    usize unmaps_done = 0;
    if (size_in_pages > UNMAP_MAX_ARRAY_ON_STACK) {
        tmp_phys_buf = (void *)PHYS_TO_HHDM(pmm_allocn(tmp_phys_buf_pages));
    }

    for (usize i = 0; i < size_in_pages; i++) {
        usize offset = i * PAGE_SIZE;
        if (!mmu_unmap(context->pagetable, virt + offset, &tmp_phys_buf[i])) {
            tmp_phys_buf[i] = NULL;
            break;
        }
        unmaps_done++;
    }

    mmu_invalidate(context->pagetable, virt, unmaps_done);

    for (usize i = 0; i < unmaps_done; i++) pmm_free(tmp_phys_buf[i]);

    if (size_in_pages > UNMAP_MAX_ARRAY_ON_STACK) {
        pmm_freen((void *)HHDM_TO_PHYS(tmp_phys_buf), tmp_phys_buf_pages);
    }
}

// NOTE: always runs in thread context with interrupts on, unless a kernel bug
// caused it, in which case the state is unknown
void vmm_handle_fault(void *fault_virt, void *fault_phys, Context *fault_ctx,
                      MMUFlags mmu_flags_hint, int attr, usize num_pages_hint) {
    ProfilerData profiler_data = profiler_start_function(
        PROFILE_FUNC_VMM_HANDLE_FAULT);
    Thread *curr_thread = cpu_get_thread();
    if (unlikely(curr_thread == NULL)) {
        panic(fault_ctx,
              "unrecoverable page fault in kernel setup code on "
              "address (virt = %p, phys = %p)",
              fault_virt, fault_phys);
    }

    Proc *curr_proc = curr_thread->proc;
    bool is_user = context_is_user(&curr_thread->ctx);
    bool thread_ctx = context_is_user(fault_ctx) ||
        MMU_ADDR_IS_USER(fault_virt);
    if (unlikely(!thread_ctx)) goto kernel_fault;

    // Spinlocks disable interrupts, so this also catches faults under them
    if (unlikely(!context_intstatus(fault_ctx))) {
        panic(fault_ctx,
              "page fault on user memory with interrupts disabled "
              "(virt = %p)",
              fault_virt);
    }
    assert(curr_thread->lock_depth == 1);
    curr_thread->lock_depth = 0;
    cpu_enable_interrupts();

    if (unlikely(!is_user || curr_proc == NULL)) goto kernel_thread;

    if (likely(attr & VMM_FAULT_ATTR_MAP)) {
        void *fault_virt_page = (void *)((uptr)fault_virt &
                                         ~(uptr)(PAGE_SIZE - 1));
        for (usize i = 0; i < num_pages_hint;) {
            usize got;
            void *new_phys = pmm_alloczn_max(num_pages_hint - i, &got);
            for (usize j = 0; j < got; j++, i++) {
                mmu_map(curr_proc->vmm_context->pagetable,
                        fault_virt_page + PAGE_SIZE * i,
                        new_phys + PAGE_SIZE * j, mmu_flags_hint);
            }
        }
        mmu_invalidate(curr_proc->vmm_context->pagetable, fault_virt_page,
                       num_pages_hint);
        // pmm_free(ZeroPage);
        goto end;
    }

    if (likely(attr & VMM_FAULT_ATTR_COW)) {
        void *fault_virt_page = (void *)((uptr)fault_virt &
                                         ~(uptr)(PAGE_SIZE - 1));
        void *fault_phys_page = (void *)((uptr)fault_phys &
                                         ~(uptr)(PAGE_SIZE - 1));
        if (!pmm_is_shared(fault_phys_page)) {
            mmu_map(curr_proc->vmm_context->pagetable, fault_virt_page,
                    fault_phys_page, mmu_flags_hint);
            mmu_invalidate(curr_proc->vmm_context->pagetable, fault_virt_page,
                           1);
            goto end;
        }
        void *fault_hhdm = (void *)PHYS_TO_HHDM(fault_phys_page);
        void *new_phys = (void *)pmm_alloc();
        void *new_hhdm = (void *)PHYS_TO_HHDM(new_phys);
        memcpy(new_hhdm, fault_hhdm, PAGE_SIZE);
        mmu_map(curr_proc->vmm_context->pagetable, fault_virt_page, new_phys,
                mmu_flags_hint);
        mmu_invalidate(curr_proc->vmm_context->pagetable, fault_virt_page, 1);
        pmm_free(fault_phys_page);
        goto end;
    }

    if (likely(context_is_user(fault_ctx))) {
        signal_send_to_proc(curr_proc->pid, SIGSEGV, NULL);
        goto end;
    }

kernel_thread:
    if (usercopy_fixup(fault_ctx)) goto end;

kernel_fault:

    panic(fault_ctx,
          "unrecoverable page fault by %s thread (tid = %d) on "
          "address (virt = %p, phys = %p) in %s context",
          (is_user) ? "USER" : "KERNEL", curr_thread->tid, fault_virt,
          fault_phys, context_is_user(fault_ctx) ? "USER" : "KERNEL");

end:
    if (thread_ctx) {
        cpu_disable_interrupts();
        curr_thread->lock_depth = 1;
    }
    profiler_end_function(&profiler_data);
}

void vmm_init(void) {
    log("initializing vmm");
    g_root_context.pagetable = mmu_get_table();
    g_root_context.lock = SPINLOCK_NEW();
    g_zero_page = pmm_allocz();
}

void *vmm_prealloc_range(VMMContext *context, VMMRangeType rangetype,
                         usize len) {
    (void)rangetype;
    context = vmm_ensure_context(context);
    return vmm_alloc_anon_range(context,
                                len / PAGE_SIZE + (len % PAGE_SIZE != 0));
}

void vmm_init_reaper(void) { log("TODO: reimplement vmm reaper"); }
