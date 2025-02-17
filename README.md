# pgtbl

## 内核页表的初始化过程

- `main` 函数调用 `kvminit` 函数. `kvminit` 函数用于初始化全局内核页表 `kernel_pagetable`. 具体操作为分配根页表, 映射 I/O 设备, 映射内核代码, 映射内核数据和空闲物理页面, 最后映射蹦床代码页面. 然后 `main` 函数调用 `kvminithart` 函数将 `satp` 寄存器的值设置为全局内核页表 `kernel_pagetable` 的值并清空 TLB.

## 系统调用 `fork` 的执行过程

### 父进程从执行 `fork` 开始到分岔口前的执行过程

- 用户调用 `fork` 包装函数.
- `fork` 包装函数将 `sys_fork` 系统调用的编号 `SYS_fork` 载入寄存器 `a7`, 然后执行 `ecall` 机器指令, 触发一次陷入.
- `ecall` 指令所触发的陷入将使得程序进入内核模式并跳转至函数 `uservec`.
  - 当前用户进程在被 `fork` 创建之时通过调用 `allocproc` 函数强制执行了一次 `forkret` 函数, 而 `forkret` 函数调用了 `usertrapret` 函数来设置 `stvec` 寄存器的内容为 `uservec` 函数的入口地址. `stvec` 寄存器控制的正是进程陷入时需要跳转到的地址.
  - 即使是系统的首个进程 `/init` 也是通过调用 `allocproc` 函数以相同的方式设置 `stvec` 寄存器的内容为 `uservec` 函数的入口地址.
- `uservec` 函数负责执行一系列上下文切换工作, 然后跳转至函数 `usertrap`. 上下文切换工作具体包括:
  - 从 `sscratch` 寄存器中获取陷入栈帧在用户空间中的虚拟页面地址 (即 `TRAPFRAME` 宏) (注意此时 `satp` 仍然指向用户页表).
  - 将所有寄存器的当前值存入当前进程的陷入栈帧 `p->trapframe` 中.
  - 将栈顶指针 `sp` 切换为 `p->trapframe->kernel_sp` (其值为 `p->kstack + PGSIZE`, 在函数 `usertrapret` 中被设置).
  - 加载陷入处理函数的地址 `p->trapframe->kernel_trap`.
  - 切换 `satp` 寄存器的值为 `p->trapframe->kernel_satp` (其值为内核页表根页面的首地址, 在函数 `usertrapret` 中被设置).
  - 最后跳转至陷入处理函数, 即 `usertrap`.
- `usertrap` 首先将陷入处理函数更换为内核的版本的处理函数 `kernelvec`, 然后将 `sepc` 寄存器的值 (此即触发本次陷入的 `ecall` 指令的地址, 在 RISC-V 下该地址在陷入触发时被自动保存至 `sepc` 中) 存入 `p->trapframe->epc`, 最后对本次陷入进行路由. 由于本次陷入是系统调用, 因此 `usertrap` 将进入系统调用相关的分支并调用 `syscall` 函数.
- `syscall` 函数从进程的陷入栈帧中获取系统调用的编号 `SYS_fork`, 通过哈希表找到 `sys_fork` 系统调用的入口地址, 然后调用 `sys_fork` 系统调用.
- `sys_fork` 内部全盘转发至工作函数 `fork`.
- `fork` 工作函数首先调用 `allocproc` 函数初始化子进程 `np`. `allocproc` 函数的工作包括:
  - 从进程表 `proc[NPROC]` 中获取一个空闲的进程表项 `p`.
  - 调用 `kalloc` 函数为陷入栈帧 `p->trapframe` 分配一个物理页面.
  - 调用 `proc_pagetable` 函数. `proc_pagetable` 函数的工作包括:
    - 调用 `uvmcreate` 函数为 `p` 创建页表根页面.
    - 调用 `mappages` 函数映射蹦床页面 `trampoline`.
    - 调用 `mappages` 函数映射陷入栈帧 `p->trapframe`.
  - 将 `p` 的返回地址 `p->context.ra` 设置为 `forkret` 函数的入口地址.
  - 将 `p` 的栈顶指针 `p->context.sp` 设置为内核栈页面的尾后地址 `p->kstack + PGSIZE`.
    - 注: 进程的内核栈页面 `p->kstack` 早在 `procinit` 函数 (由 `main` 函数调用) 中就已经分配好物理页面了.
  
  执行了 `allocproc` 函数之后, `fork` 调用 `uvmcopy` 函数将父进程 `p` 的所有页表页面和所有物理页面原样复制给子进程 `np`, 将父进程的陷入栈帧 `p->trapframe` 中的内容原样复制给子进程的陷入栈帧 `np->trapframe`, 设置子进程的返回值 `np->trapframe->a0` 为 `0`, 复制文件描述符表, 设置 `np->state` 的调度状态为 `RUNNABLE`, 然后返回子进程的 PID.

### 父进程自分岔口之后的执行过程

- 父进程正常从 `fork` 返回子进程的 PID 给 `sys_fork`.
- `sys_fork` 将返回值交给 `syscall` 函数.
- `syscall` 函数将返回值存入陷入栈帧中的 `a0` 寄存器 `p->trapframe->a0` 作为此次系统调用的返回值, 然后返回至 `usertrap` 函数.
- `usertrap` 函数从 `syscall` 函数中返回后紧接着便会调用 `usertrapret` 函数. `usertrapret` 函数的工作包括:
  - 将 `stvec` 寄存器的值设置为 `uservec` 函数的入口地址.
  - 设置进程的内核页表 `p->trapframe->kernel_satp` 为 `satp` 寄存器的当前值 (即全局内核页表 `kernel_pagetable`).
  - 设置进程的内核栈顶指针 `p->trapframe->kernel_sp` 为 `p->kstack + PGSIZE`.
  - 设置进程的陷入处理函数 `p->trapframe->kernel_trap` 为 `usertrap` 函数的入口地址.
  - 恢复进程的 PC 为陷入前的值 `p->trapframe->epc`.
  - 恢复 `satp` 寄存器的值为陷入前的值 `p->pagetable`.
  - 跳转至函数 `userret`.
- `userret` 函数执行和 `uservec` 相反的操作, 即恢复进程的上下文为用户空间的上下文, 然后通过 `sret` 指令从内核模式中跳出至用户模式, 并跳转回 `ecall` 指令的下一条指令.
- 至此父进程的 `fork` 函数便执行完成.

### 子进程自分岔口之后的执行过程

- 调度器函数 `scheduler` 抓取到子进程的进程表项, 并调用 `swtch` 函数.
- `swtch` 函数加载子进程的执行流的上下文, 即返回地址 `p->context.ra` (此时即 `forkret` 函数的入口地址) 和栈顶指针 `p->context.sp` (此时即内核栈页面的尾后地址 `p->kstack + PGSIZE`), 然后返回至 `forkret` 函数.
- `forkret` 函数直接调用 `usertrapret` 函数. 此外如果当前为开机以来首次调用 `forkret` 函数 (一般是 `/init` 中调用 `fork` 和 `exec` 执行 `sh` 程序的时候), 那么还会对文件系统进行初始化 (通过调用 `fsinit` 函数, 其定义位于文件 `kernel/fs.c` 中).
- 之后的执行过程便与父进程自 `usertrapret` 函数之后的执行过程无异.
- 至此子进程的 `fork` 函数便执行完成.

## Print a page table (easy)

(略)

## A kernel page table per process (hard)

目标: 为每个进程独立维护一个全局内核页表的副本.

思路: 由于全局内核页表在创建之后就不再改动, 因此只需在进程创建之时顺便创建全局内核页表的副本即可.

- [x] 在 `struct proc` 结构体中添加进程独立的内核页表字段.
- [x] 实现 `kvminit` 函数的一个变体 `kvminit_per_process`, 用于为每个进程创建独立的内核进程页表.
- [x] 在函数 `allocproc` 中添加创建逻辑, 通过调用 `kvminit_per_process` 创建全局内核页表的副本.
- [x] 将 `procinit` 中的为全局内核页表建立各个进程的内核栈页面映射的逻辑抽取出来单独形成一个函数 `procinit_per_process`, 并在函数 `allocproc` 中进行调用.
- [x] 在 `freeproc` 函数中添加用于清空内核页表的逻辑. 需要注意的是此处只能是清空页表自身, 而绝对不能释放最底层的物理页面, 因为内核页表是共享的, 其所维护的映射与内存分配之间是解耦的, 两者并没有直接关系, 只有进程的用户空间页表才会在映射物理页面时伴随着物理内存的分配. 这也是为什么只有 `uvmalloc` 函数而没有 `kvmalloc` 函数的原因.
- [x] 在调度器函数 `scheduler` 中的 `swtch` 前后添加页表切换逻辑, 以便配合 `usertrapret` 函数正确设置新进程的内核页表. 记得调用 `sfence_vma()` 函数清空 TLB.

## Simplify copyin/copyinstr (hard)

目标: 在每个进程的内核页表中维护一个用户空间页表的镜像, 使其无需再在内核模式下进行任何多余的地址转换步骤.

思路:

- 实现一个函数 `void sync(pagetable_t user_pgtbl, pagetable_t kernel_pgtbl, uint64 old_sz, uint64 new_sz);`, 用于在初始化进程 (`userinit` 和 `fork`) 和更换进程映像 (`exec`) 时在内核页表中创建用户空间页表的镜像, 以及在修改用户空间页表时 (`sbrk`/`growproc`) 将所做的修改同步至内核页表中的镜像.
- 在实现 `exec` 函数的部分时需要仔细处理页表页面内存分配失败时的情况, 确保失败的 `exec` 调用不会对原进程产生任何影响.
- 注意需要对新进程的用户空间大小进行检查, 避免其超出内核空间的最低地址 (即 `PLIC` 寄存器的首地址) 而与内核空间发生重叠.
- 对于 `TRAPFRAME` (陷入栈帧) 和 `TRAMPOLINE` (蹦床页面) 这两个页面, 前者不需要在内核页表中维护映射 (因为 `p->trapframe` 中已经保存了物理地址), 而后者不论是内核页表还是用户页表都已经被映射, 因此同样不需要维护.

注意事项:

- 对于每一个进程, 在该进程的栈帧的低地址方向紧邻着哨兵页面, 其中哨兵页面**是分配了内存的**, 之所以能够起到保护作用是因为其 `PTE_U` 标志位被置为了零, 因而无法在用户模式下被访问. 但这并不对内核模式造成任何影响, 因此在建立镜像映射时完全可以将其映射至内核页表中.
- 在 `user/usertests.c` 中有一个名为 `sbrkmuch` 的测试将会调用 `sbrk` 分配 100 MB 的空间, 而 xv6 的内核空间最低可延伸至 `0x2000000L` (即 `CLINT` 寄存器的首地址), 与虚拟地址空间起始之间的距离为 32 MB, 不足以支撑其通过测试. 但实际上 `CLINT` 页面的映射完全是多余的, 因为 `CLINT` 页面唯一的作用是获取 CPU 时钟, 这一操作必然是在机器模式下进行的, 而机器模式下是直接使用物理地址进行访存的, MMU 此时处于禁用状态, 也就无需映射 `CLINT` 页面. **因此请务必将 `kvminit` 函数 (其定义位于文件 `kernel/vm.c` 中) 中映射 `CLINT` 页面的语句注释掉**.
