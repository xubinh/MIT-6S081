# 实验

## <a id="toc"></a>目录

<details open="open"><summary><a href="#1">工具</a></summary>

- <a href="#1.1">实验所需环境</a>
- <a href="#1.2">环境搭建</a>
  - <a href="#1.2.1">Ubuntu 下</a>
    - <a href="#1.2.1.1">qemu-system-misc fix</a>
  - <a href="#1.2.2">WSL2 下</a>
- <a href="#1.3">测试环境是否安装成功</a>

</details>
<details open="open"><summary><a href="#2">实验指南</a></summary>

- <a href="#2.1">关于实验的难易程度</a>
- <a href="#2.2">Debugging 技巧</a>

</details>

<div align="right"><b><a href="#toc">返回顶部↑</a></b></div>

## <a id="1"></a>工具

参考资料:

- [6.S081 / Fall 2020](https://pdos.csail.mit.edu/6.1810/2020/tools.html)

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

> [!IMPORTANT]
>
> - 上面给出的命令并没有安装 qemu, 因此还需要手动安装一下. 直接执行命令 `sudo apt-get install qemu` 即可.

##### <a id="1.2.1.1"></a>qemu-system-misc fix

> At this moment in time, it seems that the package `qemu-system-misc` has received an update that breaks its compatibility with our kernel. If you run `make qemu` and the script appears to hang after
> `qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`
> you'll need to uninstall that package and install an older version:
>
> ```bash
> sudo apt-get remove qemu-system-misc
> sudo apt-get install qemu-system-misc=1:4.2-3ubuntu6
> ```

> [!IMPORTANT]
>
> - `qemu-system-misc=1:4.2-3ubuntu6` 是旧版本 Ubuntu 的包, 直接尝试安装会提示 "找不到此包", 这是因为软件源中并没有旧版本 Ubuntu 的包仓库. 一个可行的解决方法如下:
>   1. 首先备份软件源 `/etc/apt/sources.list`.
>   1. 向软件源 `/etc/apt/sources.list` 中添加旧版本 Ubuntu 的包仓库.
>   1. 执行命令 `sudo apt-get update` 更新数据库.
>   1. 安装 `qemu-system-misc=1:4.2-3ubuntu6`.
>   1. 复原软件源.
>
>   这种方法的好处是不用自己处理依赖关系, 不过一定要在安装前备份整个系统以防新旧系统的包的依赖关系把系统搞崩了.

#### <a id="1.2.2"></a>WSL2 下

> We haven't tested it, but it might be possible to get everything you need via the Windows Subsystem for Linux or otherwise compiling the tools yourself.

<div align="right"><b><a href="#toc">返回顶部↑</a></b></div>

### <a id="1.3"></a>测试环境是否安装成功

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

> [!IMPORTANT]
>
> - 上面运行的是 `riscv64-unknown-elf-gcc`, 然而在 WSL2 下执行时却报错 "command not found: riscv64-unknown-elf-gcc". 输入 `risc` 并 tab 补全发现是有相应程序的:
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
>   根据上述结果来看应该执行 `riscv64-linux-gnu-gcc`.

<div align="right"><b><a href="#toc">返回顶部↑</a></b></div>

## <a id="2"></a>实验指南

参考资料:

- [Lab guidance](https://pdos.csail.mit.edu/6.1810/2020/labs/guidance.html)

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
