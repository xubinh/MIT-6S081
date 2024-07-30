#include "types.h"

#ifndef KERNEL_SPINLOCK

#define KERNEL_SPINLOCK

// Mutual exclusion lock.
struct spinlock {
    uint locked; // Is the lock held?

    // For debugging:
    char *name;      // Name of lock.
    struct cpu *cpu; // The cpu holding the lock.
};

#endif