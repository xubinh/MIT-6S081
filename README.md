# cow

## Implement copy-on write(hard)

大纲:

- [x] 对于 COW 页面, 内核在尝试**写入**该页面时将会由于 `PTE_W` 被置零而导致触发缺页异常. 我们可以在 `usertrap` 函数中检查本次陷入的原因是否为写缺页异常 (`scause` 寄存器的值为 15) 以及所尝试写入的页面是否为 COW 页面, 后者可以通过利用 PTE 的 "RSW (reserved for software)" 标志位来进行标记. RSW 位于 PTE 的第 8-9 位, 十进制下可取 0 (User), 1 (Supervisor), 2 (Reserved), 以及 3 (Machine).
- [x] 在确认本次陷入确实是由于尝试写入 COW 页面所导致的缺页异常之后, 我们需要分配新页面, 将 COW 页面的内容完整复制到新页面中, 然后递减 COW 页面的引用计数. 如果 COW 页面的引用计数已经为 1 (这可能发生在进程 A 复制了 COW 页面并递减了引用计数但进程 B 的页表仍未被更新的情况下, 此时页面的引用计数已经降为 1, 已成为普通页面, 但 B 仍然认为页面是 COW 的) 则并不分配页面, 而是直接将原有页面作为新页面. 我们可以通过在 `kernel/kalloc.c` 中定义新的函数来对引用计数进行同步:
  
  ```c
  int atomic_copy_and_decrement_cow_page_reference_count(uint64 cow_page_pa, uint64 *new_page_pa_ptr)
  ```
  
  - 由于引用计数的检查必须与递减操作放在一起原子性地进行, 而页面的分配依赖于检查, 因此检查, 页面分配, 以及递减这三个操作必须放在同一个函数中进行.
  - 如果内存不足以分配新页面则直接杀死子进程.
- [x] 为了实现物理页面的引用计数机制, 我们需要在 `kernel/kalloc.c` 中创建一个全局引用计数数组 (可以将其放置在 `kmem` 数据结构中):
  
  ```c
  int reference_counts_of_all_pages[PGROUNDUP(PHYSTOP) / PGSIZE];
  ```

  - 对每个物理页面使用其页号 (PPN) 来直接进行索引 (这可能造成一定的内存浪费, 更优的方法是使用相对页号).
  - 对引用计数数组的任何更改都需要确保在临界区域中进行. 这一点很容易做到, 直接利用和 `kalloc` 等函数中的相同的同步机制即可.
  - 每个物理页面的初始引用计数应为零.
- [x] 在 `fork` 函数中是通过调用 `uvmcopy` 函数来对父进程的页表进行复制的, 我们应该修改该函数使其不再立即分配物理页面, 而是将父进程的页面标记为 COW 页面并递增引用计数. 我们可以通过在 `kernel/kalloc.c` 中定义新的函数来对引用计数进行同步:
  
  ```c
  int atomic_increment_cow_page_reference_count(uint64 cow_page_pa);
  ```

- [x] 在 `kfree` 函数中我们同样要在对页面进行回收的同时维护其引用计数. 我们可以通过在 `kernel/kalloc.c` 中定义新的函数来对引用计数进行同步:
  
  ```c
  int atomic_decrement_cow_page_reference_count(uint64 cow_page_pa);
  ```

  可以看到一个页面是否为 COW 页面与其引用计数实际上是解耦的, 即使一个页面的引用计数为 1, 一个进程也可以将其标记为 COW 页面, 其代价不过是一次额外的缺页异常而已.
- [x] 同样地我们也应该修改 `kalloc` 函数将新分配的页面的引用计数初始化为 1.
- [x] 此外还需要修改 `freerange` 函数, 其中调用了 `kfree` 函数通过对页面进行释放来模拟对页面的初始化.
- [x] 由于像 `copyout` 这样的函数需要在内核空间中对用户的虚拟地址进行手动转换, 因此无法依赖陷入以及缺页异常, 而必须同样手动进行页面分配.
- [x] 除了修改代码逻辑以外, 由于我们手动接管了虚拟地址转换 (原因是我们需要手动检查 PTE 中的标志位), 因此在转换前我们必须对所要转换的虚拟地址进行必要的合法性检查.
