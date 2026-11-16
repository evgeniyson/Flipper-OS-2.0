#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Single-producer / single-consumer ring of 32-bit values.
 * The producer is an interrupt handler, the consumer is the main loop;
 * each side only writes its own index, so no locking is needed. */
#define RB_SIZE 4096u /* must be a power of two */

typedef struct {
    volatile uint32_t head; /* written by producer */
    volatile uint32_t tail; /* written by consumer */
    volatile bool overflow;
    int32_t data[RB_SIZE];
} RingBuf;

static inline void rb_reset(RingBuf* rb) {
    rb->head = rb->tail = 0;
    rb->overflow = false;
}

static inline void rb_push(RingBuf* rb, int32_t v) {
    uint32_t h = rb->head;
    if(h - rb->tail >= RB_SIZE) {
        rb->overflow = true;
        return;
    }
    rb->data[h & (RB_SIZE - 1)] = v;
    rb->head = h + 1;
}

static inline bool rb_pop(RingBuf* rb, int32_t* v) {
    uint32_t t = rb->tail;
    if(t == rb->head) return false;
    *v = rb->data[t & (RB_SIZE - 1)];
    rb->tail = t + 1;
    return true;
}

static inline uint32_t rb_count(const RingBuf* rb) {
    return rb->head - rb->tail;
}
