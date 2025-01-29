#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

void find(const char *dir_name, const char *pattern) {
    // printf("visiting dir: %s\n", dir_name);

    int fd;

    // 打开目录文件
    if ((fd = open(dir_name, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", dir_name);
        exit(1);
    }

    // 为递归传入目录路径做准备
    int dir_name_len = strlen(dir_name);
    char path_buffer[512];
    memmove(path_buffer, dir_name, dir_name_len);
    path_buffer[dir_name_len] = '/';

    // 不断读取目录中的条目
    struct dirent de;
    struct stat st;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        // printf("checking: %s\n", de.name);

        if (de.inum == 0) {
            continue;
        }

        // 跳过这两个特殊情况避免循环遍历
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
            continue;
        }

        // 构建完整文件路径
        memmove(path_buffer + dir_name_len + 1, de.name, DIRSIZ);
        path_buffer[dir_name_len + 1 + DIRSIZ] = '\0';

        // 获取文件元数据
        if (stat(path_buffer, &st) < 0) {
            fprintf(2, "find: cannot stat %s\n", path_buffer);
            exit(1);
        }

        // 如果是普通文件
        if (st.type == T_FILE) {
            // 当且仅当匹配时打印文件路径
            if (strcmp(pattern, de.name) == 0) {
                printf("%s\n", path_buffer);
            }
        }

        // 如果是目录文件
        else if (st.type == T_DIR) {
            // 递归调用
            find(path_buffer, pattern);
        }

        // 其余情况跳过
        else {
            continue;
        }
    }

    // 关闭目录文件
    close(fd);
}

int main(int argc, char *argv[]) {
    // 合理性检查
    if (argc != 3) {
        fprintf(2, "usage: find <dir-path> <file-name-pattern>\n");
        exit(1);
    }

    struct stat st;

    // 确认参数 1 是合法的目录路径
    if (stat(argv[1], &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", argv[1]);
        exit(1);
    }

    if (st.type != T_DIR) {
        fprintf(2, "usage: find <dir-path> <file-name-pattern>\n");
        fprintf(2, "find: please input a valid dir path\n");
        exit(1);
    }

    // 确保参数 2 为非空模式
    if (strlen(argv[2]) <= 0) {
        fprintf(2, "usage: find <dir-path> <file-name-pattern>\n");
        fprintf(2, "find: empty file name pattern\n");
        exit(1);
    }

    // 调用 find 函数做真正的搜索
    find(argv[1], argv[2]);

    exit(0);
}