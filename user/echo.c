#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int i;

    // 遍历所有参数 (下标从 1 开始):
    for (i = 1; i < argc; i++) {
        // 输出至标准输出:
        write(1, argv[i], strlen(argv[i]));

        // 字符串之间以单个空格分隔:
        if (i < argc - 1) {
            write(1, " ", 1);
        }
        else {
            write(1, "\n", 1);
        }
    }

    exit(0);
}
