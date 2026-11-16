#include "analyzer.h"
#include "protocol.h"
#include "../core/util.h"
#include <string.h>

static void sort16(uint16_t* v, int n) {
    /* shell sort: no recursion, no libc dependency */
    for(int gap = n / 2; gap > 0; gap /= 2)
        for(int i = gap; i < n; i++) {
            uint16_t t = v[i];
            int j = i;
            for(; j >= gap && v[j - gap] > t; j -= gap) v[j] = v[j - gap];
            v[j] = t;
        }
}

/* Group sorted durations: a jump of more than ~25% starts a new cluster. */
static uint8_t cluster(uint16_t* v, int n, SgCluster* out) {
    SgCluster all[32];
    int na = 0, start = 0;
    for(int i = 1; i <= n; i++) {
        if(i == n || v[i] - v[i - 1] > v[i - 1] / 4 + 20) {
            if(na < (int)ARRAY_SIZE(all)) {
                all[na].us = v[start + (i - start) / 2]; /* median */
                all[na].count = (uint16_t)(i - start);
                na++;
            }
            start = i;
        }
    }
    /* keep clusters that are not just a stray pulse */
    int min_count = MAX(2, n / 25);
    uint8_t k = 0;
    for(int i = 0; i < na && k < SG_AN_MAX_CLUSTERS; i++)
        if(all[i].count >= min_count) out[k++] = all[i];
    return k;
}

static int nearest(const SgCluster* c, int n, uint32_t v) {
    int best = 0;
    uint32_t bd = 0xFFFFFFFFu;
    for(int i = 0; i < n; i++) {
        uint32_t d = v > c[i].us ? v - c[i].us : c[i].us - v;
        if(d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void add_bit(SgAnalysis* a, bool bit) {
    if(a->n_bits >= SG_AN_MAX_BITS) return;
    if(bit) a->bits[a->n_bits / 8] |= (uint8_t)(0x80 >> (a->n_bits % 8));
    a->n_bits++;
}

/* Share of (first, second) pairs whose sum stays near the median sum. */
static int pair_consistency(const SgPacket* p, int start, uint16_t* tmp) {
    int n = 0;
    for(int i = start; i + 1 < p->n; i += 2) tmp[n++] = (uint16_t)MIN(65535u, (uint32_t)p->d[i] + p->d[i + 1]);
    if(n < 8) return 0;
    sort16(tmp, n);
    uint32_t med = tmp[n / 2];
    int ok = 0;
    for(int i = 0; i < n; i++)
        if(sg_near(tmp[i], med, 18)) ok++;
    return ok * 100 / n;
}

void sg_analyze(const SgPacket* p, SgAnalysis* a) {
    static uint16_t tmp[SG_PKT_MAX];
    memset(a, 0, sizeof(*a));
    int nh = 0, nl = 0;
    for(int i = 0; i < p->n; i += 2) tmp[nh++] = p->d[i];
    sort16(tmp, nh);
    a->n_hi = cluster(tmp, nh, a->hi);
    for(int i = 1; i < p->n; i += 2) tmp[nl++] = p->d[i];
    sort16(tmp, nl);
    a->n_lo = cluster(tmp, nl, a->lo);
    for(int i = 0; i < p->n; i++) a->length_us += p->d[i];

    uint32_t te = 0xFFFF;
    if(a->n_hi) te = MIN(te, a->hi[0].us);
    if(a->n_lo) te = MIN(te, a->lo[0].us);
    if(te == 0xFFFF || te == 0) return;
    a->te = (uint16_t)te;
    a->baud = 1000000u / te;

    const SgCluster *H = a->hi, *L = a->lo;
    if(a->n_hi == 1 && a->n_lo >= 2) {
        a->enc = SG_ENC_PPM;
        for(int i = 0; i + 1 < p->n; i += 2) {
            int c = nearest(L, a->n_lo, p->d[i + 1]);
            if(c <= 1) add_bit(a, c == 1);
        }
        return;
    }
    if(a->n_hi >= 2 && a->n_lo == 1) {
        a->enc = SG_ENC_PDM;
        for(int i = 0; i < p->n; i += 2) {
            int c = nearest(H, a->n_hi, p->d[i]);
            if(c <= 1) add_bit(a, c == 1);
        }
        return;
    }
    if(a->n_hi >= 2 && a->n_lo >= 2) {
        int hl = pair_consistency(p, 0, tmp);
        int lh = pair_consistency(p, 1, tmp);
        if(hl >= 80 && hl >= lh) {
            a->enc = SG_ENC_PWM;
            for(int i = 0; i + 1 < p->n; i += 2) add_bit(a, p->d[i] > p->d[i + 1]);
            return;
        }
        if(lh >= 80) {
            a->enc = SG_ENC_PWM_LH;
            for(int i = 1; i + 1 < p->n; i += 2) add_bit(a, p->d[i] > p->d[i + 1]);
            return;
        }
        /* Manchester: only te and 2*te on both levels */
        static uint8_t hb[2 * SG_AN_MAX_BITS + 4];
        int n = sg_halfbits(p->d, p->n, true, te, hb + 1, (int)sizeof(hb) - 2);
        if(n > 16) {
            hb[0] = 0; /* idle level before the first pulse */
            hb[n + 1] = 0; /* and after the last one */
            for(int off = 1; off >= 0; off--) {
                bool valid = true;
                for(int k = off; k + 1 < n + 2 && valid; k += 2) valid = hb[k] != hb[k + 1];
                if(!valid) continue;
                a->enc = SG_ENC_MANCHESTER;
                for(int k = off; k + 1 < n + 2; k += 2) add_bit(a, hb[k]);
                return;
            }
        }
    }
}

const char* sg_encoding_name(SgEncoding enc) {
    switch(enc) {
    case SG_ENC_PWM:
        return "PWM";
    case SG_ENC_PWM_LH:
        return "PWM-LH";
    case SG_ENC_PPM:
        return "PPM";
    case SG_ENC_PDM:
        return "PDM";
    case SG_ENC_MANCHESTER:
        return "Manchester";
    default:
        return "unknown";
    }
}

/* Integer log2 in 1/16 steps: good enough to place a duration in a bin. */
static uint32_t log2_x16(uint32_t v) {
    if(v == 0) return 0;
    uint32_t e = 31u - (uint32_t)__builtin_clz(v);
    uint32_t frac = e >= 4 ? (v >> (e - 4)) & 0xF : (v << (4 - e)) & 0xF;
    return e * 16 + frac;
}

static int hist_bin(uint32_t us, int nbins, uint32_t min_us, uint32_t max_us) {
    if(us < min_us) return 0;
    uint32_t lo = log2_x16(min_us), hi = log2_x16(max_us);
    if(hi <= lo) return 0;
    uint32_t b = (log2_x16(us) - lo) * (uint32_t)nbins / (hi - lo);
    return b >= (uint32_t)nbins ? nbins - 1 : (int)b;
}

void sg_histogram(const SgPacket* p, bool high, uint16_t* bins, int nbins, uint32_t min_us, uint32_t max_us) {
    memset(bins, 0, sizeof(uint16_t) * (size_t)nbins);
    for(int i = high ? 0 : 1; i < p->n; i += 2) {
        uint16_t* b = &bins[hist_bin(p->d[i], nbins, min_us, max_us)];
        if(*b < 0xFFFF) (*b)++;
    }
}

uint32_t sg_histogram_bin_us(int bin, int nbins, uint32_t min_us, uint32_t max_us) {
    /* inverse of hist_bin by search; called only when drawing an axis */
    uint32_t lo = min_us, hi = max_us;
    while(lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if(hist_bin(mid, nbins, min_us, max_us) < bin)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int sg_bits_to_hex(const uint8_t* bits, int n_bits, char* buf, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    int nibbles = (n_bits + 3) / 4, n = 0;
    for(int i = 0; i < nibbles && (size_t)n + 1 < size; i++) {
        uint8_t byte = bits[i / 2];
        buf[n++] = hex[(i & 1) ? (byte & 0xF) : (byte >> 4)];
    }
    if(size) buf[n] = '\0';
    return n;
}

int sg_bits_diff(const uint8_t* a, const uint8_t* b, int n_bits, uint8_t* mask) {
    int diff = 0;
    for(int i = 0; i < (n_bits + 7) / 8; i++) {
        uint8_t x = a[i] ^ b[i];
        if(i == n_bits / 8 && (n_bits % 8)) x &= (uint8_t)(0xFF << (8 - n_bits % 8));
        if(mask) mask[i] = x;
        diff += __builtin_popcount(x);
    }
    return diff;
}
