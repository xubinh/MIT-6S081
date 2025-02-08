// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#include "proc.h"

#define HASH_TABLE_SIZE 13

extern struct spinlock tickslock;
extern uint ticks;

struct {
    struct spinlock lock;
    struct buf buf[NBUF];
} bcache;

struct node {
    struct node *previous_node_ptr;
    struct node *next_node_ptr;
    uint bucket_number;
    struct buf *buf_ptr;
};

struct {
    struct node nodes[NBUF];
    struct node buckets[HASH_TABLE_SIZE];
    struct spinlock locks_for_buckets[HASH_TABLE_SIZE];
} hash_table;

static uint _get_time_stamp() {
    acquire(&tickslock);
    uint time_stamp = ticks;
    release(&tickslock);

    return time_stamp;
}

static uint _hash(uint dev, uint blockno) {
    // uint hash = dev;

    // hash ^= blockno + 0x9e3779b9 + (hash << 6) + (hash >> 2);

    // return hash;

    return blockno;
}

static int _equal(uint dev_1, uint blockno_1, uint dev_2, uint blockno_2) {
    // return dev_1 == dev_2 && blockno_1 == blockno_2;
    return blockno_1 == blockno_2;
}

static uint _get_bucket_number(uint dev, uint blockno) {
    return _hash(dev, blockno) % HASH_TABLE_SIZE;
}

static void _acquire_bucket(uint bucket_number) {
    acquire(&hash_table.locks_for_buckets[bucket_number]);
}

static void _release_bucket(uint bucket_number) {
    release(&hash_table.locks_for_buckets[bucket_number]);
}

static void _cut_node(struct node *node_ptr) {
    node_ptr->previous_node_ptr->next_node_ptr = node_ptr->next_node_ptr;

    if (node_ptr->next_node_ptr) {
        node_ptr->next_node_ptr->previous_node_ptr =
            node_ptr->previous_node_ptr;
    }
}

static void _insert_node(uint bucket_number, struct node *node_ptr) {
    struct node *head = &hash_table.buckets[bucket_number];

    node_ptr->next_node_ptr = head->next_node_ptr;

    if (head->next_node_ptr) {
        head->next_node_ptr->previous_node_ptr = node_ptr;
    }

    node_ptr->previous_node_ptr = head;
    head->next_node_ptr = node_ptr;

    node_ptr->bucket_number = bucket_number;
}

static struct node *_find_node(uint bucket_number, uint dev, uint blockno) {
    struct node *current_node_ptr =
        hash_table.buckets[bucket_number].next_node_ptr;

    while (current_node_ptr) {
        struct buf *current_buf_ptr = current_node_ptr->buf_ptr;

        if (_equal(
                current_buf_ptr->dev, current_buf_ptr->blockno, dev, blockno
            )) {

            return current_node_ptr;
        }

        current_node_ptr = current_node_ptr->next_node_ptr;
    }

    return 0;
}

static struct node *_find_least_recent_used_node(uint bucket_number) {
    uint least_recent_used_buf_time_stamp = 0xffffffff;
    struct node *least_recent_used_node_ptr = 0;

    struct node *current_node_ptr =
        hash_table.buckets[bucket_number].next_node_ptr;

    while (current_node_ptr) {
        struct buf *current_buf_ptr = current_node_ptr->buf_ptr;

        if (current_buf_ptr->refcnt == 0
            && current_buf_ptr->time_stamp < least_recent_used_buf_time_stamp) {
            least_recent_used_buf_time_stamp = current_buf_ptr->time_stamp;
            least_recent_used_node_ptr = current_node_ptr;
        }

        current_node_ptr = current_node_ptr->next_node_ptr;
    }

    return least_recent_used_node_ptr;
}

void binit(void) {
    initlock(&bcache.lock, "bcache");

    for (int current_buf_number = 0; current_buf_number < NBUF;
         current_buf_number++) {
        struct buf *current_buf_ptr = &bcache.buf[current_buf_number];
        struct node *current_node_ptr = &hash_table.nodes[current_buf_number];

        initsleeplock(&current_buf_ptr->lock, "buffer");

        current_buf_ptr->node_ptr = current_node_ptr;
        current_node_ptr->buf_ptr = current_buf_ptr;

        _insert_node(0, current_node_ptr);

        current_buf_ptr->valid = 0;
    }

    for (int current_bucket_number = 0; current_bucket_number < HASH_TABLE_SIZE;
         current_bucket_number++) {
        char temporary_char_buffer[32];

        int actual_bytes_written = snprintf(
            temporary_char_buffer,
            sizeof(temporary_char_buffer) - 1,
            "bcache-bucket-%d",
            current_bucket_number
        );

        temporary_char_buffer[actual_bytes_written] = '\0';

        initlock(
            &hash_table.locks_for_buckets[current_bucket_number],
            temporary_char_buffer
        );
    }
}

static struct buf *bget(uint dev, uint blockno) {
    uint target_bucket_number = _get_bucket_number(dev, blockno);

    _acquire_bucket(target_bucket_number);

    struct node *target_node_ptr =
        _find_node(target_bucket_number, dev, blockno);

    if (target_node_ptr) {
        struct buf *target_buf_ptr = target_node_ptr->buf_ptr;

        target_buf_ptr->refcnt++;

        _release_bucket(target_bucket_number);

        acquiresleep(&target_buf_ptr->lock);

        return target_buf_ptr;
    }

    _release_bucket(target_bucket_number);

    acquire(&bcache.lock);

    _acquire_bucket(target_bucket_number);

    target_node_ptr = _find_node(target_bucket_number, dev, blockno);

    if (target_node_ptr) {
        struct buf *target_buf_ptr = target_node_ptr->buf_ptr;

        target_buf_ptr->refcnt++;

        _release_bucket(target_bucket_number);

        release(&bcache.lock);

        acquiresleep(&target_buf_ptr->lock);

        return target_buf_ptr;
    }

    _release_bucket(target_bucket_number);

    for (int current_bucket_number = 0; current_bucket_number < HASH_TABLE_SIZE;
         current_bucket_number++) {

        _acquire_bucket(current_bucket_number);

        struct node *least_recent_used_node_ptr =
            _find_least_recent_used_node(current_bucket_number);

        if (!least_recent_used_node_ptr) {
            _release_bucket(current_bucket_number);

            continue;
        }

        _cut_node(least_recent_used_node_ptr);

        _release_bucket(current_bucket_number);

        struct buf *spare_buf_ptr = least_recent_used_node_ptr->buf_ptr;

        spare_buf_ptr->dev = dev;
        spare_buf_ptr->blockno = blockno;
        spare_buf_ptr->valid = 0;
        spare_buf_ptr->refcnt = 1;

        _acquire_bucket(target_bucket_number);

        _insert_node(target_bucket_number, least_recent_used_node_ptr);

        _release_bucket(target_bucket_number);

        release(&bcache.lock);

        acquiresleep(&spare_buf_ptr->lock);

        return spare_buf_ptr;
    }

    panic("bget: no buffers");
}

struct buf *bread(uint dev, uint blockno) {
    struct buf *buf_ptr;

    buf_ptr = bget(dev, blockno);

    if (!buf_ptr->valid) {
        virtio_disk_rw(buf_ptr, 0);

        buf_ptr->valid = 1;
    }

    return buf_ptr;
}

void bwrite(struct buf *buf_ptr) {
    if (!holdingsleep(&buf_ptr->lock)) {
        panic("bwrite");
    }

    virtio_disk_rw(buf_ptr, 1);
}

void brelse(struct buf *buf_ptr) {
    if (!holdingsleep(&buf_ptr->lock) || buf_ptr->refcnt == 0) {
        panic("brelse");
    }

    releasesleep(&buf_ptr->lock);

    uint bucket_number = _get_bucket_number(buf_ptr->dev, buf_ptr->blockno);

    _acquire_bucket(bucket_number);

    buf_ptr->refcnt--;

    if (buf_ptr->refcnt == 0) {
        buf_ptr->time_stamp = _get_time_stamp();
    }

    _release_bucket(bucket_number);
}

void bpin(struct buf *buf_ptr) {
    uint bucket_number = buf_ptr->node_ptr->bucket_number;

    _acquire_bucket(bucket_number);

    buf_ptr->refcnt++;

    _release_bucket(bucket_number);
}

void bunpin(struct buf *buf_ptr) {
    uint bucket_number = buf_ptr->node_ptr->bucket_number;

    _acquire_bucket(bucket_number);

    buf_ptr->refcnt--;

    _release_bucket(bucket_number);
}
