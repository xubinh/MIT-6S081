# fs

## Large files (moderate)

思路:

- [x] 修改 `kernel/fs.h` 中的相关定义.
- [x] 修改 `kernel/file.h` 中的相关定义.
- [x] 修改 `bmap` 函数 (负责分配) 的定义.
- [x] 修改 `itrunc` 函数 (负责释放) 的定义.
- [x] 注意每一个 `bread` 都需要与一个对应的 `brelse` 进行配对.

## Symbolic links (moderate)

思路:

- [x] 将 `symlinktest` 测试程序添加至 Makefile 中.
- [x] 在 `kernel/syscall.h` 和 `kernel/syscall.c` 中添加系统调用 symlink 的声明.
- [x] 在 `user/usys.pl` 和 `user/user.h` 中添加系统调用 symlink 的用户接口 `int symlink(char *target, char *path);`.
- [x] 在 `kernel/stat.h` 中添加一个新的文件类型 `T_SYMLINK`, 表示符号链接.
- [x] 在 `kernel/fcntl.h` 中添加一个新的选项 `O_NOFOLLOW`, 表示打开符号链接文件本身, 而不是符号链接所指向的文件.
- [x] 在 `kernel/sysfile.c` 中实现系统调用 symlink 的定义 `sys_symlink`.
  - 在实现的过程中需要注意 `begin_op` 与 `end_op`, `ilock` 与 `iunlock`, `iget` 与 `iput`, 以及 `iunlockput` 之间的搭配, 务必确保不重复不遗漏.
- [x] 修改 `sys_open` 的定义以便支持打开符号链接. 尤其是如果指定了 `O_NOFOLLOW` 选项则打开符号链接文件本身.
  - 如果一个符号链接所指向的文件仍然是符号链接, 那么 `sys_open` 需要递归地打开所有符号链接直到遇到普通文件或者所打开的符号链接形成环为止 (本实验暂时可通过判断递归深度是否超过 10 来近似判定是否存在环).
- [x] 本实验暂时不需要实现指向目录的符号链接.
