// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
    struct run *next;
};

struct {
    struct spinlock lock;
    struct run *freelist;
    int reference_counts_of_all_pages[PGROUNDUP(PHYSTOP) / PGSIZE];
} kmem;

#define REFERENCE_COUNT(cow_page_pa)                                           \
    (kmem.reference_counts_of_all_pages[((uint64)cow_page_pa) >> 12])

static int
atomic_fetch_and_add_cow_page_reference_count(uint64 cow_page_pa, int offset) {
    acquire(&kmem.lock);

    int old_reference_count = REFERENCE_COUNT(cow_page_pa);

    REFERENCE_COUNT(cow_page_pa) += offset;

    release(&kmem.lock);

    return old_reference_count;
}

int atomic_fetch_cow_page_reference_count(uint64 cow_page_pa) {
    return atomic_fetch_and_add_cow_page_reference_count(cow_page_pa, 0);
}

int atomic_increment_cow_page_reference_count(uint64 cow_page_pa) {
    return atomic_fetch_and_add_cow_page_reference_count(cow_page_pa, 1);
}

int atomic_decrement_cow_page_reference_count(uint64 cow_page_pa) {
    return atomic_fetch_and_add_cow_page_reference_count(cow_page_pa, -1);
}

void kinit() {
    initlock(&kmem.lock, "kmem");
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
    uint64 current_page_pa = PGROUNDUP((uint64)pa_start);
    uint64 page_pa_end = (uint64)pa_end;

    while (current_page_pa < page_pa_end) {
        atomic_increment_cow_page_reference_count(current_page_pa);

        kfree((void *)current_page_pa);

        current_page_pa += PGSIZE;
    }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    int old_reference_count =
        atomic_decrement_cow_page_reference_count((uint64)pa);

    if (old_reference_count == 1) {
        // Fill with junk to catch dangling refs.
        memset(pa, 1, PGSIZE);

        acquire(&kmem.lock);

        r = (struct run *)pa;
        r->next = kmem.freelist;
        kmem.freelist = r;

        release(&kmem.lock);
    }

    else if (old_reference_count < 1) {
        panic("reference count should never be less than 0");
    }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
    struct run *page_ptr;

    acquire(&kmem.lock);

    page_ptr = kmem.freelist;

    if (page_ptr) {
        kmem.freelist = page_ptr->next;
    }

    release(&kmem.lock);

    if (page_ptr) {
        memset((char *)page_ptr, 5, PGSIZE); // fill with junk

        atomic_increment_cow_page_reference_count((uint64)page_ptr);
    }

    return (void *)page_ptr;
}

// returns 0 if succeeded; otherwise returns -1
int atomic_copy_and_decrement_cow_page_reference_count(
    uint64 cow_page_pa, uint64 *new_page_pa_ptr
) {
    int old_reference_count =
        atomic_fetch_cow_page_reference_count(cow_page_pa);

    // if we are the last one holding this page
    if (old_reference_count == 1) {
        // return this page directly
        *new_page_pa_ptr = cow_page_pa;

        return 0;
    }

    uint64 new_page = (uint64)kalloc();

    if (new_page == 0) {
        return -1;
    }

    // this operation might be redundant since the reference count might already
    // have been decreased down to 1 during the above operations, but it will
    // cost us much more if we put this check-and-then-copy operation inside a
    // giant critical section
    memmove((void *)new_page, (void *)cow_page_pa, PGSIZE);

    *new_page_pa_ptr = new_page;

    kfree((void *)cow_page_pa);

    return 0;
}
