#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(2, "usage: sleep <tick-number>\n");
        exit(1);
    }

    int tick_number = atoi(argv[1]);
    if (sleep(tick_number) < 0) {
        exit(1);
    }

    exit(0);
}