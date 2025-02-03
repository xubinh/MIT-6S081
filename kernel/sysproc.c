#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
    int n;
    if (argint(0, &n) < 0)
        return -1;
    exit(n);
    return 0; // not reached
}

uint64 sys_getpid(void) {
    return myproc()->pid;
}

uint64 sys_fork(void) {
    return fork();
}

uint64 sys_wait(void) {
    uint64 p;
    if (argaddr(0, &p) < 0)
        return -1;
    return wait(p);
}

uint64 sys_sbrk(void) {
    struct proc *p = myproc();
    uint64 old_sz = p->sz;
    int n;
    uint64 abs_n;

    if (argint(0, &n) < 0) {
        return -1;
    }

    int is_negative = n < 0;

    abs_n = (is_negative ? -n : n);

    // printf("[xbhuang] sbrk\n");
    // printf("[xbhuang] old size: %p\n", old_sz);
    // printf("[xbhuang] n: %d\n", n);

    if ((is_negative && old_sz < abs_n)
        || (!is_negative && n > TRAPFRAME - old_sz)) {
        // printf("[xbhuang] failed\n");

        return -1;
    }

    if (is_negative) {
        uvmdealloc(p->pagetable, old_sz, old_sz - abs_n);
        p->sz -= abs_n;
    }

    else {
        p->sz += abs_n;
    }

    // printf("[xbhuang] new size: %p\n", p->sz);
    // printf("[xbhuang] sbrk OK\n");

    return old_sz;
}

uint64 sys_sleep(void) {
    int n;
    uint ticks0;

    if (argint(0, &n) < 0)
        return -1;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (myproc()->killed) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64 sys_kill(void) {
    int pid;

    if (argint(0, &pid) < 0)
        return -1;
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}
