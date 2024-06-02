// Simple grep.  Only supports ^ . * $ operators.

#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

char buffer[1024];
int match(char *, char *);

void grep(char *pattern, int fd) {
    int current_read_bytes_number, total_read_bytes_number;
    char *line_start_pointer, *line_end_pointer;

    // 不断读取文本内容至缓冲区 (可能一次性读取到多行):
    total_read_bytes_number = 0;
    while ((current_read_bytes_number = read(fd, buffer + total_read_bytes_number, sizeof(buffer) - total_read_bytes_number - 1)) > 0) {
        total_read_bytes_number += current_read_bytes_number;
        buffer[total_read_bytes_number] = '\0';

        // 遍历每一行, 并进行匹配:
        line_start_pointer = buffer;
        while ((line_end_pointer = strchr(line_start_pointer, '\n')) != 0) {
            *line_end_pointer = 0;

            // 如果匹配成功则输出该行:
            if (match(pattern, line_start_pointer)) {
                *line_end_pointer = '\n';
                write(1, line_start_pointer, line_end_pointer + 1 - line_start_pointer);
            }

            // 移动至下一行的开头:
            line_start_pointer = line_end_pointer + 1;
        }

        // 删除已经匹配过的内容, 并将余下内容前移至缓冲区开头:
        if (total_read_bytes_number > 0) {
            total_read_bytes_number -= line_start_pointer - buffer;
            memmove(buffer, line_start_pointer, total_read_bytes_number);
        }
    }
}

int main(int argc, char *argv[]) {
    int fd, i;
    char *pattern;

    if (argc <= 1) {
        fprintf(2, "usage: grep pattern [file ...]\n");
        exit(1);
    }
    pattern = argv[1];

    // 如果没有给出文件路径, 则从标准输入读取文本:
    if (argc <= 2) {
        grep(pattern, 0);
        exit(0);
    }

    // 否则依次 grep 每个文件:
    for (i = 2; i < argc; i++) {
        if ((fd = open(argv[i], 0)) < 0) {
            printf("grep: cannot open %s\n", argv[i]);
            exit(1);
        }
        grep(pattern, fd);
        close(fd);
    }

    exit(0);
}

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9.

int matchhere(char *, char *);
int matchstar(int, char *, char *);

int match(char *re, char *text) {
    // 仅匹配开头:
    if (re[0] == '^') {
        return matchhere(re + 1, text);
    }

    // 匹配整个字符串:
    do { // must look at empty string
        if (matchhere(re, text))
            return 1;
    } while (*text++ != '\0');

    return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text) {
    // 如果是空模式:
    if (re[0] == '\0') {
        return 1;
    }

    // 如果是星号:
    if (re[1] == '*') {
        return matchstar(re[0], re + 2, text);
    }

    // 如果模式仅匹配空字符串:
    if (re[0] == '$' && re[1] == '\0') {
        return *text == '\0';
    }

    // 如果字符串非空并且能够匹配一个字符:
    if (*text != '\0' && (re[0] == '.' || re[0] == *text)) {
        return matchhere(re + 1, text + 1);
    }

    return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text) {
    // 不断吞噬相同字符并匹配余下字符串:
    do { // a * matches zero or more instances
        if (matchhere(re, text)) {
            return 1;
        }
    } while (*text != '\0' && (*text++ == c || c == '.'));

    return 0;
}
