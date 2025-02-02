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
    int addr;
    int n;

    if (argint(0, &n) < 0)
        return -1;
    addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

uint64 sys_sleep(void) {
    int n;
    uint ticks0;

    backtrace();

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

uint64 sys_sigalarm(void) {
    struct proc *p = myproc();
    int total_number_of_ticks_for_timer_handler;
    void (*timer_handler)();
    
    if (argint(0, &total_number_of_ticks_for_timer_handler) < 0){
        return -1;
    }

    if(total_number_of_ticks_for_timer_handler < 0) {
        return -1;
    }
    
    if (argaddr(1, (uint64 *)&timer_handler) < 0){
        return -1;
    }

    if(p->trapframe_backup == 0) {
        p->trapframe_backup = (struct trapframe *)kalloc();

        if(p->trapframe_backup == 0) {
            return -1;
        }
    }

    p->total_number_of_ticks_for_timer_handler = total_number_of_ticks_for_timer_handler;
    p->current_number_of_ticks_for_timer_handler_left = total_number_of_ticks_for_timer_handler;
    p->timer_handler = timer_handler;

    return 0;
}

uint64 sys_sigreturn(void) {
    struct proc *p = myproc();

    p->current_number_of_ticks_for_timer_handler_left = p->total_number_of_ticks_for_timer_handler;
    *p->trapframe = *p->trapframe_backup;

    return 0;
}
