#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frequencies the radio front-end can tune to at all (three antenna paths). */
bool sg_freq_valid(uint32_t hz);
/* 0: 300-348 MHz, 1: 387-464 MHz, 2: 779-928 MHz, -1: not tunable */
int sg_freq_band(uint32_t hz);

/* Transmission is only allowed inside the licence-free bands of the region
 * selected by the user. With no region selected nothing is transmitted. */
typedef enum {
    SG_REGION_NONE,
    SG_REGION_EU_RU,
    SG_REGION_US_CA_AU,
    SG_REGION_JP,
    SG_REGION_COUNT,
} SgRegion;

bool sg_region_tx_allowed(SgRegion region, uint32_t hz);
const char* sg_region_name(SgRegion region);

/* Frequencies commonly used by remotes and sensors. */
extern const uint32_t sg_freq_list[];
extern const size_t sg_freq_list_count;
extern const uint32_t sg_freq_hop_list[];
extern const size_t sg_freq_hop_list_count;
