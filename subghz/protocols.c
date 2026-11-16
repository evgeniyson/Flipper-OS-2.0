/* Protocol registry and the two table-driven families of fixed-code remotes.
 *
 * Family A ("HL"): every bit is a HIGH followed by a LOW, the frame ends with
 *   a stop pulse and a long guard LOW.            Princeton, SMC5326, Linear
 * Family B ("LH"): a long header LOW, a start pulse, then every bit is a LOW
 *   followed by a HIGH.                           CAME, Nice FLO, Holtek, GateTX
 */
#include "protocol.h"
#include "../core/fmt.h"
#include "../core/util.h"
#include <string.h>

typedef struct {
    uint8_t bits[4]; /* accepted frame lengths, zero terminated */
    uint8_t hdr_te[4]; /* family B: header LOW per frame length, in te (for encoding) */
    uint16_t te; /* nominal base period */
    uint16_t te_min, te_max;
    uint8_t ratio; /* long symbol = ratio * te */
    uint8_t guard_te; /* family A: guard LOW after the stop pulse */
    uint8_t start_te; /* family B: start pulse length */
    bool no_stop; /* family A without stop pulse (Linear) */
    uint8_t hdr_min_te, hdr_max_x2; /* family B: only match when the header LOW is within
                                     * [min, max_x2 / 2] te */
} PwmSpec;

static uint32_t tol_for(const PwmSpec* s) {
    return s->ratio >= 3 ? 35 : 25;
}

/* Classify one bit made of two durations whose sum is (1 + ratio) * te.
 * Receivers stretch one level and shrink the other, so the sum and the ratio
 * are far more stable than either duration on its own.
 * Returns 1 if a is the long half, 0 if b is, -1 if it is not a valid symbol. */
static int pair_bit(uint32_t a, uint32_t b, uint32_t te, uint32_t ratio) {
    if(!sg_near(a + b, te * (1 + ratio), 30)) return -1;
    uint32_t min_ratio_x10 = ratio >= 3 ? 17 : 13;
    if(a * 10 >= b * min_ratio_x10) return 1;
    if(b * 10 >= a * min_ratio_x10) return 0;
    return -1;
}

/* ------------------------------------------------------------ family A */

static bool decode_hl(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    const PwmSpec* s = self->spec;
    for(int k = 0; k < 4 && s->bits[k]; k++) {
        int bits = s->bits[k];
        int pairs = s->no_stop ? bits - 1 : bits;
        if(p->n != 2 * pairs + 1) continue;
        uint32_t sum = 0;
        for(int i = 0; i < 2 * pairs; i++) sum += p->d[i];
        uint32_t te = sum / (uint32_t)(pairs * (1 + s->ratio));
        if(te < s->te_min || te > s->te_max) continue;
        uint32_t tol = tol_for(s), lng = te * s->ratio;
        uint64_t data = 0;
        bool ok = true;
        for(int i = 0; i < pairs && ok; i++) {
            int bit = pair_bit(p->d[2 * i], p->d[2 * i + 1], te, s->ratio);
            if(bit < 0)
                ok = false;
            else
                data = (data << 1) | (uint64_t)bit;
        }
        if(!ok) continue;
        uint32_t last = p->d[2 * pairs];
        if(s->no_stop) {
            /* the last bit's LOW is merged into the guard */
            if(sg_near(last, te, tol))
                data <<= 1;
            else if(sg_near(last, lng, tol))
                data = (data << 1) | 1;
            else
                continue;
        } else if(last < te / 2 || last > lng + te) {
            /* stop pulse: some encoders send a short one, some a long one */
            continue;
        }
        if(p->gap_after && p->gap_after < te * s->guard_te / 2) continue;
        memset(out, 0, sizeof(*out));
        out->proto = self;
        out->data = data;
        out->bits = (uint8_t)bits;
        out->te = (uint16_t)te;
        sg_static_fill_fields(out);
        return true;
    }
    return false;
}

static size_t encode_hl(const SgProtocol* self, const SgDecoded* in, sg_pulse_t* out, size_t max) {
    const PwmSpec* s = self->spec;
    int32_t te = (in->te >= s->te_min && in->te <= s->te_max) ? in->te : s->te;
    size_t n = 0;
    if(max < (size_t)in->bits * 2 + 2) return 0;
    for(int i = in->bits - 1; i >= 0; i--) {
        bool one = (in->data >> i) & 1;
        bool last = i == 0 && s->no_stop;
        out[n++] = one ? te * s->ratio : te;
        if(last)
            out[n++] = -(one ? te * s->guard_te : te * (s->guard_te + 2));
        else
            out[n++] = -(one ? te : te * s->ratio);
    }
    if(!s->no_stop) {
        out[n++] = te;
        out[n++] = -te * s->guard_te;
    }
    return n;
}

/* ------------------------------------------------------------ family B */

static bool decode_lh(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    const PwmSpec* s = self->spec;
    for(int k = 0; k < 4 && s->bits[k]; k++) {
        int bits = s->bits[k];
        if(p->n != 2 * bits + 1) continue;
        uint32_t sum = 0;
        for(int i = 1; i <= 2 * bits; i++) sum += p->d[i];
        uint32_t te = sum / (uint32_t)(bits * (1 + s->ratio));
        if(te < s->te_min || te > s->te_max) continue;
        if(!sg_near(p->d[0], te * s->start_te, 30)) continue;
        if(s->hdr_max_x2) {
            /* The header LOW sits between frames, so it shows up as the gap on
             * either side; the first frame of a burst only has it after. */
            uint32_t lo = te * s->hdr_min_te, hi = te * s->hdr_max_x2 / 2;
            bool before = p->gap_before >= lo && p->gap_before <= hi;
            bool after = p->gap_after >= lo && p->gap_after <= hi;
            if(!before && !after) continue;
        }
        uint64_t data = 0;
        bool ok = true;
        for(int i = 0; i < bits && ok; i++) {
            int bit = pair_bit(p->d[1 + 2 * i], p->d[2 + 2 * i], te, s->ratio);
            if(bit < 0)
                ok = false;
            else
                data = (data << 1) | (uint64_t)bit;
        }
        if(!ok) continue;
        memset(out, 0, sizeof(*out));
        out->proto = self;
        out->data = data;
        out->bits = (uint8_t)bits;
        out->te = (uint16_t)te;
        sg_static_fill_fields(out);
        return true;
    }
    return false;
}

static size_t encode_lh(const SgProtocol* self, const SgDecoded* in, sg_pulse_t* out, size_t max) {
    const PwmSpec* s = self->spec;
    int32_t te = (in->te >= s->te_min && in->te <= s->te_max) ? in->te : s->te;
    int32_t hdr = s->hdr_te[0];
    for(int k = 0; k < 4 && s->bits[k]; k++)
        if(s->bits[k] == in->bits) hdr = s->hdr_te[k];
    size_t n = 0;
    if(max < (size_t)in->bits * 2 + 2) return 0;
    out[n++] = -te * hdr;
    out[n++] = te * s->start_te;
    for(int i = in->bits - 1; i >= 0; i--) {
        bool one = (in->data >> i) & 1;
        out[n++] = -(one ? te * s->ratio : te);
        out[n++] = one ? te : te * s->ratio;
    }
    return n;
}

/* ------------------------------------------------------------ registry */

static const PwmSpec spec_princeton = {
    .bits = {24}, .te = 390, .te_min = 120, .te_max = 700, .ratio = 3, .guard_te = 30};
static const PwmSpec spec_smc5326 = {
    .bits = {25}, .te = 300, .te_min = 120, .te_max = 600, .ratio = 3, .guard_te = 25};
static const PwmSpec spec_linear = {
    .bits = {10}, .te = 500, .te_min = 350, .te_max = 650, .ratio = 3, .guard_te = 42, .no_stop = true};
static const PwmSpec spec_holtek = {
    .bits = {12}, .hdr_te = {36}, .te = 320, .te_min = 200, .te_max = 480, .ratio = 2,
    .start_te = 1, .hdr_min_te = 20, .hdr_max_x2 = 83};
static const PwmSpec spec_gate_tx = {
    .bits = {24}, .hdr_te = {49}, .te = 350, .te_min = 250, .te_max = 450, .ratio = 2, .start_te = 2};
static const PwmSpec spec_came = {
    .bits = {12, 18, 24, 25}, .hdr_te = {47, 47, 76, 36}, .te = 320, .te_min = 200, .te_max = 480,
    .ratio = 2, .start_te = 1};
static const PwmSpec spec_nice_flo = {
    .bits = {12, 24}, .hdr_te = {36, 36}, .te = 700, .te_min = 520, .te_max = 900, .ratio = 2,
    .start_te = 1};

#define STATIC_HL(ident, label, sp) \
    static const SgProtocol ident = {label, SG_CLASS_STATIC, false, decode_hl, encode_hl, &sp}
#define STATIC_LH(ident, label, sp) \
    static const SgProtocol ident = {label, SG_CLASS_STATIC, false, decode_lh, encode_lh, &sp}

STATIC_HL(proto_princeton, "Princeton", spec_princeton);
STATIC_HL(proto_smc5326, "SMC5326", spec_smc5326);
STATIC_HL(proto_linear, "Linear", spec_linear);
STATIC_LH(proto_holtek, "Holtek_HT12X", spec_holtek);
STATIC_LH(proto_gate_tx, "GateTX", spec_gate_tx);
STATIC_LH(proto_came, "CAME", spec_came);
STATIC_LH(proto_nice_flo, "Nice FLO", spec_nice_flo);

extern const SgProtocol sg_proto_keeloq, sg_proto_nice_flor_s, sg_proto_somfy_telis,
    sg_proto_came_atomo;
extern const SgProtocol sg_proto_nexus_th, sg_proto_gt_wt02, sg_proto_infactory,
    sg_proto_lacrosse_tx141, sg_proto_acurite_592;

/* Order matters where two protocols share a frame shape: the more specific
 * matcher goes first. */
const SgProtocol* const sg_protocols[] = {
    &proto_princeton,      &proto_smc5326,      &proto_linear,        &proto_holtek,
    &proto_gate_tx,        &proto_came,         &proto_nice_flo,      &sg_proto_keeloq,
    &sg_proto_nice_flor_s, &sg_proto_somfy_telis, &sg_proto_nexus_th, &sg_proto_gt_wt02,
    &sg_proto_infactory,   &sg_proto_lacrosse_tx141, &sg_proto_acurite_592, &sg_proto_came_atomo,
};
const size_t sg_protocol_count = ARRAY_SIZE(sg_protocols);

bool sg_decode_packet(const SgPacket* pkt, SgDecoded* out) {
    if(pkt->truncated) return false;
    for(size_t i = 0; i < sg_protocol_count; i++)
        if(sg_protocols[i]->decode(sg_protocols[i], pkt, out)) return true;
    return false;
}

const SgProtocol* sg_protocol_by_name(const char* name) {
    for(size_t i = 0; i < sg_protocol_count; i++)
        if(strcmp(sg_protocols[i]->name, name) == 0) return sg_protocols[i];
    return NULL;
}

void sg_static_fill_fields(SgDecoded* d) {
    if(d->proto == &proto_princeton || d->proto == &proto_holtek) {
        d->serial = (uint32_t)(d->data >> 4);
        d->btn = (uint8_t)(d->data & 0xF);
        d->flags |= SG_F_SERIAL | SG_F_BTN;
    }
}

size_t sg_encode(const SgDecoded* d, sg_pulse_t* out, size_t max) {
    if(!d->proto || !d->proto->encode || d->proto->cls != SG_CLASS_STATIC) return 0;
    return d->proto->encode(d->proto, d, out, max);
}

int sg_halfbits(const uint16_t* d, int n, bool first_high, uint32_t te, uint8_t* hb, int max) {
    int k = 0;
    bool level = first_high;
    for(int i = 0; i < n; i++, level = !level) {
        int units;
        if(sg_near(d[i], te, 35))
            units = 1;
        else if(sg_near(d[i], 2 * te, 30))
            units = 2;
        else
            return -1;
        while(units--) {
            if(k >= max) return -1;
            hb[k++] = level;
        }
    }
    return k;
}

int sg_decoded_describe(const SgDecoded* d, char* buf, size_t size) {
    int n = 0;
#define ADD(...) \
    do { \
        if((size_t)n < size) n += fmt_snprintf(buf + n, size - (size_t)n, __VA_ARGS__); \
    } while(0)
    ADD("%s %ubit%s\n", d->proto->name, d->bits, d->proto->experimental ? " (exp)" : "");
    if(d->bits > 32)
        ADD("Key: %08lX%08lX\n", (unsigned long)(d->data >> 32), (unsigned long)(d->data & 0xFFFFFFFFu));
    else
        ADD("Key: %0*lX\n", (d->bits + 3) / 4, (unsigned long)d->data);
    if(d->flags & SG_F_SERIAL) ADD("Sn: %07lX", (unsigned long)d->serial);
    if(d->flags & SG_F_BTN) ADD(" Btn: %X", d->btn);
    if(d->flags & (SG_F_SERIAL | SG_F_BTN)) ADD("\n");
    if(d->flags & SG_F_CNT) ADD("Cnt: %04X\n", d->cnt);
    if(d->flags & SG_F_HOP) ADD("Hop: %08lX (encrypted)\n", (unsigned long)d->hop);
    if(d->flags & SG_F_ID) ADD("Id: %lu", (unsigned long)d->id);
    if(d->flags & SG_F_CHANNEL) ADD(" Ch: %u", d->channel);
    if(d->flags & (SG_F_ID | SG_F_CHANNEL)) ADD("\n");
    if(d->flags & SG_F_TEMP) {
        int t = d->temp_c10;
        ADD("T: %s%d.%dC", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
    }
    if(d->flags & SG_F_HUM) ADD(" H: %u%%", d->humidity);
    if(d->flags & SG_F_BATT) ADD(" Bat: %s", d->battery_low ? "low" : "ok");
    if(d->flags & (SG_F_TEMP | SG_F_HUM | SG_F_BATT)) ADD("\n");
    if(d->flags & SG_F_CRC_BAD) ADD("Checksum: BAD\n");
    ADD("TE: %uus", d->te);
#undef ADD
    return n;
}
