# thread

## Uthread: switching between threads (moderate)

思路:

- 首先需要一个保存当前线程的上下文的数据结构, 直接在 `struct thread` 中添加一个和 `kernel/proc.h` 中所定义的 `struct context` 的定义相同的结构体成员即可.
- `thread_schedule` 函数负责从全局数组 `all_thread` 中挑出下一个可以执行的线程然后将当前线程的上下文与该线程的进行切换. 上下文切换是通过调用 `thread_switch` 函数进行的. 通过调用 `thread_switch`, `sp` 会自动递减, `ra` 也会自动生成, 因此 `thread_schedule` 完全可以将上下文切换的任务全权交给 `thread_switch` 来完成, 整个切换过程对于 `thread_schedule` 而言是透明的.
- `thread_switch` 函数负责具体的上下文切换工作, 类似于 `kernel/swtch.S` 中所定义的 `swtch` 函数. 由于 caller-save 寄存器在调用 `thread_switch` 函数之前已经由编译器压入当前线程的栈中, 因此 `thread_switch` 只需要负责保存 callee-save 寄存器即可.
- 对于 `thread_create`, 我们需要使得新创建的线程从 `func` 函数开始执行, 这可以通过手动设置其 `struct context` 上下文结构体来做到, 类似于 `kernel/proc.c` 中的 `allocproc` 函数所做的那样.

## Using threads (moderate)

(略)

## Barrier(moderate)

(略)
