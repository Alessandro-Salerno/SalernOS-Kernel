#pragma once

#include <vendor/tailq.h>
struct Proc;
struct ProcGroup;
TAILQ_HEAD(ProcTailQ, Proc);
TAILQ_HEAD(ProcGroupTailQ, ProcGroup);

#include <arch/mmu.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <signal.h>
#include <sys/types.h>

typedef struct ProcSession {
    pid_t sid;
    Vnode *tty;
    struct ProcGroupTailQ proc_groups;
    Spinlock pg_lock;
} ProcSession;

typedef struct ProcGroup {
    pid_t pgid;
    ProcSession *session;
    struct ProcTailQ procs;
    Spinlock procs_lock;
    TAILQ_ENTRY(ProcGroup) proc_groups;
} ProcGroup;

typedef enum ProcState {
    PROC_STATE_RUNNING = 0,
    PROC_STATE_STOPPED,
    PROC_STATE_EXITED
} ProcState;

typedef struct Proc {
    pid_t pid;
    pid_t parent_pid;
    VMMContext *vmm_context;
    usize num_children;

    // Threads
    Spinlock threads_lock;
    struct ThreadTailQ threads;
    struct Waitlist threads_waitlist;
    struct Thread *threads_killer;
    usize num_ref;
    usize num_running_threads;

    // Process groups
    Spinlock pg_lock;
    ProcGroup *proc_group;
    TAILQ_ENTRY(Proc) procs;
    bool did_execve;

    // Termination & signals
    // state_cause_sig is the signal that caused the current state (if any)
    cache_aligned Spinlock signal_lock;
    SigMask pending_signals;
    ProcState state;
    int state_cause_sig;
    bool state_notified; // reported to the parent
    int exit_code;
    Spinlock waitpid_lock;
    struct Waitlist waitpid_waitlist;
    struct SignalAction *sigaction[NSIG];
    struct Waitlist stop_waitlist;

    // Both own a reference, cwd is protected by fd_lock
    cache_aligned Vnode *root;
    Vnode *cwd;
    Spinlock fd_lock;
    int next_fd;
    int max_fd;
    FileDesc fd[CONFIG_OPEN_MAX];

    char executable_path[CONFIG_PATH_MAX];
} Proc;

Proc *proc_new(VMMContext *vmm_context, pid_t parent_pid, Vnode *root,
               Vnode *cwd);
void proc_destroy(Proc *proc);

// File management

// NOTE: next_fd needs to distinguish file descriptors that are free (all zero,
// or file == NULL) from those that have been already handed out but that have
// no backing file yet. This can happen because operations like openat() need to
// reserve a file descriptor before they have a vnode/file to install. The
// transition state is currently represented by the "reserved" file descriptor
// flag, and these functions handle it correctly

int proc_next_fd(Proc *proc);
// The caller's reference to file goes to the table
void proc_install_fd(Proc *proc, int fd, File *file, u64 fd_flags);
void proc_unreserve_fd(Proc *proc, int fd);
FileDesc *proc_get_fildesc_nolock(Proc *proc, int fd);
File *proc_get_file(Proc *proc, int fd);
File *proc_get_file_nolock(Proc *proc, int fd);
int proc_duplicate_file_nolock(Proc *proc, int new_fd, int old_fd);
int proc_duplicate_file(Proc *proc, int new_fd, int old_fd);
int proc_close_file_nolock(Proc *proc, int fd, File **out_file);
int proc_close_file(Proc *proc, int fd);
// These return referenced vnodes, set_cwd takes its own reference
Vnode *proc_get_cwd(Proc *proc);
void proc_set_cwd(Proc *proc, Vnode *cwd);
int proc_get_directory(Proc *proc, int dir_fd, Vnode **out_dir);
Proc *proc_get_by_pid(pid_t pid);
Proc *proc_get_arbitrary_child(Proc *proc);

// Threads
// Caller holds threads_lock, excludes except when non-NULL
// Alive means final switch-out has not been acknowledged, even after EXITED
bool proc_threads_alive_nolock(Proc *proc, struct Thread *except);
// Caller holds threads_lock, this checks requests rather than acknowledgements
bool proc_all_threads_exiting_nolock(Proc *proc);
void proc_add_thread(Proc *proc, struct Thread *thread);
void proc_remove_thread(Proc *proc, struct Thread *thread);
void proc_remove_thread_nolock(Proc *proc, struct Thread *thread);

// These work exclusively on the running process
void proc_kill_other_threads(void);
void proc_kill_other_threads_nolock(void);
void proc_transition_nolock(Proc *proc, ProcState state, int cause_sig);
void proc_exit(int exit_code, int cause_sig);
void proc_stop_nolock(int stop_signal);
void proc_terminate(int exit_code, int cause_sig);

void proc_hide(Proc *proc);

ProcGroup *proc_new_group(Proc *leader, ProcSession *session);
int proc_join_group(Proc *proc, ProcGroup *group);
int proc_join_group_nolock(Proc *proc, ProcGroup *group);
ProcGroup *proc_get_group_by_pgid(pid_t pgid);
ProcSession *proc_new_session_nolock(Proc *leader, Vnode *tty);
void proc_init(void);

static inline void proc_hold(Proc *proc) {
    __atomic_add_fetch(&proc->num_ref, 1, __ATOMIC_RELAXED);
}

static inline void proc_rel(Proc *proc) {
    if (__atomic_add_fetch(&proc->num_ref, -1, __ATOMIC_ACQ_REL) == 0) {
        proc_hide(proc);
    }
}
