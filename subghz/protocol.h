#pragma once
#include "packet.h"
#include <stddef.h>

typedef enum {
    SG_CLASS_STATIC, /* fixed code: can be decoded and re-encoded */
    SG_CLASS_DYNAMIC, /* rolling code: identified and dissected, never transmitted */
    SG_CLASS_WEATHER, /* sensor telemetry */
} SgClass;

enum {
    SG_F_SERIAL = 1 << 0,
    SG_F_BTN = 1 << 1,
    SG_F_CNT = 1 << 2,
    SG_F_HOP = 1 << 3, /* encrypted part, shown as-is */
    SG_F_TEMP = 1 << 4,
    SG_F_HUM = 1 << 5,
    SG_F_CHANNEL = 1 << 6,
    SG_F_BATT = 1 << 7,
    SG_F_ID = 1 << 8,
    SG_F_CRC_OK = 1 << 9,
    SG_F_CRC_BAD = 1 << 10,
};

struct SgProtocol;

typedef struct {
    const struct SgProtocol* proto;
    uint64_t data; /* received bits, first bit on air in the MSB position */
    uint8_t bits;
    uint16_t te; /* measured base period, us */
    uint32_t flags;
    uint32_t serial;
    uint32_t hop;
    uint32_t id;
    uint16_t cnt;
    uint8_t btn;
    int16_t temp_c10; /* 0.1 C */
    uint8_t humidity;
    uint8_t channel;
    bool battery_low;
} SgDecoded;

typedef struct SgProtocol {
    const char* name;
    SgClass cls;
    bool experimental; /* no reference capture to validate against yet */
    bool (*decode)(const struct SgProtocol* self, const SgPacket* pkt, SgDecoded* out);
    /* One frame, ready to be repeated; NULL when the protocol must not be sent. */
    size_t (*encode)(const struct SgProtocol* self, const SgDecoded* in, sg_pulse_t* out, size_t max);
    const void* spec;
} SgProtocol;

extern const SgProtocol* const sg_protocols[];
extern const size_t sg_protocol_count;

/* Try every registered protocol on a packet. */
bool sg_decode_packet(const SgPacket* pkt, SgDecoded* out);
const SgProtocol* sg_protocol_by_name(const char* name);
/* Fill the derived fields (serial, button...) from data/bits for a static code. */
void sg_static_fill_fields(SgDecoded* d);
/* Human readable multi-line summary; returns the number of characters. */
int sg_decoded_describe(const SgDecoded* d, char* buf, size_t size);
/* One frame of a static code. Returns 0 if the protocol cannot be transmitted. */
size_t sg_encode(const SgDecoded* d, sg_pulse_t* out, size_t max);

/* ---- helpers shared by protocol implementations ---- */
static inline bool sg_near(uint32_t v, uint32_t target, uint32_t tol_pct) {
    uint32_t tol = target * tol_pct / 100 + 25;
    return v + tol >= target && v <= target + tol;
}
/* Turn alternating durations into half-bit levels of length te. Returns the
 * number of half-bits, or -1 if a duration is neither te nor 2*te. */
int sg_halfbits(const uint16_t* d, int n, bool first_high, uint32_t te, uint8_t* hb, int max);
