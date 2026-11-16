#include "region.h"
#include "../core/util.h"

typedef struct {
    uint32_t start, end;
} Band;

static const Band tunable[] = {
    {299999755, 348000335},
    {386999938, 464000000},
    {778999847, 928000000},
};

int sg_freq_band(uint32_t hz) {
    for(int i = 0; i < (int)ARRAY_SIZE(tunable); i++)
        if(hz >= tunable[i].start && hz <= tunable[i].end) return i;
    return -1;
}

bool sg_freq_valid(uint32_t hz) {
    return sg_freq_band(hz) >= 0;
}

static const Band eu_ru[] = {{433050000, 434790000}, {868150000, 868550000}, {0, 0}};
static const Band us_ca_au[] = {
    {304100000, 321950000}, {433050000, 434790000}, {915000000, 928000000}, {0, 0}};
static const Band jp[] = {{312000000, 315250000}, {920500000, 923500000}, {0, 0}};

static const struct {
    const char* name;
    const Band* bands;
} regions[SG_REGION_COUNT] = {
    [SG_REGION_NONE] = {"Not set", NULL},
    [SG_REGION_EU_RU] = {"EU/RU", eu_ru},
    [SG_REGION_US_CA_AU] = {"US/CA/AU", us_ca_au},
    [SG_REGION_JP] = {"JP", jp},
};

bool sg_region_tx_allowed(SgRegion region, uint32_t hz) {
    if(region <= SG_REGION_NONE || region >= SG_REGION_COUNT) return false;
    if(!sg_freq_valid(hz)) return false;
    for(const Band* b = regions[region].bands; b->end; b++)
        if(hz >= b->start && hz <= b->end) return true;
    return false;
}

const char* sg_region_name(SgRegion region) {
    return region < SG_REGION_COUNT ? regions[region].name : "?";
}

const uint32_t sg_freq_list[] = {
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000, 390000000,
    418000000, 433075000, 433420000, 433920000, 434420000, 434775000, 438900000,
    868350000, 868950000, 915000000, 925000000,
};
const size_t sg_freq_list_count = ARRAY_SIZE(sg_freq_list);

const uint32_t sg_freq_hop_list[] = {310000000, 315000000, 318000000, 390000000, 433920000, 868350000};
const size_t sg_freq_hop_list_count = ARRAY_SIZE(sg_freq_hop_list);
