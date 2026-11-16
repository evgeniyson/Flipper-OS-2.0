#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A pulse is a signed duration in microseconds: positive = carrier present
 * (high), negative = carrier absent (low). Same convention as .sub RAW files. */
typedef int32_t sg_pulse_t;

#define SG_PKT_MAX        1024 /* durations per packet */
#define SG_PKT_GAP_US     5000 /* a low this long ends a packet */
#define SG_PKT_MIN_PULSES 16 /* shorter bursts are treated as noise */

/* One burst of activity between two long gaps.
 * d[0] is always a HIGH duration and levels alternate from there, so a packet
 * always has an odd count and ends on a HIGH. Gaps of 0 mean "unknown". */
typedef struct {
    uint16_t d[SG_PKT_MAX];
    uint16_t n;
    uint32_t gap_before;
    uint32_t gap_after;
    bool truncated;
} SgPacket;

typedef void (*SgPacketCb)(const SgPacket* pkt, void* ctx);

typedef struct {
    SgPacket pkt;
    uint32_t gap_us;
    uint32_t last_low;
    bool in_pkt;
    bool skipping;
    SgPacketCb cb;
    void* ctx;
} SgPacketizer;

void sg_packetizer_init(SgPacketizer* z, uint32_t gap_us, SgPacketCb cb, void* ctx);
void sg_packetizer_feed(SgPacketizer* z, sg_pulse_t p);
/* Emit whatever is buffered (end of file, or the air went quiet). */
void sg_packetizer_flush(SgPacketizer* z);
