#pragma once
#include "packet.h"
#include <stddef.h>

/* Blind analysis of a packet whose protocol is not known. */

#define SG_AN_MAX_CLUSTERS 6
#define SG_AN_MAX_BITS     512

typedef struct {
    uint16_t us; /* typical duration */
    uint16_t count;
} SgCluster;

typedef enum {
    SG_ENC_UNKNOWN,
    SG_ENC_PWM, /* HIGH+LOW constant, long HIGH = 1 */
    SG_ENC_PWM_LH, /* start pulse, then LOW+HIGH constant, long LOW = 1 */
    SG_ENC_PPM, /* fixed pulse, LOW length carries the bit */
    SG_ENC_PDM, /* fixed gap, HIGH length carries the bit */
    SG_ENC_MANCHESTER, /* HIGH->LOW transition = 1 */
} SgEncoding;

typedef struct {
    SgCluster hi[SG_AN_MAX_CLUSTERS], lo[SG_AN_MAX_CLUSTERS];
    uint8_t n_hi, n_lo;
    uint16_t te; /* shortest stable duration */
    uint32_t baud; /* 1e6 / te */
    uint32_t length_us;
    SgEncoding enc;
    uint8_t bits[SG_AN_MAX_BITS / 8]; /* MSB first */
    uint16_t n_bits;
} SgAnalysis;

void sg_analyze(const SgPacket* pkt, SgAnalysis* out);
const char* sg_encoding_name(SgEncoding enc);

/* Logarithmic duration histogram for display. Bin i covers
 * [min_us * r^i, min_us * r^(i+1)) with r chosen so nbins reach max_us. */
void sg_histogram(const SgPacket* pkt, bool high, uint16_t* bins, int nbins, uint32_t min_us, uint32_t max_us);
uint32_t sg_histogram_bin_us(int bin, int nbins, uint32_t min_us, uint32_t max_us);

/* Hex dump of a bit string (last nibble left aligned). */
int sg_bits_to_hex(const uint8_t* bits, int n_bits, char* buf, size_t size);
/* Number of differing bits; mask (same layout) gets a 1 for each difference. */
int sg_bits_diff(const uint8_t* a, const uint8_t* b, int n_bits, uint8_t* mask);
