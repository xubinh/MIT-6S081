#include "kernel/param.h"
#include "user/user.h"

#define MAXLINE 512

int read_line_from_stdin(char *const buffer, const int buffer_size) {
    char *current_buffer_position = buffer;
    int read_status;

    // 循环退出当且仅当读取到换行符 `\n`, 或者读取到 EOF, 或者读取出错:
    while ((read_status = read(0, current_buffer_position, 1)) > 0) {
        // 如果读取到换行符则需要做截断:
        if (*current_buffer_position == '\n') {
            *current_buffer_position = '\0';
            break;
        }

        current_buffer_position++;
    }

    if (read_status < 0) {
        fprintf(2, "xargs: read from stdin error\n");
        exit(1);
    }

    // 如果读取到 EOF 同样需要截断:
    if (read_status == 0) {
        *current_buffer_position = '\0';
    }

    // 返回读取到的字符串长度:
    return current_buffer_position - buffer;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        fprintf(2, "usage: xargs <command> [<argument>...]\n");
        exit(1);
    }

    // 合理性检查:
    if (strcmp(argv[0], "xargs") != 0) {
        fprintf(2, "surprise motherfucker!\n");
        exit(1);
    }

    char *child_argv[MAXARG];
    int i;

    // 初始化子进程的 argv (在 exec 中是通过空指针 0 来表明数组的结尾的):
    for (i = 0; i < MAXARG; i++) {
        child_argv[i] = 0;
    }

    // xargs 的参数即为子进程的 `argv`:
    for (i = 1; i < argc; i++) {
        child_argv[i - 1] = argv[i];
    }

    // 从 `xargs` 的标准输入中读取的额外参数:
    char line[MAXLINE];
    child_argv[i - 1] = line;

    while (read_line_from_stdin(line, MAXLINE) > 0) {
        // printf("read line: %s\n", line);

        if (fork() == 0) {
            exec(argv[1], child_argv);
        }
        else {
            wait(0);
        }
    }

    exit(0);
}