#include "packet.h"
#include <stddef.h>

static uint16_t sat16(uint32_t v) {
    return v > 0xFFFF ? 0xFFFF : (uint16_t)v;
}

void sg_packetizer_init(SgPacketizer* z, uint32_t gap_us, SgPacketCb cb, void* ctx) {
    z->pkt.n = 0;
    z->gap_us = gap_us ? gap_us : SG_PKT_GAP_US;
    z->last_low = 0;
    z->in_pkt = false;
    z->skipping = false;
    z->cb = cb;
    z->ctx = ctx;
}

static void finish(SgPacketizer* z, uint32_t gap_after) {
    SgPacket* p = &z->pkt;
    z->in_pkt = false;
    if((p->n & 1) == 0 && p->n > 0) {
        /* ended on a low: fold it into the trailing gap */
        p->n--;
        if(gap_after) gap_after += p->d[p->n];
    }
    p->gap_after = gap_after;
    z->last_low = gap_after;
    if(p->n >= SG_PKT_MIN_PULSES && z->cb) z->cb(p, z->ctx);
    p->n = 0;
}

void sg_packetizer_feed(SgPacketizer* z, sg_pulse_t pulse) {
    if(pulse == 0) return;
    bool high = pulse > 0;
    uint32_t dur = high ? (uint32_t)pulse : (uint32_t)(-(int64_t)pulse);
    SgPacket* p = &z->pkt;

    if(z->skipping) {
        /* the previous packet overflowed: wait for the next real gap */
        if(!high && dur >= z->gap_us) {
            z->skipping = false;
            z->last_low = dur;
        }
        return;
    }
    if(!z->in_pkt) {
        if(!high) {
            uint32_t sum = z->last_low + dur;
            z->last_low = sum < z->last_low ? 0xFFFFFFFFu : sum;
            return;
        }
        p->n = 0;
        p->gap_before = z->last_low;
        p->truncated = false;
        z->in_pkt = true;
    } else if(!high && dur >= z->gap_us) {
        if((p->n & 1) == 0 && p->n > 0) {
            /* two lows in a row: the first one is already stored */
            dur += p->d[p->n - 1];
            p->n--;
        }
        finish(z, dur);
        return;
    }

    bool want_high = (p->n & 1) == 0;
    if(high != want_high) {
        /* same level twice in a row: merge into the previous duration */
        if(p->n > 0) p->d[p->n - 1] = sat16((uint32_t)p->d[p->n - 1] + dur);
        return;
    }
    if(p->n == SG_PKT_MAX) {
        p->truncated = true;
        finish(z, 0);
        z->skipping = true;
        return;
    }
    p->d[p->n++] = sat16(dur);
}

void sg_packetizer_flush(SgPacketizer* z) {
    if(z->in_pkt) finish(z, 0);
    z->skipping = false;
}
