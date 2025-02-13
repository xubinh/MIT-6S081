#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void) {
    initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void) {
    w_stvec((uint64)kernelvec);
}

// return 0 if failed
static struct vma *_get_vma_ptr_of_process_by_request_va(
    struct proc *current_process_ptr, uint64 request_va
) {
    for (int vma_offset = 0; vma_offset < NVMA; vma_offset++) {
        struct vma *vma_ptr =
            current_process_ptr->mapped_vma_ranges[vma_offset];

        if (!vma_ptr) {
            continue;
        }

        uint64 vma_range_start_va = vma_ptr->range_start_va;
        uint64 vma_range_end_va = vma_range_start_va + vma_ptr->range_size;

        if (vma_range_start_va <= request_va && request_va < vma_range_end_va) {
            return vma_ptr;
        }
    }

    return 0;
}

// return 0 if failed
static int _check_file_permission(struct vma *vma_ptr, uint64 scause) {
    if (scause == 13) {
        return vma_ptr->readable;
    }

    else if (scause == 17) {
        return vma_ptr->writable;
    }

    else {
        return 0;
    }
}

// return 0 if failed
static int _load_page_from_file(
    struct proc *current_process_ptr, struct vma *vma_ptr, uint64 request_va
) {
    // printf("[_load_page_from_file] enter\n");

    if (!vma_ptr) {
        panic("_load_page_from_file: never reaches here");
    }

    uint64 new_page_pa = (uint64)kalloc();

    // out of memory
    if (new_page_pa == 0) {
        // printf("[_load_page_from_file] OOM\n");

        return 0;
    }

    int perm = PTE_U;

    if (vma_ptr->readable) {
        perm |= PTE_R;
    }

    if (vma_ptr->writable) {
        perm |= PTE_W;
    }

    uint64 page_start_va = PGROUNDDOWN(request_va);

    if (mappages(
            current_process_ptr->pagetable,
            page_start_va,
            PGSIZE,
            new_page_pa,
            perm
        )
        != 0) {

        kfree((void *)new_page_pa);

        // printf("[_load_page_from_file] failed to map page\n");

        return 0;
    }

    struct inode *inode_ptr = get_inode(vma_ptr->backed_file_ptr);

    uint64 dst = page_start_va >= vma_ptr->range_start_va
                     ? page_start_va
                     : vma_ptr->range_start_va;

    uint off = vma_ptr->backed_file_offset + (dst - vma_ptr->range_start_va);

    uint64 end = (page_start_va + PGSIZE
                  <= vma_ptr->range_start_va + vma_ptr->range_size)
                     ? (page_start_va + PGSIZE)
                     : (vma_ptr->range_start_va + vma_ptr->range_size);

    uint n = end - dst;

    printf(
        "[_load_page_from_file] inode_ptr: %p, dst: %p, off: %d, n: %d\n",
        (uint64)inode_ptr,
        dst,
        off,
        n
    );

    begin_op();

    ilock(inode_ptr);

    n = readi(inode_ptr, 1, dst, off, n);

    iunlock(inode_ptr);

    end_op();

    if (n == (uint)-1) {
        // deallocate_physical_pages_in_range_selectively(
        //     current_process_ptr->pagetable,
        //     page_start_va,
        //     page_start_va + PGSIZE
        // );

        // printf("[_load_page_from_file] failed to read from file\n");

        // return 0;

        panic("[_load_page_from_file] never reaches here");
    }

    end = dst + n;

    if (dst > page_start_va) {
        memset((char *)new_page_pa, 0, dst - page_start_va);
    }

    if (page_start_va + PGSIZE > end) {
        memset(
            (char *)(new_page_pa + (end - page_start_va)),
            0,
            (page_start_va + PGSIZE) - end
        );
    }

    return 1;
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void) {
    int which_dev = 0;

    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    w_stvec((uint64)kernelvec);

    struct proc *p = myproc();

    // save user program counter.
    p->trapframe->epc = r_sepc();

    if (r_scause() == 8) {
        // system call

        if (p->killed)
            exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trapframe->epc += 4;

        // an interrupt will change sstatus &c registers,
        // so don't enable until done with those registers.
        intr_on();

        syscall();
    }

    // load/store page fault
    else if (r_scause() == 13 || r_scause() == 15) {
        // printf("[xbhuang] usertrap\n");

        uint64 request_va = r_stval();

        // printf("[xbhuang] p->pid: %d\n", p->pid);
        // printf("[xbhuang] p->sz: %p\n", (uint64)p->sz);
        // printf("[xbhuang] p->trapframe->sp: %p\n", p->trapframe->sp);
        // printf("[xbhuang] request_va: %p\n", request_va);

        struct vma *vma_ptr =
            _get_vma_ptr_of_process_by_request_va(p, request_va);

        // out of bound or permission denied
        if (!vma_ptr || !_check_file_permission(vma_ptr, r_scause())) {
            p->killed = 1;
        }

        else if (!_load_page_from_file(p, vma_ptr, request_va)) {
            // printf("[usertrap] killed process, PID: %d\n", myproc()->pid);

            p->killed = 1;
        }
    }

    else if ((which_dev = devintr()) != 0) {
        // ok
    }

    else {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
    }

    if (p->killed)
        exit(-1);

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2)
        yield();

    usertrapret();
}

//
// return to user space
//
void usertrapret(void) {
    struct proc *p = myproc();

    // we're about to switch the destination of traps from
    // kerneltrap() to usertrap(), so turn off interrupts until
    // we're back in user space, where usertrap() is correct.
    intr_off();

    // send syscalls, interrupts, and exceptions to trampoline.S
    w_stvec(TRAMPOLINE + (uservec - trampoline));

    // set up trapframe values that uservec will need when
    // the process next re-enters the kernel.
    p->trapframe->kernel_satp = r_satp();         // kernel page table
    p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
    p->trapframe->kernel_trap = (uint64)usertrap;
    p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->trapframe->epc);

    // tell trampoline.S the user page table to switch to.
    uint64 satp = MAKE_SATP(p->pagetable);

    // jump to trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64 fn = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap() {
    int which_dev = 0;
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0)
        panic("kerneltrap: interrupts enabled");

    if ((which_dev = devintr()) == 0) {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
        yield();

    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    w_sepc(sepc);
    w_sstatus(sstatus);
}

void clockintr() {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr() {
    uint64 scause = r_scause();

    if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9) {
        // this is a supervisor external interrupt, via PLIC.

        // irq indicates which device interrupted.
        int irq = plic_claim();

        if (irq == UART0_IRQ) {
            uartintr();
        }
        else if (irq == VIRTIO0_IRQ) {
            virtio_disk_intr();
        }
        else if (irq) {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if (irq)
            plic_complete(irq);

        return 1;
    }
    else if (scause == 0x8000000000000001L) {
        // software interrupt from a machine-mode timer interrupt,
        // forwarded by timervec in kernelvec.S.

        if (cpuid() == 0) {
            clockintr();
        }

        // acknowledge the software interrupt by clearing
        // the SSIP bit in sip.
        w_sip(r_sip() & ~2);

        return 2;
    }
    else {
        return 0;
    }
}
