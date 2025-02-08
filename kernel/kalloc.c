// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(int current_cpu_id, void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
    struct run *next;
};

struct {
    struct spinlock lock;
    struct run *freelist;
} kmem[NCPU];

void kinit() {
    char buf[32];
    uint64 free_range_interval = (PHYSTOP - (uint64)end) / NCPU;
    uint64 free_range_begin = (uint64)end;

    for (int i = 0; i < NCPU; i++) {
        int actual_bytes_written = snprintf(buf, sizeof(buf) - 1, "kmem-%d", i);

        buf[actual_bytes_written] = '\0';

        initlock(&kmem[i].lock, buf);

        uint64 free_range_end =
            (i == NCPU - 1 ? PHYSTOP : (free_range_begin + free_range_interval)
            );

        freerange(i, (void *)free_range_begin, (void *)free_range_end);

        free_range_begin += free_range_interval;
    }
}

static void _kfree(int current_cpu_id, void *pa) {
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem[current_cpu_id].lock);
    r->next = kmem[current_cpu_id].freelist;
    kmem[current_cpu_id].freelist = r;
    release(&kmem[current_cpu_id].lock);
}

void freerange(int current_cpu_id, void *pa_start, void *pa_end) {
    for (char *p = (char *)PGROUNDUP((uint64)pa_start); p < (char *)pa_end;
         p += PGSIZE) {
        _kfree(current_cpu_id, p);
    }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
    push_off();
    _kfree(cpuid(), pa);
    pop_off();
}

static void *_kalloc(int current_cpu_id) {
    struct run *r;

    acquire(&kmem[current_cpu_id].lock);

    r = kmem[current_cpu_id].freelist;

    if (r) {
        kmem[current_cpu_id].freelist = r->next;
    }

    release(&kmem[current_cpu_id].lock);

    return (void *)r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
    struct run *r;

    push_off();

    int current_cpu_id = cpuid();

    do {
        r = _kalloc(current_cpu_id);

        current_cpu_id = (current_cpu_id + 1) % NCPU;
    } while (!r && current_cpu_id != cpuid());

    if (r) {
        memset((char *)r, 5, PGSIZE); // fill with junk
    }

    pop_off();

    return (void *)r;
}
