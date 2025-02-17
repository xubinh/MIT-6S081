# <div align="center">Notes for xv6/MIT 6.S081 Labs</div>

> [!IMPORTANT]
>
> - 根据实验的设计形式, 本项目使用独立的分支分别展现各个实验的结果, 具体请移步至对应分支查看:
>   - [Lab Utilities](https://github.com/xubinh/MIT-6S081/tree/util) ✔️
>   - [Lab System calls](https://github.com/xubinh/MIT-6S081/tree/syscall) ✔️
>   - [Lab Page tables](https://github.com/xubinh/MIT-6S081/tree/pgtbl) ✔️
>   - [Lab Traps](https://github.com/xubinh/MIT-6S081/tree/traps) ✔️
>   - [Lab Lazy allocation](https://github.com/xubinh/MIT-6S081/tree/lazy) ✔️
>   - [Lab Copy on-write](https://github.com/xubinh/MIT-6S081/tree/cow) ✔️
>   - [Lab Multithreading](https://github.com/xubinh/MIT-6S081/tree/thread) ✔️
>   - [Lab Lock](https://github.com/xubinh/MIT-6S081/tree/lock) ✔️
>   - [Lab File system](https://github.com/xubinh/MIT-6S081/tree/fs) ✔️
>   - [Lab mmap](https://github.com/xubinh/MIT-6S081/tree/mmap) ✔️
>   - Lab network driver [TODO]
> - xv6 教材笔记与源码解析敬请移步至[我的博客](https://xubinh.github.io/tags/xv6/).

本分支主要介绍实验前的一些准备工作, 其中的内容均摘自 (或翻译自) 以下官方网站中的内容:

- <https://pdos.csail.mit.edu/6.1810/2020/tools.html>
- <https://pdos.csail.mit.edu/6.1810/2020/labs/guidance.html>

## <a id="toc"></a>目录

<details open="open"><summary><a href="#1">工具</a></summary>

- <a href="#1.1">实验所需环境</a>
- <a href="#1.2">环境搭建</a>
  - <a href="#1.2.1">Ubuntu 下</a>
    - <a href="#1.2.1.1">qemu-system-misc fix</a>
  - <a href="#1.2.2">WSL 下</a>
- <a href="#1.3">检查环境安装是否成功</a>
- <a href="#1.4">参考资料</a>

</details>
<details open="open"><summary><a href="#2">实验指南</a></summary>

- <a href="#2.1">关于实验的难易程度</a>
- <a href="#2.2">Debugging 技巧</a>
- <a href="#2.3">参考资料</a>

</details>

<div align="right"><b><a href="#toc">返回顶部↑</a></b></div>

## <a id="1"></a>工具

### <a id="1.1"></a>实验所需环境

> QEMU 5.1, GDB 8.3, GCC, and Binutils.

### <a id="1.2"></a>环境搭建

#### <a id="1.2.1"></a>Ubuntu 下

> Make sure you are running either "bullseye" or "sid" for your debian version (on ubuntu this can be checked by running `cat /etc/debian_version`), then run:
>
> ```bash
> sudo apt-get install git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
> ```
>
> (The version of QEMU on "buster" is too old, so you'd have to get that separately.)

主要是安装 QEMU 以及一系列编译工具.

> [!NOTE]
>
> - 上方并没有给出安装 qemu 的命令, 故仍需手动安装. 直接执行命令 `sudo apt-get install qemu` 即可.

##### <a id="1.2.1.1"></a>qemu-system-misc fix

> At this moment in time, it seems that the package `qemu-system-misc` has received an update that breaks its compatibility with our kernel. If you run `make qemu` and the script appears to hang after
> `qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`
> you'll need to uninstall that package and install an older version:
>
> ```bash
> sudo apt-get remove qemu-system-misc
> sudo apt-get install qemu-system-misc=1:4.2-3ubuntu6
> ```

最新版本的 `qemu-system-misc` 包与 xv6 的源码之间存在兼容性问题, 需要对 `qemu-system-misc` 包进行手动降级.

> [!NOTE]
>
> - `qemu-system-misc=1:4.2-3ubuntu6` 为旧版本 Ubuntu 下的包, 直接尝试安装将提示找不到此包, 这是因为软件源中并没有旧版本 Ubuntu 的包仓库. 解决方法如下:
>
>   1. 备份软件源 `/etc/apt/sources.list`.
>   1. 向软件源中添加旧版本 Ubuntu 的包仓库.
>   1. 执行命令 `sudo apt-get update` 更新数据库.
>   1. 安装 `qemu-system-misc=1:4.2-3ubuntu6`.
>   1. 恢复软件源.
>
>   完整脚本见 [fix-qemu.sh](fix-qemu.sh).
>
> - ⚠️ 执行上述操作之前请务必备份系统.

#### <a id="1.2.2"></a>WSL 下

> We haven't tested it, but it might be possible to get everything you need via the Windows Subsystem for Linux or otherwise compiling the tools yourself.

<div align="right"><b><a href="#toc">返回顶部↑</a></b></div>

### <a id="1.3"></a>检查环境安装是否成功

> To test your installation, you should be able to check the following:
>
> ```text
> $ riscv64-unknown-elf-gcc --version
> riscv64-unknown-elf-gcc (GCC) 10.1.0
> ...
>
> $ qemu-system-riscv64 --version
> QEMU emulator version 5.1.0
> ```
>
> You should also be able to compile and run xv6:
>
> ```text
> # in the xv6 directory
> $ make qemu
> # ... lots of output ...
> init: starting sh
> $
> ```
>
> To quit qemu type: Ctrl-a x.

主要是检查 riscv64-gcc 和 qemu 是否安装正常.

> [!NOTE]
>
> - 上方所运行的是 `riscv64-unknown-elf-gcc`, 但在 WSL2 下执行时将报错: "command not found: riscv64-unknown-elf-gcc". 输入 `risc` + tab 得到如下补全内容:
>
>   ```text
>   $ riscv64-linux-gnu-
>   riscv64-linux-gnu-addr2line      riscv64-linux-gnu-gcc-nm         riscv64-linux-gnu-ld
>   riscv64-linux-gnu-ar             riscv64-linux-gnu-gcc-nm-11      riscv64-linux-gnu-ld.bfd
>   riscv64-linux-gnu-as             riscv64-linux-gnu-gcc-ranlib     riscv64-linux-gnu-lto-dump-11
>   riscv64-linux-gnu-c++filt        riscv64-linux-gnu-gcc-ranlib-11  riscv64-linux-gnu-nm
>   riscv64-linux-gnu-cpp            riscv64-linux-gnu-gcov           riscv64-linux-gnu-objcopy
>   riscv64-linux-gnu-cpp-11         riscv64-linux-gnu-gcov-11        riscv64-linux-gnu-objdump
>   riscv64-linux-gnu-elfedit        riscv64-linux-gnu-gcov-dump      riscv64-linux-gnu-ranlib
>   riscv64-linux-gnu-gcc            riscv64-linux-gnu-gcov-dump-11   riscv64-linux-gnu-readelf
>   riscv64-linux-gnu-gcc-11         riscv64-linux-gnu-gcov-tool      riscv64-linux-gnu-size
>   riscv64-linux-gnu-gcc-ar         riscv64-linux-gnu-gcov-tool-11   riscv64-linux-gnu-strings
>   riscv64-linux-gnu-gcc-ar-11      riscv64-linux-gnu-gprof          riscv64-linux-gnu-strip
>   ```
>
>   可以看出所要执行的版本应为 `riscv64-linux-gnu-gcc`.

<div align="right"><b><a href="#toc">返回顶部↑</a></b></div>

### <a id="1.4"></a>参考资料

- <https://pdos.csail.mit.edu/6.1810/2020/tools.html>

## <a id="2"></a>实验指南

### <a id="2.1"></a>关于实验的难易程度

- 实验通常不需要大量的代码 (一般只需几十到几百行代码), 难的地方在于概念, 因为概念很复杂, 在做实验之前务必确保完成所布置的教材和代码的阅读任务, 并了解相关文档.

### <a id="2.2"></a>Debugging 技巧

- 通不过测试样例的时候记得多用 `printf`.
- 在 qemu 中使用 `printf` 语句直接在终端里输出日志的话找起来会不方便, 因此推荐使用 `script` 程序运行 `make qemu`, 这个程序会将所有控制台输出输出至文件中, 这样找起来会比较方便一些.
- 有些情况下单单使用 `printf` 语句可能无法满足调试需求, 这时候需要使用 gdb 进行单步调试. 要使用 gdb 进行调试需要在一个窗口中执行 `make qemu-gdb`, 然后在另一个窗口中运行 `gdb` 或 `riscv64-linux-gnu-gdb`.
- 如果想要查看汇编代码或是某一内核地址下的指令是什么, 可以查看文件 `kernel.asm` 以及其他 `.asm` 文件. 这些 `.asm` 文件均为 make 编译过程中产生的.
- 当内核报错 (报错时会执行 `panic` 函数) 的时候它会输出报错时的 PC 值, 可以利用这一 PC 值通过命令 `addr2line -e kernel/kernel <pc-value>` 或者直接在 `kernel.asm` 中定位报错的函数. 如果想要获取 backtrace 信息可以通过 gdb 在 `panic` 函数上设置端点并在报错之后执行 `bt` 来获取.
- 如果内核在运行过程中卡住, 可以通过像前面所说的方式双开窗口进行调试, 并在内核挂起的时候在 `qemu-gdb` 窗口中发送 Ctrl+C 并执行 `bt` 来获取 backtrace 信息.
- qemu 中也包含一个 "monitor" 能够查询所模拟的机器的状态, 可以通过 Ctrl+A C 来进入 monitor. 一条特别有用的 monitor 命令是 `info mem`, 其作用是打印页表. 使用这条命令之前可能需要先使用 `cpu` 命令选择想要查看的 CPU 核心, 或者也可以从一开始直接用 `make CPUS=1 qemu` 以限制 qemu 仅使用单个核心.

### <a id="2.3"></a>参考资料

- <https://pdos.csail.mit.edu/6.1810/2020/labs/guidance.html>
