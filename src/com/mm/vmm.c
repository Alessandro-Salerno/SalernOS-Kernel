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

#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <lib/str.h>
#include <stdatomic.h>

#define UNMAP_MAX_ARRAY_ON_STACK 64

static com_vmm_context_t RootContext = {0};
static void             *ZeroPage    = NULL;

// This lock guards the context queue and is used as condvar for the waitlist
static kspinlock_t ZombieQueueLock = KSPINLOCK_NEW();
// Queue of context pointers on which destroy() was called
static TAILQ_HEAD(, com_vmm_context) ZombieContextQueue;
// Waitlist used only by the vmm reaper thread to wait
static com_waitlist_t ReaperThreadWaitlist;
// Number of elements in the zombie context queue
static size_t ReaperNumPending = 0;

static inline com_vmm_context_t *vmm_current_context(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL == curr_thread || NULL == curr_thread->proc ||
        NULL == curr_thread->proc->vmm_context) {
        return &RootContext;
    }
    return curr_thread->proc->vmm_context;
}

static inline com_vmm_context_t *
vmm_ensure_context(com_vmm_context_t *context) {
    KASSERT(&RootContext != context);

    if (NULL == context) {
        return vmm_current_context();
    }

    return context;
}

static void vmm_reaper_thread(void) {
    bool already_done = false;
    TAILQ_HEAD(, com_vmm_context) local_zombies;
    TAILQ_INIT(&local_zombies);

    while (true) {
        // Allow kernel preemption
        com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
        curr_thread->lock_depth   = 0;

        size_t             num_done = 0;
        com_vmm_context_t *context, *_;

        // First, we move the zombies to a local queue. This way, we hold the
        // lock as little as possible and don't bottleneck others wanting to add
        // to the queue
        kspinlock_acquire(&ZombieQueueLock);
        if (TAILQ_EMPTY(&ZombieContextQueue) || already_done) {
            already_done = false;
            com_sys_sched_wait(&ReaperThreadWaitlist, &ZombieQueueLock);
        }
        TAILQ_FOREACH_SAFE(context, &ZombieContextQueue, reaper_entry, _) {
            if (CONFIG_VMM_REAPER_MAX == num_done) {
                break;
            }

            TAILQ_REMOVE(&ZombieContextQueue, context, reaper_entry);
            TAILQ_INSERT_TAIL(&local_zombies, context, reaper_entry);
            ReaperNumPending--;
            num_done++;
        }
        kspinlock_release(&ZombieQueueLock);

        // Then we destroy the ones in the local zombie queue
        TAILQ_FOREACH_SAFE(context, &local_zombies, reaper_entry, _) {
            KASSERT(context != vmm_current_context());
            KASSERT(context->pagetable != arch_mmu_get_table());
            KASSERT(NULL != context->pagetable);
            arch_mmu_destroy_table(context->pagetable);
            TAILQ_REMOVE_HEAD(&local_zombies, reaper_entry);
            com_mm_slab_free(context, sizeof(com_vmm_context_t));
        }

        already_done = true;
    }
}

com_vmm_context_t *com_mm_vmm_new_context(arch_mmu_pagetable_t *pagetable) {
    if (NULL == pagetable) {
        pagetable = arch_mmu_new_table();
    }

    com_vmm_context_t *context = com_mm_slab_alloc(sizeof(com_vmm_context_t));
    context->pagetable         = pagetable;
    context->lock              = KSPINLOCK_NEW();
    return context;
}

void com_mm_vmm_destroy_context(com_vmm_context_t *context) {
    KASSERT(NULL != context);
    KASSERT(&RootContext != context);
    KASSERT(RootContext.pagetable != context->pagetable);
    KASSERT(context != vmm_current_context());
    KASSERT(context->pagetable != arch_mmu_get_table());
    KASSERT(NULL != context->pagetable);

    kspinlock_acquire(&ZombieQueueLock);
    TAILQ_INSERT_TAIL(&ZombieContextQueue, context, reaper_entry);
    if (++ReaperNumPending >= CONFIG_VMM_REAPER_NOTIFY) {
        com_sys_sched_notify(&ReaperThreadWaitlist);
    }
    kspinlock_release(&ZombieQueueLock);
}

com_vmm_context_t *com_mm_vmm_duplicate_context(com_vmm_context_t *context) {
    if (NULL == context) {
        return com_mm_vmm_new_context(NULL);
    }

    arch_mmu_pagetable_t *new_pt = arch_mmu_duplicate_table(context->pagetable);
    com_vmm_context_t    *new_vmm_ctx = com_mm_vmm_new_context(new_pt);
    new_vmm_ctx->anon_pages           = context->anon_pages;
    return new_vmm_ctx;
}

void *com_mm_vmm_map(com_vmm_context_t *context,
                     void              *virt,
                     void              *phys,
                     size_t             len,
                     int                vmm_flags,
                     arch_mmu_flags_t   mmu_flags) {
    if (COM_MM_VMM_FLAGS_ALLOCATE & vmm_flags) {
        phys = NULL;
    }

#ifdef ARCH_MMU_EXTRA_FLAGS_SHARED
    if (COM_MM_VMM_FLAGS_SHARED & vmm_flags) {
        mmu_flags |= ARCH_MMU_EXTRA_FLAGS_SHARED;
    }
#else
#warning "no support for architectures without ARCH_MMU_EXTRA_FLAGS_SHARED yet"
#endif

    if (COM_MM_VMM_FLAGS_ALLOCATE & vmm_flags) {
        vmm_flags |= COM_MM_VMM_FLAGS_PRIVATE;
#ifdef ARCH_MMU_EXTRA_FLAGS_NOCOPY
        mmu_flags |= ARCH_MMU_EXTRA_FLAGS_NOCOPY;
#else
#warning "no support for architectures without ARCH_MMU_EXTRA_FLAGS_NOCOPY yet"
#endif
    }

#ifdef ARCH_MMU_EXTRA_FLAGS_PRIVATE
    if (COM_MM_VMM_FLAGS_PRIVATE & vmm_flags &&
        ARCH_MMU_FLAGS_WRITE & mmu_flags) {
        mmu_flags &= ~ARCH_MMU_FLAGS_WRITE;
        mmu_flags |= ARCH_MMU_EXTRA_FLAGS_PRIVATE;
    }
#else
#warning "no support for architectures without ARCH_MMU_EXTRA_FLAGS_PRIVATE yet"
#endif

    void     *page_paddr = (void *)((uintptr_t)phys &
                                ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
    uintptr_t page_off   = (uintptr_t)phys % ARCH_PAGE_SIZE;
    size_t    size_p_off = page_off + len;
    size_t size_in_pages = (size_p_off + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

    context = vmm_ensure_context(context);

    if (COM_MM_VMM_FLAGS_NOHINT & vmm_flags) {
        if (COM_MM_VMM_FLAGS_ANONYMOUS & vmm_flags) {
            KASSERT(&RootContext != context);
            KASSERT(RootContext.pagetable != context->pagetable);
            kspinlock_acquire(&context->lock);
            virt = (void *)(context->anon_pages * ARCH_PAGE_SIZE +
                            CONFIG_VMM_ANON_START);
            context->anon_pages += size_in_pages;
            kspinlock_release(&context->lock);
        } else if (COM_MM_VMM_FLAGS_PHYSICAL & vmm_flags) {
            virt = (void *)ARCH_PHYS_TO_HHDM(page_paddr);
        } else {
            KASSERT(!"unsupported vmm flags");
        }
    } else {
        virt = (void *)((uintptr_t)virt & ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
    }

    for (size_t i = 0; i < size_in_pages; i++) {
        uintptr_t offset = i * ARCH_PAGE_SIZE;
        void     *vaddr  = (void *)((uintptr_t)virt + offset);

        void *paddr = (void *)((uintptr_t)page_paddr + offset);
        if (COM_MM_VMM_FLAGS_ALLOCATE & vmm_flags) {
            paddr = ZeroPage;
            com_mm_pmm_hold(ZeroPage);
        }

        bool success = arch_mmu_map(context->pagetable,
                                    vaddr,
                                    paddr,
                                    mmu_flags);
        KASSERT(success);
    }

    return (void *)((uintptr_t)virt + page_off);
}

void com_mm_vmm_switch(com_vmm_context_t *context) {
    if (NULL == context) {
        context = &RootContext;
    }

    arch_mmu_switch(context->pagetable);
}

void *com_mm_vmm_get_physical(com_vmm_context_t *context, void *virt_addr) {
    context = vmm_ensure_context(context);
    return arch_mmu_get_physical(context->pagetable, virt_addr);
}

void com_mm_vmm_unmap(com_vmm_context_t *context, void *virt, size_t len) {
    context = vmm_ensure_context(context);

    uintptr_t page_off   = (uintptr_t)virt % ARCH_PAGE_SIZE;
    size_t    size_p_off = page_off + len;
    size_t size_in_pages = (size_p_off + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    virt = (void *)((uintptr_t)virt & ~(uintptr_t)(ARCH_PAGE_SIZE - 1));

    void  *tmp_phys_array[UNMAP_MAX_ARRAY_ON_STACK];
    void **tmp_phys_buf       = tmp_phys_array;
    size_t tmp_phys_buf_pages = (size_in_pages * sizeof(void *) +
                                 ARCH_PAGE_SIZE - 1) /
                                ARCH_PAGE_SIZE;
    size_t unmaps_done = 0;
    if (size_in_pages > UNMAP_MAX_ARRAY_ON_STACK) {
        tmp_phys_buf = (void *)ARCH_PHYS_TO_HHDM(
            com_mm_pmm_alloc_many(tmp_phys_buf_pages));
    }

    for (size_t i = 0; i < size_in_pages; i++) {
        size_t offset = i * ARCH_PAGE_SIZE;
        if (!arch_mmu_unmap(&tmp_phys_buf[i],
                            context->pagetable,
                            virt + offset)) {
            tmp_phys_buf[i] = NULL;
            break;
        }
        unmaps_done++;
    }

    arch_mmu_invalidate(context->pagetable, virt, unmaps_done);

    for (size_t i = 0; i < unmaps_done; i++) {
        com_mm_pmm_free(tmp_phys_buf[i]);
    }

    if (size_in_pages > UNMAP_MAX_ARRAY_ON_STACK) {
        com_mm_pmm_free_many((void *)ARCH_HHDM_TO_PHYS(tmp_phys_buf),
                             tmp_phys_buf_pages);
    }
}

void com_mm_vmm_handle_fault(void            *fault_virt,
                             void            *fault_phys,
                             arch_context_t  *fault_ctx,
                             arch_mmu_flags_t mmu_flags_hint,
                             int              attr) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL == curr_thread) {
        com_sys_panic(fault_ctx,
                      "unrecoverable page fault in kernel setup code on "
                      "address (virt = %p, phys = %p)",
                      fault_virt,
                      fault_phys);
    }

    com_proc_t *curr_proc = curr_thread->proc;
    bool        is_user   = ARCH_CONTEXT_ISUSER(&curr_thread->ctx);
    if (!is_user || NULL == curr_proc) {
        goto kernel_thread;
    }

    if (COM_MM_VMM_FAULT_ATTR_COW & attr) {
        void *fault_virt_page = (void *)((uintptr_t)fault_virt &
                                         ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
        void *fault_phys_page = (void *)((uintptr_t)fault_phys &
                                         ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
        void *fault_hhdm      = (void *)ARCH_PHYS_TO_HHDM(fault_phys_page);
        void *new_phys        = (void *)com_mm_pmm_alloc();
        void *new_hhdm        = (void *)ARCH_PHYS_TO_HHDM(new_phys);
        kmemcpy(new_hhdm, fault_hhdm, ARCH_PAGE_SIZE);
        arch_mmu_map(curr_proc->vmm_context->pagetable,
                     fault_virt_page,
                     new_phys,
                     mmu_flags_hint);
        arch_mmu_invalidate(curr_proc->vmm_context->pagetable,
                            fault_virt_page,
                            1);
        com_mm_pmm_free(fault_phys_page);
        return;
    }

    if (COM_MM_VMM_FAULT_ATTR_MAP & attr) {
        void *fault_virt_page = (void *)((uintptr_t)fault_virt &
                                         ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
        void *new_phys        = com_mm_pmm_alloc_zero();
        arch_mmu_map(curr_proc->vmm_context->pagetable,
                     fault_virt_page,
                     new_phys,
                     mmu_flags_hint);
        arch_mmu_invalidate(curr_proc->vmm_context->pagetable,
                            fault_virt_page,
                            1);
        com_mm_pmm_free(ZeroPage);
        return;
    }

    if (ARCH_CONTEXT_ISUSER(fault_ctx)) {
        KDEBUG("sending SIGSEGV to pid=%d due to page fault on addr %p",
               curr_proc->pid,
               fault_virt);
        com_ipc_signal_send_to_proc(curr_proc->pid, SIGSEGV, NULL);
        return;
    }

kernel_thread:
    com_sys_panic(fault_ctx,
                  "unrecoverable page fault by %s thread (tid = %d) on "
                  "address (virt = %p, phys = %p) in %s context",
                  (is_user) ? "USER" : "KERNEL",
                  curr_thread->tid,
                  fault_virt,
                  fault_phys,
                  ARCH_CONTEXT_ISUSER(fault_ctx) ? "USER" : "KERNEL");
}

void com_mm_vmm_init(void) {
    KLOG("initializing vmm");
    RootContext.pagetable = arch_mmu_get_table();
    RootContext.lock      = KSPINLOCK_NEW();
    ZeroPage              = com_mm_pmm_alloc_zero();

    // This is here because destroy will add stuff to the queue even if this is
    // not initialized!
    TAILQ_INIT(&ZombieContextQueue);
    COM_SYS_THREAD_WAITLIST_INIT(&ReaperThreadWaitlist);
}

void *com_mm_vmm_prealloc_range(com_vmm_context_t   *context,
                                com_vmm_range_type_t rangetype,
                                size_t               len) {
    (void)rangetype;
    context = vmm_ensure_context(context);
    KASSERT(&RootContext != context);
    KASSERT(RootContext.pagetable != context->pagetable);

    kspinlock_acquire(&context->lock);
    void *range_base = (void *)(context->anon_pages * ARCH_PAGE_SIZE +
                                CONFIG_VMM_ANON_START);
    context->anon_pages += (len + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    kspinlock_release(&context->lock);

    return range_base;
}

void com_mm_vmm_init_reaper(void) {
    KLOG("initializing vmm reaper");
    com_thread_t *reaper_thread = com_sys_thread_new_kernel(NULL,
                                                            vmm_reaper_thread);
    reaper_thread->runnable     = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, reaper_thread, threads);
}
