#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct {
    struct vma pool[NVMA];
    int is_free[NVMA];
    struct spinlock lock;
} global_vma_pool;

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void wakeup1(struct proc *chan);
static void freeproc(struct proc *p);
static void _munmap_all(struct proc *current_process_ptr);

extern char trampoline[]; // trampoline.S

static void _initialize_vma_pool() {
    initlock(&global_vma_pool.lock, "global_vma_pool");

    for (int i = 0; i < NVMA; i++) {
        global_vma_pool.is_free[i] = 1;
    }
}

// return 0 if failed
static struct vma *_allocate_vma_from_global_pool() {
    acquire(&global_vma_pool.lock);

    for (int offset = 0; offset < NVMA; offset++) {
        if (global_vma_pool.is_free[offset]) {
            global_vma_pool.is_free[offset] = 0;

            release(&global_vma_pool.lock);

            return &global_vma_pool.pool[offset];
        }
    }

    release(&global_vma_pool.lock);

    return 0;
}

static void _deallocate_vma_to_global_pool(struct vma *vma_ptr) {
    int offset = (int)(vma_ptr - global_vma_pool.pool);

    if (offset >= 0 && offset < NVMA) {
        acquire(&global_vma_pool.lock);

        global_vma_pool.is_free[offset] = 1;

        release(&global_vma_pool.lock);
    }

    else {
        panic("_deallocate_vma_to_global_pool: invalid pointer");
    }

    return;
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        char *pa = kalloc();
        if (pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int)(p - proc));
        kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// initialize the proc table at boot time.
void procinit(void) {
    struct proc *p;

    initlock(&pid_lock, "nextpid");
    for (p = proc; p < &proc[NPROC]; p++) {
        initlock(&p->lock, "proc");
        p->kstack = KSTACK((int)(p - proc));
        p->max_mmap_range_end_va = MMAPBASE;
    }

    _initialize_vma_pool();
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid() {
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *mycpu(void) {
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc *myproc(void) {
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int allocpid() {
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *allocproc(void) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == UNUSED) {
            goto found;
        }
        else {
            release(&p->lock);
        }
    }
    return 0;

found:
    p->pid = allocpid();

    // Allocate a trapframe page.
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
        release(&p->lock);
        return 0;
    }

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void freeproc(struct proc *p) {
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t proc_pagetable(struct proc *p) {
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(
            pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X
        )
        < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe just below TRAMPOLINE, for trampoline.S.
    if (mappages(
            pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe), PTE_R | PTE_W
        )
        < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02, 0x97,
                    0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02, 0x93, 0x08,
                    0x70, 0x00, 0x73, 0x00, 0x00, 0x00, 0x93, 0x08, 0x20,
                    0x00, 0x73, 0x00, 0x00, 0x00, 0xef, 0xf0, 0x9f, 0xff,
                    0x2f, 0x69, 0x6e, 0x69, 0x74, 0x00, 0x00, 0x24, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void) {
    struct proc *p;

    p = allocproc();
    initproc = p;

    // allocate one user page and copy init's instructions
    // and data into it.
    uvminit(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    // prepare for the very first "return" from kernel to user.
    p->trapframe->epc = 0;     // user program counter
    p->trapframe->sp = PGSIZE; // user stack pointer

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;

    release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n) {
    uint sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0) {
        if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
            return -1;
        }
    }
    else if (n < 0) {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// allocates page table pages and physical pages
// deallocates all physical pages when out of memory (but not the page table
// pages)
// the vma_offset passed in must point to an non-null vma_ptr slot
// return 0 if failed
static int
_vma_copy_one(struct proc *parent, struct proc *child, int vma_offset) {
    pagetable_t parent_pagetable = parent->pagetable;
    pagetable_t child_pagetable = child->pagetable;
    struct vma *vma_ptr =
        parent->mapped_vma_ranges[vma_offset]; // guaranteed to be non-null
    uint64 aligned_page_start_va = PGROUNDDOWN(vma_ptr->range_start_va);
    uint64 current_mmap_range_end_va =
        vma_ptr->range_start_va + vma_ptr->range_size;
    uint64 aligned_page_end_va = PGROUNDUP(current_mmap_range_end_va);

    struct vma *allocated_vma_ptr = _allocate_vma_from_global_pool();

    // out of memory
    if (!allocated_vma_ptr) {
        return -1;
    }

    // update the max range of page table pages first in case of OOM
    if (child->max_mmap_range_end_va < current_mmap_range_end_va) {
        child->max_mmap_range_end_va = current_mmap_range_end_va;
    }

    // out of memory
    if (!copy_page_table_in_range_selectively(
            parent_pagetable,
            child_pagetable,
            aligned_page_start_va,
            aligned_page_end_va
        )) {
        deallocate_physical_pages_in_range_selectively(
            child_pagetable, aligned_page_start_va, aligned_page_end_va
        );

        _deallocate_vma_to_global_pool(allocated_vma_ptr);

        return 0;
    }

    filedup(vma_ptr->backed_file_ptr);

    allocated_vma_ptr->range_start_va = vma_ptr->range_start_va;
    allocated_vma_ptr->range_size = vma_ptr->range_size;
    allocated_vma_ptr->backed_file_ptr = vma_ptr->backed_file_ptr;
    allocated_vma_ptr->backed_file_offset = vma_ptr->backed_file_offset;
    allocated_vma_ptr->need_write_back = vma_ptr->need_write_back;
    allocated_vma_ptr->readable = vma_ptr->readable;
    allocated_vma_ptr->writable = vma_ptr->writable;

    child->mapped_vma_ranges[vma_offset] = allocated_vma_ptr;

    return 1;
}

// return 0 if failed
static int _vma_copy(struct proc *parent, struct proc *child) {
    for (int vma_offset = 0; vma_offset < NVMA; vma_offset++) {
        if (!parent->mapped_vma_ranges[vma_offset]) {
            continue;
        }

        if (!_vma_copy_one(parent, child, vma_offset)) {
            _munmap_all(child);

            return 0;
        }
    }

    return 1;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void) {
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    if (!_vma_copy(p, np)) {
        freeproc(np);
        release(&np->lock);
        return -1;
    }

    np->parent = p;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    np->state = RUNNABLE;

    release(&np->lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold p->lock.
void reparent(struct proc *p) {
    struct proc *pp;

    for (pp = proc; pp < &proc[NPROC]; pp++) {
        // this code uses pp->parent without holding pp->lock.
        // acquiring the lock first could cause a deadlock
        // if pp or a child of pp were also in exit()
        // and about to try to lock p.
        if (pp->parent == p) {
            // pp->parent can't change between the check and the acquire()
            // because only the parent changes it, and we're the parent.
            acquire(&pp->lock);
            pp->parent = initproc;
            // we should wake up init here, but that would require
            // initproc->lock, which would be a deadlock, since we hold
            // the lock on one of init's children (pp). this is why
            // exit() always wakes init (before acquiring any locks).
            release(&pp->lock);
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status) {
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    _munmap_all(p);
    end_op();
    p->cwd = 0;

    // we might re-parent a child to init. we can't be precise about
    // waking up init, since we can't acquire its lock once we've
    // acquired any other proc lock. so wake up init whether that's
    // necessary or not. init may miss this wakeup, but that seems
    // harmless.
    acquire(&initproc->lock);
    wakeup1(initproc);
    release(&initproc->lock);

    // grab a copy of p->parent, to ensure that we unlock the same
    // parent we locked. in case our parent gives us away to init while
    // we're waiting for the parent lock. we may then race with an
    // exiting parent, but the result will be a harmless spurious wakeup
    // to a dead or wrong process; proc structs are never re-allocated
    // as anything else.
    acquire(&p->lock);
    struct proc *original_parent = p->parent;
    release(&p->lock);

    // we need the parent's lock in order to wake it up from wait().
    // the parent-then-child rule says we have to lock it first.
    acquire(&original_parent->lock);

    acquire(&p->lock);

    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup1(original_parent);

    p->xstate = status;
    p->state = ZOMBIE;

    release(&original_parent->lock);

    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr) {
    struct proc *np;
    int havekids, pid;
    struct proc *p = myproc();

    // hold p->lock for the whole time to avoid lost
    // wakeups from a child's exit().
    acquire(&p->lock);

    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (np = proc; np < &proc[NPROC]; np++) {
            // this code uses np->parent without holding np->lock.
            // acquiring the lock first would cause a deadlock,
            // since np might be an ancestor, and we already hold p->lock.
            if (np->parent == p) {
                // np->parent can't change between the check and the acquire()
                // because only the parent changes it, and we're the parent.
                acquire(&np->lock);
                havekids = 1;
                if (np->state == ZOMBIE) {
                    // Found one.
                    pid = np->pid;
                    if (addr != 0
                        && copyout(
                               p->pagetable,
                               addr,
                               (char *)&np->xstate,
                               sizeof(np->xstate)
                           ) < 0) {
                        release(&np->lock);
                        release(&p->lock);
                        return -1;
                    }
                    freeproc(np);
                    release(&np->lock);
                    release(&p->lock);
                    return pid;
                }
                release(&np->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || p->killed) {
            release(&p->lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &p->lock); // DOC: wait-sleep
    }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void) {
    struct proc *p;
    struct cpu *c = mycpu();

    c->proc = 0;
    for (;;) {
        // Avoid deadlock by ensuring that devices can interrupt.
        intr_on();

        int nproc = 0;
        for (p = proc; p < &proc[NPROC]; p++) {
            acquire(&p->lock);
            if (p->state != UNUSED) {
                nproc++;
            }
            if (p->state == RUNNABLE) {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->context);

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
            }
            release(&p->lock);
        }
        if (nproc <= 2) { // only init and sh exist
            intr_on();
            asm volatile("wfi");
        }
    }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
    int intena;
    struct proc *p = myproc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void) {
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void) {
    static int first = 1;

    // Still holding p->lock from scheduler.
    release(&myproc()->lock);

    if (first) {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        first = 0;
        fsinit(ROOTDEV);
    }

    usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk) {
    struct proc *p = myproc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.
    if (lk != &p->lock) {  // DOC: sleeplock0
        acquire(&p->lock); // DOC: sleeplock1
        release(lk);
    }

    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    if (lk != &p->lock) {
        release(&p->lock);
        acquire(lk);
    }
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == SLEEPING && p->chan == chan) {
            p->state = RUNNABLE;
        }
        release(&p->lock);
    }
}

// Wake up p if it is sleeping in wait(); used by exit().
// Caller must hold p->lock.
static void wakeup1(struct proc *p) {
    if (!holding(&p->lock))
        panic("wakeup1");
    if (p->chan == p && p->state == SLEEPING) {
        p->state = RUNNABLE;
    }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->pid == pid) {
            p->killed = 1;
            if (p->state == SLEEPING) {
                // Wake process from sleep().
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
    struct proc *p = myproc();
    if (user_dst) {
        return copyout(p->pagetable, dst, src, len);
    }
    else {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
    struct proc *p = myproc();
    if (user_src) {
        return copyin(p->pagetable, dst, src, len);
    }
    else {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
    static char *states[] = {
        [UNUSED] "unused",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING] "run   ",
        [ZOMBIE] "zombie"};
    struct proc *p;
    char *state;

    printf("\n");
    for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s", p->pid, state, p->name);
        printf("\n");
    }
}

// the returned address is guaranteed to be page-aligned
// return 0 if failed
static uint64
_get_next_vma_range_start_va(struct proc *current_process_ptr, uint64 length) {
    uint64 highest_vma_range_end_va = PGROUNDUP(MMAPBASE);

    for (int vma_offset = 0; vma_offset < NVMA; vma_offset++) {
        struct vma *vma_ptr =
            current_process_ptr->mapped_vma_ranges[vma_offset];

        if (!vma_ptr) {
            continue;
        }

        uint64 current_vma_range_end_va =
            PGROUNDUP(vma_ptr->range_start_va + vma_ptr->range_size);

        highest_vma_range_end_va =
            highest_vma_range_end_va < current_vma_range_end_va
                ? current_vma_range_end_va
                : highest_vma_range_end_va;
    }

    return length > TRAPFRAME - highest_vma_range_end_va
               ? 0
               : highest_vma_range_end_va;
}

// return 0 if failed
static int
_check_file_permission(struct file *backed_file_ptr, int prot, int flags) {
    if ((prot & PROT_READ) && !is_readable(backed_file_ptr)) {
        return 0;
    }

    if ((prot & PROT_WRITE)
        && !(is_writable(backed_file_ptr) || (flags & MAP_PRIVATE))) {
        return 0;
    }

    return 1;
}

uint64 mmap(int length, int prot, int flags, int fd) {
    struct proc *current_process_ptr = myproc();

    uint64 next_vma_range_start_va =
        _get_next_vma_range_start_va(current_process_ptr, length);

    // out of memory
    if (!next_vma_range_start_va) {
        // printf("[mmap] out of memory: running out mmap region\n");

        return -1;
    }

    int current_process_vma_offset = -1;

    for (int i = 0; i < NVMA; i++) {
        if (current_process_ptr->mapped_vma_ranges[i] == 0) {
            current_process_vma_offset = i;

            break;
        }
    }

    // out of memory
    if (current_process_vma_offset == -1) {
        // printf("[mmap] out of memory: running out current process's vma
        // slot\n"
        // );

        return -1;
    }

    struct file *backed_file_ptr = current_process_ptr->ofile[fd];

    if (!backed_file_ptr || !is_normal_file(backed_file_ptr)
        || !_check_file_permission(backed_file_ptr, prot, flags)) {

        // printf("[mmap] invalid file or permission\n");

        return -1;
    }

    struct vma *allocated_vma_ptr = _allocate_vma_from_global_pool();

    // out of memory
    if (!allocated_vma_ptr) {
        // printf("[mmap] out of memory: running out global vma pool\n");

        return -1;
    }

    filedup(backed_file_ptr);

    allocated_vma_ptr->range_start_va = next_vma_range_start_va;
    allocated_vma_ptr->range_size = length;
    allocated_vma_ptr->backed_file_ptr = backed_file_ptr;
    allocated_vma_ptr->backed_file_offset = 0;
    allocated_vma_ptr->need_write_back = flags & MAP_SHARED;
    allocated_vma_ptr->readable = prot & PROT_READ;
    allocated_vma_ptr->writable = prot & PROT_WRITE;

    current_process_ptr->mapped_vma_ranges[current_process_vma_offset] =
        allocated_vma_ptr;

    uint64 current_range_end_va = next_vma_range_start_va + length;

    if (current_process_ptr->max_mmap_range_end_va < current_range_end_va) {
        current_process_ptr->max_mmap_range_end_va = current_range_end_va;
    }

    return next_vma_range_start_va;
}

// return -1 if failed
static int _check_if_is_valid_addr_and_get_vma_offset(
    struct proc *current_process_ptr, uint64 addr, int length
) {
    uint64 target_range_start_va = addr;
    uint64 target_range_end_va = target_range_start_va + length;

    // printf(
    //     "[_check_if_is_valid_addr_and_get_vma_offset] target_range_start_va:
    //     "
    //     "%p\n",
    //     target_range_start_va
    // );
    // printf(
    //     "[_check_if_is_valid_addr_and_get_vma_offset] target_range_end_va: "
    //     "%p\n",
    //     target_range_end_va
    // );

    for (int vma_offset = 0; vma_offset < NVMA; vma_offset++) {
        struct vma *vma_ptr =
            current_process_ptr->mapped_vma_ranges[vma_offset];

        if (!vma_ptr) {
            continue;
        }

        uint64 vma_range_start_va = vma_ptr->range_start_va;
        uint64 vma_range_end_va = vma_range_start_va + vma_ptr->range_size;

        // printf(
        //     "[_check_if_is_valid_addr_and_get_vma_offset] vma_offset: %d\n",
        //     vma_offset
        // );
        // printf(
        //     "[_check_if_is_valid_addr_and_get_vma_offset] vma_ptr: %p\n",
        //     (uint64)vma_ptr
        // );
        // printf(
        //     "[_check_if_is_valid_addr_and_get_vma_offset] vma_range_start_va:
        //     "
        //     "%p\n",
        //     vma_range_start_va
        // );
        // printf(
        //     "[_check_if_is_valid_addr_and_get_vma_offset] vma_range_end_va: "
        //     "%p\n",
        //     vma_range_end_va
        // );

        // no overlap
        if (target_range_end_va <= vma_range_start_va
            || vma_range_end_va <= target_range_start_va) {
            // printf("[_check_if_is_valid_addr_and_get_vma_offset] no
            // overlap\n");

            continue;
        }

        // overlap but out of bound
        else if (target_range_start_va < vma_range_start_va || target_range_end_va > vma_range_end_va) {
            // printf("[_check_if_is_valid_addr_and_get_vma_offset] overlap but
            // "
            //    "out of bound\n");

            return -1;
        }

        // inside the vma but punch a hole of it
        else if (target_range_start_va > vma_range_start_va && target_range_end_va < vma_range_end_va) {
            // printf("[_check_if_is_valid_addr_and_get_vma_offset] inside the "
            //    "vma but punch a hole of it\n");

            return -1;
        }

        else {
            // printf("[_check_if_is_valid_addr_and_get_vma_offset] success\n");

            return vma_offset;
        }
    }

    return -1;
}

static void _write_back_to_file(struct vma *vma_ptr, uint64 addr, int length) {
    struct inode *inode_ptr = get_inode(vma_ptr->backed_file_ptr);

    begin_op();

    ilock(inode_ptr);

    uint off = vma_ptr->backed_file_offset + (addr - vma_ptr->range_start_va);

    writei(inode_ptr, 1, addr, off, length);

    iunlock(inode_ptr);

    end_op();
}

static void _write_back_to_file_and_unmap_physical_pages(
    struct proc *current_process_ptr,
    int vma_offset,
    struct vma *vma_ptr,
    uint64 addr,
    int length
) {
    if (vma_ptr->writable && vma_ptr->need_write_back) {
        _write_back_to_file(vma_ptr, addr, length);
    }

    pagetable_t pagetable = current_process_ptr->pagetable;
    uint64 aligned_page_start_va;
    uint64 aligned_page_end_va;

    if (addr == vma_ptr->range_start_va) {
        aligned_page_start_va = PGROUNDDOWN(addr);

        if (length == vma_ptr->range_size) {
            aligned_page_end_va = PGROUNDUP(addr + length);

            vma_ptr->backed_file_offset = -1;
            vma_ptr->range_start_va = -1;
            vma_ptr->range_size = 0;
        }

        else {
            aligned_page_end_va = PGROUNDDOWN(addr + length);

            vma_ptr->backed_file_offset += length;
            vma_ptr->range_start_va += length;
            vma_ptr->range_size -= length;
        }
    }

    else if (addr + length == vma_ptr->range_start_va + vma_ptr->range_size) {
        aligned_page_start_va = PGROUNDUP(addr);
        aligned_page_end_va = PGROUNDUP(addr + length);

        vma_ptr->range_size -= length;
    }

    // don't need to implement in this lab
    else {
        panic("never reaches here");
    }

    deallocate_physical_pages_in_range_selectively(
        pagetable, aligned_page_start_va, aligned_page_end_va
    );

    if (vma_ptr->range_size == 0) {
        fileclose(vma_ptr->backed_file_ptr);

        _deallocate_vma_to_global_pool(vma_ptr);

        current_process_ptr->mapped_vma_ranges[vma_offset] = 0;
    }
}

// unmap only the physical pages inside the given range (while the page table
// pages still being allocated), and write back all modified blocks to the
// backed file
uint64 munmap(uint64 addr, int length) {
    struct proc *current_process_ptr = myproc();

    int vma_offset = _check_if_is_valid_addr_and_get_vma_offset(
        current_process_ptr, addr, length
    );

    if (vma_offset == -1) {
        // printf("[munmap] invalid addr\n");

        return -1;
    }

    // guaranteed to be non-null since the returned vma_offset is non-null
    struct vma *vma_ptr = current_process_ptr->mapped_vma_ranges[vma_offset];

    _write_back_to_file_and_unmap_physical_pages(
        current_process_ptr, vma_offset, vma_ptr, addr, length
    );

    return 0;
}

static void _free_mmap_page_table_pages(struct proc *current_process_ptr) {
    pagetable_t pagetable = current_process_ptr->pagetable;
    uint64 aligned_page_start_va = MMAPBASE;
    uint64 aligned_page_end_va =
        PGROUNDUP(current_process_ptr->max_mmap_range_end_va);

    deallocate_pagetable_pages_in_range_selectively(
        pagetable, aligned_page_start_va, aligned_page_end_va
    );
}

// unmap all the physical pages and page table pages in the entire mmap region
// starting at MMAPBASE, and write back all modified blocks to the backed file
static void _munmap_all(struct proc *current_process_ptr) {
    for (int vma_offset = 0; vma_offset < NVMA; vma_offset++) {
        struct vma *vma_ptr =
            current_process_ptr->mapped_vma_ranges[vma_offset];

        if (!vma_ptr) {
            continue;
        }

        uint64 addr = vma_ptr->range_start_va;
        int length = (int)vma_ptr->range_size;

        _write_back_to_file_and_unmap_physical_pages(
            current_process_ptr, vma_offset, vma_ptr, addr, length
        );
    }

    _free_mmap_page_table_pages(current_process_ptr);
}
