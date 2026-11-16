#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A radio configuration: register/value pairs terminated by {0,0} and the
 * 8-byte power table. Layout matches the "Custom preset" data of .sub files. */
typedef struct {
    const char* name; /* short label for the UI */
    const char* sub_name; /* name stored in .sub files */
    bool fsk;
    const uint8_t* regs;
    uint8_t patable[8];
} SgPreset;

extern const SgPreset sg_presets[];
extern const size_t sg_preset_count;
const SgPreset* sg_preset_by_sub_name(const char* sub_name);

/* User-tunable preset for exploring unknown signals. */
typedef struct {
    bool fsk;
    uint32_t rx_bw_hz;
    uint32_t deviation_hz;
    uint32_t data_rate;
} SgCustomParams;

#define SG_CUSTOM_REGS_MAX 48
typedef struct {
    uint8_t regs[SG_CUSTOM_REGS_MAX];
    SgPreset preset;
    SgCustomParams actual; /* what the chip can really do */
} SgCustomPreset;

void sg_preset_build_custom(SgCustomPreset* out, const SgCustomParams* want);

/* Register encoders; *actual receives the value the chip will really use. */
uint8_t sg_cc1101_rx_bw_bits(uint32_t bw_hz, uint32_t* actual); /* MDMCFG4[7:4] */
uint8_t sg_cc1101_deviation_bits(uint32_t dev_hz, uint32_t* actual); /* DEVIATN */
void sg_cc1101_data_rate_bits(uint32_t baud, uint8_t* exp, uint8_t* mant, uint32_t* actual);
uint32_t sg_cc1101_freq_word(uint32_t hz); /* FREQ2:FREQ1:FREQ0 */
uint32_t sg_cc1101_freq_hz(uint32_t word);
