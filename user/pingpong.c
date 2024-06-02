#include "user/user.h"

int main() {
    // 提前创建好两个管道:
    int parent_to_child_pipe[2];
    int child_to_parent_pipe[2];
    pipe(parent_to_child_pipe);
    pipe(child_to_parent_pipe);

    if (fork() == 0) {
        int pid = getpid();

        // 关闭不需要的管道:
        close(parent_to_child_pipe[1]);
        close(child_to_parent_pipe[0]);

        char a_byte;

        // 从父进程读取一个字节:
        if (read(parent_to_child_pipe[0], &a_byte, 1) < 0) {
            fprintf(2, "child read error\n");
            exit(1);
        }

        printf("%d: received ping\n", pid);

        // 写回该字节至父进程:
        if (write(child_to_parent_pipe[1], &a_byte, 1) < 0) {
            fprintf(2, "child write error\n");
            exit(1);
        }

        // 清理垃圾:
        close(parent_to_child_pipe[0]);
        close(child_to_parent_pipe[1]);

        exit(0);
    }

    // 父进程:
    else {
        int pid = getpid();

        // 关闭不需要的管道:
        close(parent_to_child_pipe[0]);
        close(child_to_parent_pipe[1]);

        char a_byte = -1;

        // 向子进程写入一个字节:
        if (write(parent_to_child_pipe[1], &a_byte, 1) < 0) {
            fprintf(2, "parent write error\n");
            exit(1);
        }

        // 从子进程读取返回的字节:
        if (read(child_to_parent_pipe[0], &a_byte, 1) < 0) {
            fprintf(2, "parent read error\n");
            exit(1);
        }

        printf("%d: received pong\n", pid);

        // 清理垃圾:
        close(parent_to_child_pipe[1]);
        close(child_to_parent_pipe[0]);

        exit(0);
    }
}