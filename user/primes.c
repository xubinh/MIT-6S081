#include "user/user.h"

int main(int argc, char *argv[]) {
    int left_pipe[2], right_pipe[2];

    // 先创建右边的管道:
    pipe(right_pipe);

    // 子进程:
    if (fork() == 0) {
    child_entrance:
        // 关闭管道的写端并将其移动至左边:
        close(right_pipe[1]);
        left_pipe[0] = right_pipe[0];

        int prime_number;

        // 从左边 (父进程) 读取第一个整数即素数:
        if (read(left_pipe[0], &prime_number, sizeof(int)) < 0) {
            fprintf(2, "child %d read error\n", getpid());
            exit(1);
        }

        printf("prime %d\n", prime_number);

        int number;
        int read_status;
        int has_created_child_process = 0;

        // 不断从左边读取整数:
        while ((read_status = read(left_pipe[0], &number, sizeof(int))) > 0) {
            // 如果该整数能够被筛掉则跳过:
            if (number % prime_number == 0) {
                continue;
            }

            // 否则视情况创建子进程:
            if (!has_created_child_process) {
                has_created_child_process = 1;

                // 创建右边的管道:
                pipe(right_pipe);

                // 如果是子进程:
                if (fork() == 0) {
                    // 重新开始逻辑:
                    goto child_entrance;
                }

                // 如果是父进程:
                else {
                    // 关闭管道的读端:
                    close(right_pipe[0]);
                }
            }

            // 将整数传输给子进程:
            if (write(right_pipe[1], &number, sizeof(int)) < 0) {
                fprintf(2, "child %d write error\n", getpid());
                exit(1);
            }
        }

        // 父进程关闭管道的写端 (若有):
        if (has_created_child_process) {
            close(right_pipe[1]);
        }

        if (read_status < 0) {
            fprintf(2, "child %d read error\n", getpid());
            exit(1);
        }

        // 等待右边处理完毕:
        if (has_created_child_process && wait(0) < 0) {
            fprintf(2, "child %d wait error\n", getpid());
            exit(1);
        }

        // printf("child %d exit\n", getpid());

        exit(0);
    }

    // 父进程:
    else {
        // 关闭管道的读端:
        close(right_pipe[0]);

        int number;

        // 输出所有整数:
        for (number = 2; number <= 35; number++) {
            if (write(right_pipe[1], &number, sizeof(int)) < 0) {
                fprintf(2, "parent write error\n");
                exit(1);
            }
        }

        // 关闭管道的写端:
        close(right_pipe[1]);

        // 等待右边处理完毕:
        if (wait(0) < 0) {
            fprintf(2, "parent wait error\n");
            exit(1);
        }

        exit(0);
    }
}