#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int i;

    if (argc <= 1) {
        fprintf(2, "Usage: rm files...\n");
        exit(1);
    }

    // 逐个 unlink 文件:
    for (i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            fprintf(2, "rm: %s failed to delete\n", argv[i]);
            break;
        }
    }

    exit(0);
}
