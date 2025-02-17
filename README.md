# traps

## RISC-V 中的函数调用规范

- 函数的第一个参数由寄存器 `a0` 给出, 第二个参数由 `a1` 给出, 以此类推.
- caller 在调用 callee 时首先按倒序准备好 callee 的参数, 然后执行 `auipc ra,0x0` 和 `jalr xxx(ra)` 来跳转至 callee. 其中 `auipc` 指的是 "Add Upper Immediate to PC", 具体操作为 `ra = PC + (imm << 12)`, 由于此时立即数 `imm` 等于零, 因此实际效果相当于获取当前 PC 值并存储至 `ra` 中. `jalr xxx(ra)` 等价于 `jalr ra, ra, xxx`, 具体操作为 `PC (new) = ra + xxx`, `ra = PC (old) + 4`.
- 进入 callee 之后需要做的工作:
  - 保存 `ra` 至固定位置;
  - 保存 `s0` 至固定位置;
  - 保存其他 callee-save 寄存器 `s1`-`s11` (可选);
  - 递减 `sp` 和 `s0` 以创建栈帧.
  
  此时:
  
  - `sp` 为 callee 的栈底, `s0` 为 callee 的栈顶, `sp` 至 `s0` 为 callee 的栈帧.
  - 新的栈帧和旧的栈帧之间并不一定需要紧邻着, 两者之间有可能需要腾出额外的空间用于存储一些其他变量.
  - 但不论如何, 递减后的 callee 的 `s0` 总是满足 `s0 - 8` 指向 caller 的 `ra` 的保存位置, 而 `s0 - 16` 指向 caller 的 `s0` 的保存位置. 这也是为什么称二者被保存在 "固定位置" 的原因.

- 当 callee 执行完毕时, 依次执行相反的操作, 即递增 `sp` (令其等于 `s0`), 恢复 `s0`, 取出返回地址至 `ra`, 然后 `ret`.
- 当执行一个机器指令时, PC 的值就是当前所执行的指令的地址. 只有当指令执行完毕之后才会递增至下一条指令.

## RISC-V assembly (easy)

(略)

## Backtrace (moderate)

思路:

- 进入 `backtrace` 函数之后, `s0` 指向 `backtrace` 自己的栈顶, 而调用者的栈顶位于 `*(s0 - 16)`, `backtrace` 的返回地址位于 `*(s0 - 8)`. 因此根据 callee 的栈顶指针 `s0_current` 我们可以获得 callee 的返回地址 `ra` (即 caller 中紧接着 `jalr` 的下一条指令的地址) 以及 caller 的栈顶指针 `s0_previous`, 不断重复这一过程直到 `s0_current` 超出合法区域为止.

## Alarm (hard)

思路:

- [x] 将测试文件 `user/alarmtest.c` 添加进 Makefile 中.
- [x] 向文件 `user/user.h` 中添加声明:

  ```c
  int sigalarm(int ticks, void (*handler)());
  int sigreturn(void);
  ```

- [x] 向文件 `user/usys.pl`, `kernel/syscall.h`, 以及 `kernel/syscall.c` 中添加声明.
- [x] 在 `struct proc` 结构体中添加如下几个字段:

  ```c
  int total_number_of_ticks_for_timer_handler;
  int current_number_of_ticks_for_timer_handler_left;
  void (*timer_handler)();
  struct trapframe *trapframe_backup;
  ```

- [x] 在文件 `kernel/sysproc.c` 和 `kernel/trap.c` 中进行实现:
  - `sys_sigalarm` 系统调用需要做的工作很简单, 就是将定时区间 `interval` 和定时器处理函数入口 `handler` 存储到 `struct proc` 中.
  - 真正处理定时器的是 `usertrap` 函数, 大致过程如下:
    - 在用户空间函数执行的过程中定时器触发中断, 进入机器模式并跳转至 `timervec`.
    - `timervec` 触发一次软件中断, 然后跳转回用户空间.
    - 软件中断导致执行流又一次进入内核模式, 并跳转至 `uservec` 汇编代码.
    - `uservec` 汇编代码保存执行流的上下文至陷入栈帧中, 然后跳转至 `usertrap` 函数.
    - `usertrap` 函数检测到本次是一次由定时器中断导致的软件中断 (`if (which_dev == 2)`), 因此我们需要检查 `p->current_number_of_ticks_for_timer_handler_left` (以下简称 "倒计时")：
      - 如果倒计时已经为零, 那么什么也不做——这要么是因为用户根本就没有设置定时器, 要么是因为定时器已经被触发, 而程序就是在执行定时器处理函数的时候再次遇到定时器中断的. 对于后者, 由于定时器处理函数是不可重入的, 因此我们选择忽略它.
      - 如果倒计时不为零, 那么我们递减它:
        - 如果倒计时在递减之后仍然大于零, 那么我们什么也不做.
        - 如果倒计时在递减之后降为零, **那么我们将调用一次定时器处理函数**.
  - 由于调用定时器处理函数要求我们从内核空间跳转至用户空间, 因此我们需要修改的并不是 `ra` 寄存器, 而是 `sepc` (S Exception Program Counter) 寄存器.
  - 由于跳转到定时器处理函数的这一操作并不是常规的函数调用, 而是一次上下文切换, 因此我们需要将原函数的上下文从 `p->trapframe` 中取出并保存到暂存地点 `p->trapframe_backup`, 待定时器处理函数执行完毕并调用 `sigreturn` 时恢复.
  - 我们还需要在 `allocproc` 函数中对 `struct proc` 结构体中新增的字段进行初始化并分配资源, 并在 `freeproc` 中对资源进行释放并对字段进行清空.
