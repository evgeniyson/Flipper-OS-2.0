#include "presets.h"
#include "cc1101_regs.h"
#include "../core/util.h"
#include <string.h>

/* Asynchronous (transparent) mode: the demodulated signal goes straight to
 * GD0 and the MCU times the edges. Values per the CC1101 datasheet. */
static const uint8_t regs_ook_270[] = {
    CC1101_IOCFG0,   0x0D, /* GD0: async serial data */
    CC1101_FIFOTHR,  0x47, /* ADC retention */
    CC1101_PKTCTRL0, 0x32, /* async, infinite length, no whitening */
    CC1101_FSCTRL1,  0x06, /* IF 152 kHz */
    CC1101_MDMCFG0,  0x00,
    CC1101_MDMCFG1,  0x00,
    CC1101_MDMCFG2,  0x30, /* ASK/OOK, no sync */
    CC1101_MDMCFG3,  0x32, /* 3.79 kBaud */
    CC1101_MDMCFG4,  0x67, /* RX BW 270 kHz */
    CC1101_MCSM0,    0x18, /* auto-calibrate on IDLE->RX/TX */
    CC1101_FOCCFG,   0x18,
    CC1101_AGCCTRL0, 0x40,
    CC1101_AGCCTRL1, 0x00,
    CC1101_AGCCTRL2, 0x03, /* max gain, target 24 dB */
    CC1101_WORCTRL,  0xFB,
    CC1101_FREND0,   0x11, /* OOK: PATABLE[1] is the "on" level */
    CC1101_FREND1,   0xB6,
    0, 0};

static const uint8_t regs_ook_650[] = {
    CC1101_IOCFG0,   0x0D,
    CC1101_FIFOTHR,  0x07,
    CC1101_PKTCTRL0, 0x32,
    CC1101_FSCTRL1,  0x06,
    CC1101_MDMCFG0,  0x00,
    CC1101_MDMCFG1,  0x00,
    CC1101_MDMCFG2,  0x30,
    CC1101_MDMCFG3,  0x32,
    CC1101_MDMCFG4,  0x17, /* RX BW 650 kHz */
    CC1101_MCSM0,    0x18,
    CC1101_FOCCFG,   0x18,
    CC1101_AGCCTRL0, 0x91,
    CC1101_AGCCTRL1, 0x00,
    CC1101_AGCCTRL2, 0x07, /* target 42 dB for wide filters */
    CC1101_WORCTRL,  0xFB,
    CC1101_FREND0,   0x11,
    CC1101_FREND1,   0xB6,
    0, 0};

#define FSK_REGS(dev) \
    { \
        CC1101_IOCFG0,   0x0D, \
        CC1101_FSCTRL1,  0x06, \
        CC1101_PKTCTRL0, 0x32, \
        CC1101_PKTCTRL1, 0x04, \
        CC1101_MDMCFG0,  0x00, \
        CC1101_MDMCFG1,  0x02, \
        CC1101_MDMCFG2,  0x04, /* 2-FSK, no sync */ \
        CC1101_MDMCFG3,  0x83, /* 4.8 kBaud */ \
        CC1101_MDMCFG4,  0x67, /* RX BW 270 kHz */ \
        CC1101_DEVIATN,  dev, \
        CC1101_MCSM0,    0x18, \
        CC1101_FOCCFG,   0x16, \
        CC1101_AGCCTRL0, 0x91, \
        CC1101_AGCCTRL1, 0x00, \
        CC1101_AGCCTRL2, 0x07, \
        CC1101_WORCTRL,  0xFB, \
        CC1101_FREND0,   0x10, \
        CC1101_FREND1,   0x56, \
        0, 0 \
    }
static const uint8_t regs_fsk_238[] = FSK_REGS(0x04); /* 2.38 kHz */
static const uint8_t regs_fsk_476[] = FSK_REGS(0x47); /* 47.6 kHz */

#define PA_OOK {0x00, 0xC0, 0, 0, 0, 0, 0, 0} /* off / +10 dBm */
#define PA_FSK {0xC0, 0, 0, 0, 0, 0, 0, 0}

const SgPreset sg_presets[] = {
    {"AM650", "FuriHalSubGhzPresetOok650Async", false, regs_ook_650, PA_OOK},
    {"AM270", "FuriHalSubGhzPresetOok270Async", false, regs_ook_270, PA_OOK},
    {"FM238", "FuriHalSubGhzPreset2FSKDev238Async", true, regs_fsk_238, PA_FSK},
    {"FM476", "FuriHalSubGhzPreset2FSKDev476Async", true, regs_fsk_476, PA_FSK},
};
const size_t sg_preset_count = ARRAY_SIZE(sg_presets);

const SgPreset* sg_preset_by_sub_name(const char* sub_name) {
    for(size_t i = 0; i < sg_preset_count; i++)
        if(strcmp(sg_presets[i].sub_name, sub_name) == 0) return &sg_presets[i];
    return NULL;
}

/* BW = fxosc / (8 * (4 + M) * 2^E) */
uint8_t sg_cc1101_rx_bw_bits(uint32_t bw_hz, uint32_t* actual) {
    uint8_t best = 0;
    uint32_t best_bw = 0, best_err = 0xFFFFFFFFu;
    for(uint8_t e = 0; e < 4; e++)
        for(uint8_t m = 0; m < 4; m++) {
            uint32_t bw = CC1101_XTAL_HZ / (8u * (4u + m) * (1u << e));
            uint32_t err = bw > bw_hz ? bw - bw_hz : bw_hz - bw;
            if(err < best_err) {
                best_err = err;
                best_bw = bw;
                best = (uint8_t)((e << 6) | (m << 4));
            }
        }
    if(actual) *actual = best_bw;
    return best;
}

/* fdev = fxosc / 2^17 * (8 + M) * 2^E */
uint8_t sg_cc1101_deviation_bits(uint32_t dev_hz, uint32_t* actual) {
    uint8_t best = 0;
    uint32_t best_dev = 0, best_err = 0xFFFFFFFFu;
    for(uint8_t e = 0; e < 8; e++)
        for(uint8_t m = 0; m < 8; m++) {
            uint32_t dev = (uint32_t)(((uint64_t)CC1101_XTAL_HZ * (8u + m) * (1u << e)) >> 17);
            uint32_t err = dev > dev_hz ? dev - dev_hz : dev_hz - dev;
            if(err < best_err) {
                best_err = err;
                best_dev = dev;
                best = (uint8_t)((e << 4) | m);
            }
        }
    if(actual) *actual = best_dev;
    return best;
}

/* rate = (256 + M) * 2^E / 2^28 * fxosc */
void sg_cc1101_data_rate_bits(uint32_t baud, uint8_t* exp, uint8_t* mant, uint32_t* actual) {
    uint32_t best_err = 0xFFFFFFFFu;
    for(uint8_t e = 0; e < 16; e++)
        for(uint32_t m = 0; m < 256; m++) {
            uint32_t rate = (uint32_t)(((uint64_t)CC1101_XTAL_HZ * (256u + m) << e) >> 28);
            uint32_t err = rate > baud ? rate - baud : baud - rate;
            if(err < best_err) {
                best_err = err;
                *exp = e;
                *mant = (uint8_t)m;
                if(actual) *actual = rate;
            }
        }
}

uint32_t sg_cc1101_freq_word(uint32_t hz) {
    return (uint32_t)((((uint64_t)hz << 16) + CC1101_XTAL_HZ / 2) / CC1101_XTAL_HZ);
}

uint32_t sg_cc1101_freq_hz(uint32_t word) {
    return (uint32_t)(((uint64_t)word * CC1101_XTAL_HZ) >> 16);
}

void sg_preset_build_custom(SgCustomPreset* out, const SgCustomParams* want) {
    const uint8_t* base = want->fsk ? regs_fsk_238 : regs_ook_270;
    uint8_t exp = 0, mant = 0;
    out->actual.fsk = want->fsk;
    uint8_t bw = sg_cc1101_rx_bw_bits(want->rx_bw_hz, &out->actual.rx_bw_hz);
    uint8_t dev = sg_cc1101_deviation_bits(want->deviation_hz, &out->actual.deviation_hz);
    sg_cc1101_data_rate_bits(want->data_rate, &exp, &mant, &out->actual.data_rate);
    size_t n = 0;
    for(; base[n] || base[n + 1]; n += 2) {
        uint8_t reg = base[n], val = base[n + 1];
        if(reg == CC1101_MDMCFG4) val = (uint8_t)(bw | exp);
        if(reg == CC1101_MDMCFG3) val = mant;
        if(reg == CC1101_DEVIATN) val = dev;
        /* wide filters need the higher AGC target, see the datasheet */
        if(reg == CC1101_AGCCTRL2) val = out->actual.rx_bw_hz > 100000 ? 0x07 : 0x03;
        out->regs[n] = reg;
        out->regs[n + 1] = val;
    }
    out->regs[n] = out->regs[n + 1] = 0;
    out->preset.name = "Custom";
    out->preset.sub_name = "FuriHalSubGhzPresetCustom";
    out->preset.fsk = want->fsk;
    out->preset.regs = out->regs;
    memset(out->preset.patable, 0, 8);
    if(want->fsk)
        out->preset.patable[0] = 0xC0;
    else
        out->preset.patable[1] = 0xC0;
}
