/* Wireless weather sensors (receive only).
 * Frame layouts follow the public descriptions in the rtl_433 project. */
#include "protocol.h"
#include <string.h>

/* Read pulse-distance bits: a short HIGH followed by a LOW whose length is the
 * symbol. Stops at the first LOW that is neither symbol. Returns the bit count
 * and the index of the first unread duration. */
static int ppm_read(
    const SgPacket* p,
    int start,
    uint32_t zero,
    uint32_t one,
    uint64_t* data,
    int* next) {
    int bits = 0;
    uint64_t v = 0;
    int i = start;
    for(; i + 1 < p->n; i += 2) {
        if(p->d[i] < 200 || p->d[i] > 900) break;
        uint32_t l = p->d[i + 1];
        if(sg_near(l, zero, 25))
            v <<= 1;
        else if(sg_near(l, one, 25))
            v = (v << 1) | 1;
        else
            break;
        if(++bits > 64) break;
    }
    *data = v;
    *next = i;
    return bits;
}

/* Read pulse-width bits (HIGH+LOW = constant): long HIGH = 1. */
static int pwm_read(const SgPacket* p, int start, uint32_t te_s, uint32_t te_l, int want, uint64_t* data) {
    uint64_t v = 0;
    int bits = 0;
    for(int i = start; i < p->n && bits < want; i += 2, bits++) {
        uint32_t h = p->d[i];
        bool one = h > (te_s + te_l) / 2;
        if(!sg_near(h, one ? te_l : te_s, 40)) break;
        if(i + 1 < p->n && bits + 1 < want && !sg_near(h + p->d[i + 1], te_s + te_l, 35)) break;
        v = (v << 1) | one;
    }
    *data = v;
    return bits;
}

/* Skip a run of sync pulses whose HIGH and LOW both lie in [lo, hi]. */
static int skip_sync(const SgPacket* p, int start, uint32_t lo, uint32_t hi, int* count) {
    int i = start, c = 0;
    while(i + 1 < p->n && p->d[i] >= lo && p->d[i] <= hi && p->d[i + 1] >= lo && p->d[i + 1] <= hi) {
        i += 2;
        c++;
    }
    *count = c;
    return i;
}

static int16_t sext12(uint32_t v) {
    v &= 0xFFF;
    return (int16_t)(v & 0x800 ? (int32_t)v - 0x1000 : (int32_t)v);
}

static void begin(SgDecoded* o, const SgProtocol* self, uint64_t data, uint8_t bits, uint16_t te) {
    memset(o, 0, sizeof(*o));
    o->proto = self;
    o->data = data;
    o->bits = bits;
    o->te = te;
    o->flags = SG_F_ID | SG_F_CHANNEL | SG_F_TEMP | SG_F_HUM | SG_F_BATT;
}

/* ------------------------------------------------------------ Nexus-TH
 * 36 bits repeated 12 times in one burst, separated by a 4 ms LOW.
 * 500us pulse; 1 ms gap = 0, 2 ms gap = 1.
 * [id:8][batt_ok:1][0][channel:2][temp:12 signed, 0.1C][1111][humidity:8] */
static bool nexus_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    int i = 0;
    while(i + 1 < p->n) {
        uint64_t v;
        int next;
        int bits = ppm_read(p, i, 1000, 2000, &v, &next);
        if(bits == 36 && ((v >> 8) & 0xF) == 0xF && next + 1 < p->n &&
           sg_near(p->d[next + 1], 4000, 25)) {
            uint32_t hum = (uint32_t)(v & 0xFF);
            uint32_t ch = (uint32_t)((v >> 24) & 3);
            if(hum <= 100 && ch < 3) {
                begin(out, self, v, 36, 500);
                out->id = (uint32_t)((v >> 28) & 0xFF);
                out->battery_low = !((v >> 27) & 1);
                out->channel = (uint8_t)(ch + 1);
                out->temp_c10 = sext12((uint32_t)(v >> 12));
                out->humidity = (uint8_t)hum;
                return true;
            }
        }
        i = next + 2; /* skip the pulse and the gap that stopped the reader */
    }
    return false;
}

/* ------------------------------------------------------------ GT-WT-02
 * 37 bits per packet; 500us pulse; 2 ms gap = 0, 4 ms gap = 1; 9 ms sync gap.
 * [id:8][batt_low:1][button:1][channel:2][temp:12 signed][humidity:7][checksum:6] */
static bool gt_wt02_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    if(p->n != 75) return false;
    uint64_t v;
    int next;
    if(ppm_read(p, 0, 2000, 4000, &v, &next) != 37) return false;
    uint32_t sum = (uint32_t)((v >> 5) & 0xE);
    uint64_t t = v >> 9;
    for(int i = 0; i < 7; i++) sum += (uint32_t)((t >> (i * 4)) & 0xF);
    if((sum & 0x3F) != (v & 0x3F)) return false;
    begin(out, self, v, 37, 500);
    out->id = (uint32_t)((v >> 29) & 0xFF);
    out->battery_low = (v >> 28) & 1;
    out->channel = (uint8_t)(((v >> 25) & 3) + 1);
    out->temp_c10 = sext12((uint32_t)(v >> 13));
    out->humidity = (uint8_t)((v >> 6) & 0x7F);
    out->flags |= SG_F_CRC_OK;
    return true;
}

/* ------------------------------------------------------------ inFactory
 * Preamble, 8 ms sync gap, then 40 bits; 500us pulse; 2 ms = 0, 4 ms = 1.
 * [id:8][crc:4][?][batt_low][??][temp:12, 0.1F + 90F][humidity: 2 BCD digits][??][ch:2] */
static bool infactory_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    if(p->n != 81) return false;
    if(p->gap_before && (p->gap_before < 6000 || p->gap_before > 10500)) return false;
    uint64_t v;
    int next;
    if(ppm_read(p, 0, 2000, 4000, &v, &next) != 40) return false;
    uint32_t h10 = (uint32_t)((v >> 8) & 0xF), h1 = (uint32_t)((v >> 4) & 0xF);
    if(h10 > 10 || h1 > 9) return false;
    int32_t f10 = (int32_t)((v >> 12) & 0xFFF) - 900;
    int32_t c10 = (f10 - 320) * 5 / 9;
    if(c10 < -500 || c10 > 800) return false;
    begin(out, self, v, 40, 500);
    out->id = (uint32_t)(v >> 32);
    out->battery_low = (v >> 26) & 1;
    out->channel = (uint8_t)(v & 3);
    out->temp_c10 = (int16_t)c10;
    out->humidity = (uint8_t)(h10 * 10 + h1);
    return true;
}

/* ------------------------------------------------------------ LaCrosse TX141TH-Bv2
 * Four 833us sync pulses, then 40 PWM bits (208 / 417 us), repeated.
 * [id:8][batt_low][test][channel:2][temp:12, 0.1C + 50C][humidity:8][digest:8] */
static uint8_t lfsr_digest8_reflect(const uint8_t* msg, int bytes, uint8_t gen, uint8_t key) {
    uint8_t sum = 0;
    for(int k = bytes - 1; k >= 0; k--) {
        uint8_t data = msg[k];
        for(int i = 0; i < 8; i++) {
            if((data >> i) & 1) sum ^= key;
            key = (key & 0x80) ? (uint8_t)((key << 1) ^ gen) : (uint8_t)(key << 1);
        }
    }
    return sum;
}

static bool lacrosse_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    for(int i = 0; i + 80 < p->n; i += 2) {
        int syncs;
        int j = skip_sync(p, i, 600, 1100, &syncs);
        if(syncs < 3) continue;
        uint64_t v;
        if(pwm_read(p, j, 208, 417, 40, &v) == 40) {
            uint8_t b[5];
            for(int k = 0; k < 5; k++) b[k] = (uint8_t)(v >> (32 - 8 * k));
            int32_t c10 = (int32_t)((v >> 16) & 0xFFF) - 500;
            uint32_t hum = (uint32_t)((v >> 8) & 0xFF);
            bool crc = lfsr_digest8_reflect(b, 4, 0x31, 0xF4) == b[4];
            if(crc || (c10 >= -400 && c10 <= 600 && hum <= 100)) {
                begin(out, self, v, 40, 208);
                out->id = (uint32_t)(v >> 32);
                out->battery_low = (v >> 31) & 1;
                out->channel = (uint8_t)(((v >> 28) & 3) + 1);
                out->temp_c10 = (int16_t)c10;
                out->humidity = (uint8_t)hum;
                out->flags |= crc ? SG_F_CRC_OK : SG_F_CRC_BAD;
                return true;
            }
        }
        i = j;
    }
    return false;
}

/* ------------------------------------------------------------ Acurite 592TXR
 * Four ~600us sync pulses, then 56 PWM bits (200 / 400 us).
 * [ch:2][id:14][p][batt_ok][type:6 = 0x04][p][hum:7][p][??][temp hi:5][p][temp lo:7][sum:8] */
static bool acurite_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    for(int i = 0; i + 110 < p->n; i += 2) {
        int syncs;
        int j = skip_sync(p, i, 450, 800, &syncs);
        if(syncs < 3) continue;
        uint64_t v;
        if(pwm_read(p, j, 200, 400, 56, &v) == 56) {
            uint8_t b[7], sum = 0;
            for(int k = 0; k < 7; k++) b[k] = (uint8_t)(v >> (48 - 8 * k));
            for(int k = 0; k < 6; k++) sum = (uint8_t)(sum + b[k]);
            if(sum == b[6] && (b[2] & 0x3F) == 0x04) {
                static const uint8_t channel_map[4] = {3, 0, 2, 1}; /* C, -, B, A */
                uint32_t raw = (uint32_t)(((v >> 9) & 0xF80) | ((v >> 8) & 0x7F));
                begin(out, self, v, 56, 200);
                out->channel = channel_map[(v >> 54) & 3];
                out->id = (uint32_t)((v >> 40) & 0x3FFF);
                out->battery_low = !((v >> 38) & 1);
                out->humidity = (uint8_t)((v >> 24) & 0x7F);
                out->temp_c10 = (int16_t)((int32_t)raw - 1000);
                out->flags |= SG_F_CRC_OK;
                return true;
            }
        }
        i = j;
    }
    return false;
}

const SgProtocol sg_proto_nexus_th = {"Nexus-TH", SG_CLASS_WEATHER, true, nexus_decode, NULL, NULL};
const SgProtocol sg_proto_gt_wt02 = {"GT-WT02", SG_CLASS_WEATHER, true, gt_wt02_decode, NULL, NULL};
const SgProtocol sg_proto_infactory = {
    "inFactory-TH", SG_CLASS_WEATHER, true, infactory_decode, NULL, NULL};
const SgProtocol sg_proto_lacrosse_tx141 = {
    "LaCrosse TX141THBv2", SG_CLASS_WEATHER, true, lacrosse_decode, NULL, NULL};
const SgProtocol sg_proto_acurite_592 = {
    "Acurite 592TXR", SG_CLASS_WEATHER, true, acurite_decode, NULL, NULL};
