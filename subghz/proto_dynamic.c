/* Rolling-code remotes. These are recognised and their clear-text fields are
 * shown; the encrypted part is displayed as received. There is deliberately no
 * decryption and no encoder: SgProtocol.encode is NULL for all of them. */
#include "protocol.h"
#include <string.h>

/* ------------------------------------------------------------ KeeLoq
 * Preamble of ~12 te pulses, 10 te header LOW, then 66 PWM bits sent LSB first:
 * 32 bit hopping code, 28 bit serial, 4 bit button, 2 status bits.
 * '1' = te HIGH + 2te LOW, '0' = 2te HIGH + te LOW. */
static bool keeloq_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    int hdr = -1;
    for(int i = 1; i < p->n && i < 40; i += 2) {
        if(p->d[i] >= 2800 && p->d[i] < 5000) {
            hdr = i;
            break;
        }
    }
    if(hdr < 5) return false;
    uint32_t sum = 0;
    for(int i = 0; i < hdr; i++) sum += p->d[i];
    uint32_t te = sum / (uint32_t)hdr;
    if(te < 280 || te > 520) return false;
    for(int i = 0; i < hdr; i++)
        if(!sg_near(p->d[i], te, 40)) return false;
    if(!sg_near(p->d[hdr], 10 * te, 30)) return false;

    int j = hdr + 1;
    int pairs = (p->n - j + 1) / 2;
    if(pairs < 64 || pairs > 66) return false;
    uint64_t value = 0;
    for(int k = 0; k < pairs; k++) {
        uint32_t h = p->d[j + 2 * k];
        bool one = h < te + te / 2;
        if(!sg_near(h, one ? te : 2 * te, 40)) return false;
        if(j + 2 * k + 1 < p->n && !sg_near(h + p->d[j + 2 * k + 1], 3 * te, 30)) return false;
        if(k < 64 && one) value |= 1ULL << k;
    }
    uint32_t fix = (uint32_t)(value >> 32);
    memset(out, 0, sizeof(*out));
    out->proto = self;
    out->data = value; /* fixed part in the high word, hopping code in the low word */
    out->bits = 64;
    out->te = (uint16_t)te;
    out->hop = (uint32_t)value;
    out->serial = fix & 0x0FFFFFFF;
    out->btn = (uint8_t)(fix >> 28);
    out->flags = SG_F_SERIAL | SG_F_BTN | SG_F_HOP;
    return true;
}

/* ------------------------------------------------------------ Nice FLOR-S
 * 3te HIGH, 3te LOW, then 52 PWM bits (72 for Nice One), 3te stop pulse.
 * '0' = te HIGH + 2te LOW, '1' = 2te HIGH + te LOW. Everything is encrypted. */
static bool nice_flor_s_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    int bits = (p->n - 3) / 2;
    if(p->n != 2 * bits + 3 || (bits != 52 && bits != 72)) return false;
    uint32_t sum = 0;
    for(int i = 2; i < 2 + 2 * bits; i++) sum += p->d[i];
    uint32_t te = sum / (uint32_t)(bits * 3);
    if(te < 380 || te > 650) return false;
    if(!sg_near(p->d[0], 3 * te, 30) || !sg_near(p->d[1], 3 * te, 30)) return false;
    if(!sg_near(p->d[p->n - 1], 3 * te, 30)) return false;
    uint64_t data = 0;
    for(int k = 0; k < bits; k++) {
        uint32_t h = p->d[2 + 2 * k], l = p->d[3 + 2 * k];
        bool bit;
        if(sg_near(h, te, 30) && sg_near(l, 2 * te, 25))
            bit = false;
        else if(sg_near(h, 2 * te, 25) && sg_near(l, te, 30))
            bit = true;
        else
            return false;
        if(k < 64) data = (data << 1) | bit;
    }
    memset(out, 0, sizeof(*out));
    out->proto = self;
    out->data = data;
    out->bits = (uint8_t)bits;
    out->te = (uint16_t)te;
    return true;
}

/* ------------------------------------------------------------ Somfy Telis (RTS)
 * Hardware sync pulses of 4te, a ~7te software sync HIGH and a te LOW, then
 * 56 Manchester bits with te = 640us. The frame is obfuscated by XOR-chaining
 * the bytes (not encryption); a 4-bit XOR checksum validates the result. */
static bool somfy_try(const uint8_t* hb, bool rising_is_one, SgDecoded* out) {
    uint8_t b[7] = {0};
    for(int k = 0; k < 56; k++) {
        uint8_t a = hb[2 * k], c = hb[2 * k + 1];
        if(a == c) return false;
        bool bit = rising_is_one ? (a == 0) : (a == 1);
        b[k / 8] = (uint8_t)((b[k / 8] << 1) | bit);
    }
    for(int i = 6; i > 0; i--) b[i] ^= b[i - 1];
    uint8_t ck = 0;
    for(int i = 0; i < 7; i++) ck ^= (uint8_t)(b[i] ^ (b[i] >> 4));
    if(ck & 0xF) return false;
    uint64_t data = 0;
    for(int i = 0; i < 7; i++) data = (data << 8) | b[i];
    out->data = data;
    out->btn = (uint8_t)((data >> 44) & 0xF);
    out->cnt = (uint16_t)((data >> 24) & 0xFFFF);
    out->serial = (uint32_t)(data & 0xFFFFFF);
    return true;
}

static bool somfy_telis_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    const uint32_t te = 640;
    int s = -1;
    for(int i = 0; i < p->n && i < 20; i += 2) {
        if(p->d[i] >= 3900 && p->d[i] < 5400) {
            s = i;
            break;
        }
    }
    if(s < 0 || s + 1 >= p->n) return false;
    uint8_t hb[120];
    int n = sg_halfbits(&p->d[s + 1], p->n - (s + 1), false, te, hb, 116);
    if(n < 112 || n > 114) return false;
    /* hb[0] is the software sync LOW; a missing trailing half-bit is a LOW */
    while(n < 114) hb[n++] = 0;
    memset(out, 0, sizeof(*out));
    if(!somfy_try(&hb[1], true, out) && !somfy_try(&hb[1], false, out)) return false;
    out->proto = self;
    out->bits = 56;
    out->te = (uint16_t)te;
    out->flags = SG_F_SERIAL | SG_F_BTN | SG_F_CNT | SG_F_CRC_OK;
    return true;
}

/* ------------------------------------------------------------ CAME Atomo
 * 62 Manchester bits with te = 600us after a very long header LOW. Only the
 * timing signature is recognised; the payload is encrypted. */
static bool came_atomo_decode(const SgProtocol* self, const SgPacket* p, SgDecoded* out) {
    if(p->n < 60 || p->n > 126) return false;
    /* frames are separated by the 72 ms header; the first one of a burst has
     * whatever came before it, so accept the header on either side */
    if(p->gap_before && p->gap_before < 20000 && p->gap_after && p->gap_after < 20000) return false;
    uint8_t hb[132];
    int n = sg_halfbits(p->d, p->n, true, 600, hb + 1, 130);
    if(n < 119 || n > 125) return false;
    hb[0] = 0; /* the tail of the header LOW is the first half-bit */
    n++;
    while(n < 124) hb[n++] = 0;
    uint64_t data = 0;
    for(int k = 0; k < 62; k++) {
        if(hb[2 * k] == hb[2 * k + 1]) return false;
        data = (data << 1) | hb[2 * k + 1];
    }
    memset(out, 0, sizeof(*out));
    out->proto = self;
    out->data = data;
    out->bits = 62;
    out->te = 600;
    return true;
}

const SgProtocol sg_proto_keeloq = {"KeeLoq", SG_CLASS_DYNAMIC, false, keeloq_decode, NULL, NULL};
const SgProtocol sg_proto_nice_flor_s = {
    "Nice FloR-S", SG_CLASS_DYNAMIC, true, nice_flor_s_decode, NULL, NULL};
const SgProtocol sg_proto_somfy_telis = {
    "Somfy Telis", SG_CLASS_DYNAMIC, true, somfy_telis_decode, NULL, NULL};
const SgProtocol sg_proto_came_atomo = {
    "CAME Atomo", SG_CLASS_DYNAMIC, true, came_atomo_decode, NULL, NULL};
