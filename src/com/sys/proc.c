#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/ds/hashmap.h>
#include <kernel/com/ds/radixtree.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <signal.h>
#include <stdatomic.h>
#include <vendor/tailq.h>

static Spinlock g_pid_namespace_lock = SPINLOCK_NEW();
static RadixTree g_processes;
static pid_t g_next_pid = 1;
static RadixTree g_proc_group_map;

static void proc_notify_parent(Proc *proc) {
    Proc *parent = proc_get_by_pid(proc->parent_pid);
    if (parent != NULL) {
        spinlock_acq(&parent->waitpid_lock);
        sched_notify_all(&parent->waitpid_waitlist);
        spinlock_rel(&parent->waitpid_lock);
        proc_rel(parent);
    }
}

static bool fd_free(Proc *proc, int fd) {
    return proc->fd[fd].file == NULL && !proc->fd[fd].reserved;
}

static void proc_update_fd_hint(Proc *proc) {
    // Best case: there's a file after the hint
    for (; proc->next_fd < CONFIG_OPEN_MAX; proc->next_fd++) {
        if (fd_free(proc, proc->next_fd)) goto end;
    }

    // Worst case: retry from the start, files might have been liberated without
    // calling close
    for (proc->next_fd = 0;
         proc->next_fd < CONFIG_OPEN_MAX && !fd_free(proc, proc->next_fd);
         proc->next_fd++);
end:
    proc->max_fd = max(proc->max_fd, proc->next_fd);
}

Proc *proc_new(VMMContext *vmm_context, pid_t parent_pid, Vnode *root,
               Vnode *cwd) {
    Proc *proc = slab_alloc(sizeof(Proc));
    proc->vmm_context = vmm_context;
    proc->state = PROC_STATE_RUNNING;
    proc->state_cause_sig = SIGNAL_NONE;
    proc->state_notified = false;
    proc->exit_code = 0;
    proc->num_children = 0;
    proc->parent_pid = parent_pid;
    vfs_vnode_hold(root);
    vfs_vnode_hold(cwd);
    proc->root = root;
    proc->cwd = cwd;
    proc->num_ref = 1; // 1 because of the parent
    proc->num_running_threads = 0;
    proc->waitpid_lock = SPINLOCK_NEW();
    waitlist_init(&proc->waitpid_waitlist);
    waitlist_init(&proc->stop_waitlist);

    proc->threads_lock = SPINLOCK_NEW();
    TAILQ_INIT(&proc->threads);
    waitlist_init(&proc->threads_waitlist);
    proc->threads_killer = NULL;

    spinlock_acq(&g_processes.lock);
    assert(g_next_pid <= CONFIG_PROC_MAX);
    proc->pid = g_next_pid++;
    radixtree_put_nolock(&g_processes, proc->pid - 1, proc);
    spinlock_rel(&g_processes.lock);

    proc->fd_lock = SPINLOCK_NEW();
    proc->next_fd = 0;
    proc->max_fd = 0;

    proc->pg_lock = SPINLOCK_NEW();
    proc->proc_group = NULL;
    proc->did_execve = false;

    proc->signal_lock = SPINLOCK_NEW();
    signal_sigmask_init(&proc->pending_signals);

    Proc *parent = proc_get_by_pid(parent_pid);
    if (parent != NULL) {
        spinlock_acq(&parent->pg_lock);
        if (parent->proc_group != NULL) {
            proc_join_group(proc, parent->proc_group);
        }
        spinlock_rel(&parent->pg_lock);
        proc_rel(parent);
    }

    return proc;
}

void proc_destroy(Proc *proc) {
    assert(proc->num_ref == 0);
    assert(proc->state == PROC_STATE_EXITED);

    VMMContext *proc_vmm_ctx = proc->vmm_context;
    slab_free(proc, sizeof(Proc));
    vmm_destroy_context(proc_vmm_ctx);
}

int proc_next_fd(Proc *proc) {
    spinlock_acq(&proc->fd_lock);
    if (proc->next_fd >= CONFIG_OPEN_MAX || !fd_free(proc, proc->next_fd)) {
        proc_update_fd_hint(proc);
    }

    int ret = proc->next_fd;
    if (ret < CONFIG_OPEN_MAX) {
        proc->fd[ret].reserved = true;
        proc->next_fd++;
        proc_update_fd_hint(proc);
    } else {
        ret = -1;
    }

    spinlock_rel(&proc->fd_lock);
    return ret;
}

void proc_install_fd(Proc *proc, int fd, File *file, u64 fd_flags) {
    spinlock_acq(&proc->fd_lock);
    assert(proc->fd[fd].reserved && proc->fd[fd].file == NULL);
    proc->fd[fd] = (FileDesc){.file = file, .flags = fd_flags};
    spinlock_rel(&proc->fd_lock);
}

void proc_unreserve_fd(Proc *proc, int fd) {
    spinlock_acq(&proc->fd_lock);
    assert(proc->fd[fd].reserved && proc->fd[fd].file == NULL);
    proc->fd[fd].reserved = false;
    if (fd < proc->next_fd) proc->next_fd = fd;
    spinlock_rel(&proc->fd_lock);
}

FileDesc *proc_get_fildesc_nolock(Proc *proc, int fd) {
    if (proc == NULL) return NULL;
    if (fd >= CONFIG_OPEN_MAX || fd < 0) return NULL;

    FileDesc *fildesc = &proc->fd[fd];
    return fildesc;
}

File *proc_get_file(Proc *proc, int fd) {
    spinlock_acq(&proc->fd_lock);
    File *file = proc_get_file_nolock(proc, fd);
    spinlock_rel(&proc->fd_lock);
    return file;
}

File *proc_get_file_nolock(Proc *proc, int fd) {
    if (proc == NULL) return NULL;
    if (fd >= CONFIG_OPEN_MAX || fd < 0) return NULL;

    File *file = proc->fd[fd].file;
    FILE_HOLD(file);
    return file;
}

int proc_duplicate_file_nolock(Proc *proc, int new_fd, int old_fd) {
    if (old_fd < 0 || old_fd >= CONFIG_OPEN_MAX || new_fd < 0 ||
        new_fd >= CONFIG_OPEN_MAX) {
        return -EINVAL;
    }

    while (new_fd < CONFIG_OPEN_MAX && !fd_free(proc, new_fd)) new_fd++;
    if (new_fd >= CONFIG_OPEN_MAX) return -EMFILE;

    proc->fd[new_fd] = proc->fd[old_fd];
    proc->fd[new_fd].flags = 0;
    proc->max_fd = max(proc->max_fd, new_fd + 1);
    proc_update_fd_hint(proc);

    FILE_HOLD(proc->fd[new_fd].file);
    log_debug("after dup, fd=%d/%d has ref=%d/%d", old_fd, new_fd,
              proc->fd[old_fd].file->num_ref, proc->fd[new_fd].file->num_ref);

    return new_fd;
}

int proc_duplicate_file(Proc *proc, int new_fd, int old_fd) {
    spinlock_acq(&proc->fd_lock);
    int ret = proc_duplicate_file_nolock(proc, new_fd, old_fd);
    spinlock_rel(&proc->fd_lock);
    return ret;
}

int proc_close_file_nolock(Proc *proc, int fd, File **out_file) {
    if (fd < 0 || fd >= CONFIG_OPEN_MAX) return EBADF;

    FileDesc *fildesc = &proc->fd[fd];
    if (fildesc->file == NULL) return EBADF;

    *out_file = fildesc->file;
    *fildesc = (FileDesc){0};

    // Recycle file descriptors
    if (fd < proc->next_fd) proc->next_fd = fd;

    return 0;
}

int proc_close_file(Proc *proc, int fd) {
    spinlock_acq(&proc->fd_lock);
    File *file = NULL;
    int ret = proc_close_file_nolock(proc, fd, &file);
    spinlock_rel(&proc->fd_lock);
    FILE_REL(file);
    return ret;
}

Vnode *proc_get_cwd(Proc *proc) {
    spinlock_acq(&proc->fd_lock);
    Vnode *cwd = proc->cwd;
    vfs_vnode_hold(cwd);
    spinlock_rel(&proc->fd_lock);
    return cwd;
}

void proc_set_cwd(Proc *proc, Vnode *cwd) {
    vfs_vnode_hold(cwd);
    spinlock_acq(&proc->fd_lock);
    Vnode *old = proc->cwd;
    proc->cwd = cwd;
    spinlock_rel(&proc->fd_lock);

    // Outside the spinlock, since it may free the vnode
    vfs_vnode_rel(old);
}

int proc_get_directory(Proc *proc, int dir_fd, Vnode **out_dir) {
    if (dir_fd == AT_FDCWD) {
        *out_dir = proc_get_cwd(proc);
        return 0;
    }

    File *file = proc_get_file(proc, dir_fd);
    if (file == NULL) {
        *out_dir = NULL;
        return EBADF;
    }

    *out_dir = file->vnode;
    vfs_vnode_hold(*out_dir);
    FILE_REL(file);
    return 0;
}

// A count of 0 means that the last holder is on its way to proc_hide. A
// new reference would make the next release hide the process a second time
static bool proc_hold_if_alive(Proc *proc) {
    usize num_ref = __atomic_load_n(&proc->num_ref, __ATOMIC_RELAXED);
    while (num_ref != 0) {
        if (__atomic_compare_exchange_n(&proc->num_ref, &num_ref, num_ref + 1,
                                        false, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
            return true;
        }
    }
    return false;
}

Proc *proc_get_by_pid(pid_t pid) {
    if (pid > CONFIG_PROC_MAX || pid < 1) return NULL;

    Proc *ret = NULL;
    spinlock_acq(&g_pid_namespace_lock);
    radixtree_get_nolock(&g_processes, pid - 1, (void *)&ret);
    if (ret != NULL && !proc_hold_if_alive(ret)) ret = NULL;
    spinlock_rel(&g_pid_namespace_lock);
    return ret;
}

Proc *proc_get_arbitrary_child(Proc *proc) {
    spinlock_acq(&g_pid_namespace_lock);

    Proc *ret = NULL;
    for (int i = 0; i < g_next_pid + 1; i++) {
        Proc *candidate = NULL;
        radixtree_get_nolock(&g_processes, i, (void *)&candidate);

        if (candidate != NULL && candidate->parent_pid == proc->pid) {
            spinlock_acq(&candidate->signal_lock);
            bool eligible = candidate->state != PROC_STATE_EXITED ||
                !candidate->state_notified;
            bool ready = (candidate->state == PROC_STATE_EXITED ||
                          candidate->state == PROC_STATE_STOPPED) &&
                !candidate->state_notified;
            spinlock_rel(&candidate->signal_lock);
            if (eligible && (ret == NULL || ready)) ret = candidate;
            if (eligible && ready) break;
        }
    }

    if (ret != NULL) proc_hold(ret);
    spinlock_rel(&g_pid_namespace_lock);
    return ret;
}

bool proc_threads_alive_nolock(Proc *proc, Thread *except) {
    Thread *thread = TAILQ_FIRST(&proc->threads);
    return thread != NULL &&
        (thread != except || TAILQ_NEXT(thread, proc_threads) != NULL);
}

bool proc_all_threads_exiting_nolock(Proc *proc) {
    Thread *thread;
    TAILQ_FOREACH(thread, &proc->threads, proc_threads) {
        if (!__atomic_load_n(&thread->exit_requested, __ATOMIC_ACQUIRE)) {
            return false;
        }
    }
    return true;
}

void proc_add_thread(Proc *proc, Thread *thread) {
    assert(proc->state != PROC_STATE_EXITED);
    spinlock_acq(&proc->threads_lock);
    TAILQ_INSERT_TAIL(&proc->threads, thread, proc_threads);
    proc_hold(proc);
    spinlock_rel(&proc->threads_lock);
}

void proc_remove_thread(Proc *proc, Thread *thread) {
    spinlock_acq(&proc->threads_lock);
    proc_remove_thread_nolock(proc, thread);
    spinlock_rel(&proc->threads_lock);
    sched_notify_all(&proc->threads_waitlist);
}

void proc_remove_thread_nolock(Proc *proc, struct Thread *thread) {
    TAILQ_REMOVE(&proc->threads, thread, proc_threads);
}

void proc_kill_other_threads(void) {
    Thread *curr_thraed = cpu_get_thread();
    assert(curr_thraed != NULL);
    Proc *curr_proc = curr_thraed->proc;
    assert(curr_proc != NULL);

    spinlock_acq(&curr_proc->threads_lock);
    proc_kill_other_threads_nolock();
    spinlock_rel(&curr_proc->threads_lock);
}

// Caller holds threads_lock. Wait for switch-out acknowledgement before
// releasing process resources or replacing its address space during exec
void proc_kill_other_threads_nolock(void) {
    Thread *curr = cpu_get_thread();
    Proc *proc = curr->proc;
    if (proc->threads_killer != NULL && proc->threads_killer != curr) {
        spinlock_rel(&proc->threads_lock);
        thread_exit(curr);
        __builtin_unreachable();
    }
    proc->threads_killer = curr;
    Thread *thread;
    TAILQ_FOREACH(thread, &proc->threads, proc_threads) {
        if (thread != curr) thread_request_exit(thread);
    }
    while (proc_threads_alive_nolock(proc, curr)) {
        spinlock_wait(&proc->threads_lock, &proc->threads_waitlist, 0, false);
    }
    proc->threads_killer = NULL;
}

void proc_transition_nolock(Proc *proc, ProcState state, int cause_sig) {
    switch (state) {
        case PROC_STATE_RUNNING:
            assert(proc->state == PROC_STATE_STOPPED);
            break;
        case PROC_STATE_STOPPED:
            assert(proc->state == PROC_STATE_RUNNING);
            assert(cause_sig != SIGNAL_NONE);
            break;
        case PROC_STATE_EXITED:
            assert(proc->state != PROC_STATE_EXITED);
            break;
        default:
            assert(!"invalid argument");
            break;
    }

    proc->state = state;
    proc->state_cause_sig = cause_sig;
    proc->state_notified = false;
}

void proc_exit(int exit_code, int cause_sig) {
    Thread *curr_thread = cpu_get_thread();
    assert(curr_thread != NULL);
    Proc *curr_proc = curr_thread->proc;
    assert(curr_proc != NULL);
    assert(curr_proc->num_running_threads == 1);

    // NOTE: we don't need to take the fd lock because we are guaranteed to be
    // the only thread left
    for (int i = 0; i < curr_proc->max_fd; i++) {
        File *file = curr_proc->fd[i].file;
        curr_proc->fd[i] = (FileDesc){0};
        FILE_REL(file);
    }

    Vnode *cwd = curr_proc->cwd;
    Vnode *root = curr_proc->root;
    curr_proc->cwd = curr_proc->root = NULL;
    vfs_vnode_rel(cwd);
    vfs_vnode_rel(root);

    // A session loses its controlling terminal with the controlling process
    ProcSession *session = curr_proc->proc_group->session;
    if (session != NULL && session->sid == curr_proc->pid) {
        spinlock_acq(&session->pg_lock);
        Vnode *ctty = session->tty;
        __atomic_store_n(&session->tty, NULL, __ATOMIC_RELAXED);
        spinlock_rel(&session->pg_lock);
        vfs_vnode_rel(ctty);
    }

    // Publish status before notifying and never acquire authority under
    // signal_lock
    spinlock_acq(&curr_proc->signal_lock);
    curr_proc->exit_code = exit_code;
    proc_transition_nolock(curr_proc, PROC_STATE_EXITED, cause_sig);

    spinlock_rel(&curr_proc->signal_lock);
    proc_notify_parent(curr_proc);
    signal_send_to_proc(curr_proc->parent_pid, SIGCHLD, curr_proc);

    log_debug("pid=%d exited with code %d", curr_proc->pid, exit_code);
}

void proc_stop_nolock(int stop_signal) {
    Thread *curr_thread = cpu_get_thread();
    assert(curr_thread != NULL);
    Proc *curr_proc = curr_thread->proc;
    assert(curr_proc != NULL);
    assert(stop_signal != SIGNAL_NONE);

    if (curr_proc->state != PROC_STATE_RUNNING) return;

    proc_transition_nolock(curr_proc, PROC_STATE_STOPPED, stop_signal);

    spinlock_rel(&curr_proc->signal_lock);
    Thread *t, *_;
    spinlock_acq(&curr_proc->threads_lock);
    TAILQ_FOREACH_SAFE(t, &curr_proc->threads, proc_threads, _) {
        signal_send_to_thread(t, stop_signal, curr_proc);
    }
    spinlock_rel(&curr_proc->threads_lock);
    proc_notify_parent(curr_proc);
    signal_send_to_proc(curr_proc->parent_pid, SIGCHLD, curr_proc);
    spinlock_acq(&curr_proc->signal_lock);
}

void proc_terminate(int exit_code, int cause_sig) {
    Thread *curr_thread = cpu_get_thread();
    assert(curr_thread != NULL);
    Proc *curr_proc = curr_thread->proc;
    assert(curr_proc != NULL);

    proc_kill_other_threads();
    proc_exit(exit_code, cause_sig);
    // NOTE: now the process is in an exited state, but this thread is still
    // running. We could be preempted here, so it's important that the scheduler
    // never refuses to run a thread if it's process has exited, because we want
    // to get back here and finish our job
    thread_exit(curr_thread);
}

void proc_hide(Proc *proc) {
    spinlock_acq(&g_pid_namespace_lock);
    // TODO: add it to new reaper thread queue
    // the new reaper thread will work differently from the previous: since we
    // have this function now, we can just have other parts drop their refcounts
    // and we'll always know when it hits 0 and we can dispatch the reaper
    // thread accordingly without hash tables or scheduler magic
    radixtree_remove_nolock(&g_processes, proc->pid - 1);
    spinlock_acq(&proc->pg_lock);
    if (proc->proc_group != NULL) {
        spinlock_acq(&proc->proc_group->procs_lock);
        TAILQ_REMOVE(&proc->proc_group->procs, proc, procs);
        spinlock_rel(&proc->proc_group->procs_lock);
    }
    proc->proc_group = NULL;
    spinlock_rel(&proc->pg_lock);
    spinlock_rel(&g_pid_namespace_lock);
}

ProcGroup *proc_new_group(Proc *leader, ProcSession *session) {
    spinlock_acq(&g_proc_group_map.lock);
    ProcGroup *group = slab_alloc(sizeof(ProcGroup));
    group->procs_lock = SPINLOCK_NEW();
    group->pgid = leader->pid;
    group->session = session;
    TAILQ_INIT(&group->procs);

    if (session == NULL && leader->proc_group != NULL) {
        group->session = leader->proc_group->session;
    }

    verify(radixtree_put_nolock(&g_proc_group_map, group->pgid, group) == 0);
    log_debug("created pgid=%d", group->pgid);
    spinlock_rel(&g_proc_group_map.lock);
    return group;
}

int proc_join_group(Proc *proc, ProcGroup *group) {
    spinlock_acq(&proc->pg_lock);
    int ret = proc_join_group_nolock(proc, group);
    spinlock_rel(&proc->pg_lock);
    return ret;
}

int proc_join_group_nolock(Proc *proc, ProcGroup *group) {
    if (proc->proc_group != NULL) {
        assert(proc->proc_group->session != NULL);
        assert(group->session != NULL);

        /*if (proc->proc_group->session->sid != proc->pid &&
            proc->proc_group->session->sid != group->session->sid) {
            return EPERM;
        }*/

        spinlock_acq(&proc->proc_group->procs_lock);
        TAILQ_REMOVE(&proc->proc_group->procs, proc, procs);
        spinlock_rel(&proc->proc_group->procs_lock);
    }

    spinlock_acq(&group->procs_lock);
    TAILQ_INSERT_TAIL(&group->procs, proc, procs);
    proc->proc_group = group;
    log_debug("pid=%d is now part of group pgid=%d", proc->pid, group->pgid);
    spinlock_rel(&group->procs_lock);
    return 0;
}

ProcGroup *proc_get_group_by_pgid(pid_t pgid) {
    spinlock_acq(&g_proc_group_map.lock);
    ProcGroup *group;
    if (radixtree_get_nolock(&g_proc_group_map, pgid, (void **)&group) != 0) {
        group = NULL;
    }
    spinlock_rel(&g_proc_group_map.lock);
    return group;
}

ProcSession *proc_new_session_nolock(Proc *leader, Vnode *tty) {
    ProcSession *session = slab_alloc(sizeof(ProcSession));
    if (tty != NULL) vfs_vnode_hold(tty);
    session->tty = tty;
    session->pg_lock = SPINLOCK_NEW();
    session->sid = leader->pid;
    TAILQ_INIT(&session->proc_groups);

    if (leader->proc_group != NULL) {
        spinlock_acq(&leader->proc_group->procs_lock);
        TAILQ_REMOVE(&leader->proc_group->procs, leader, procs);
        spinlock_rel(&leader->proc_group->procs_lock);
    }

    ProcGroup *new_group = proc_new_group(leader, session);
    proc_join_group_nolock(leader, new_group);

    log_debug("pid=%d is now leader of session sid=%d", leader->pid,
              session->sid);
    return session;
}

void proc_init(void) {
    // 2 layers of radix tree are sufficient to hold like 260K processes on a
    // 64-bit system, so they should be enough. More layers may break
    // compatibility and cause lookup overhead
    radixtree_init(&g_processes, 2);
    radixtree_init(&g_proc_group_map, 2);
}
