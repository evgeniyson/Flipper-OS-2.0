#include "settings.h"
#include "../core/fmt.h"
#include "../hal/hal.h"
#include "../hal/radio.h"
#include "../hal/storage.h"
#include "../subghz/region.h"
#include <string.h>

#define SETTINGS_DIR  "/fos"
#define SETTINGS_FILE "/fos/settings.txt"

Settings settings = {
    .region = SG_REGION_NONE,
    .radio_mode = RADIO_MODE_AUTO,
    .ext_5v = false,
    .backlight = 7,
    .contrast = 0,
    .frequency = 433920000,
    .preset = 0,
    .hopping = false,
};

static const struct {
    const char* key;
    uint8_t kind; /* 0: u8, 1: i8, 2: u32, 3: bool */
    void* ptr;
    int32_t min, max;
} fields[] = {
    {"region", 0, &settings.region, 0, SG_REGION_COUNT - 1},
    {"radio", 0, &settings.radio_mode, 0, RADIO_MODE_COUNT - 1},
    {"ext_5v", 3, &settings.ext_5v, 0, 1},
    {"backlight", 0, &settings.backlight, 0, 10},
    {"contrast", 1, &settings.contrast, -8, 8},
    {"frequency", 2, &settings.frequency, 0, 0},
    {"preset", 0, &settings.preset, 0, 3},
    {"hopping", 3, &settings.hopping, 0, 1},
};
#define N_FIELDS (sizeof(fields) / sizeof(fields[0]))

static void set_field(const char* key, const char* val) {
    for(size_t i = 0; i < N_FIELDS; i++) {
        if(strcmp(fields[i].key, key) != 0) continue;
        bool neg = *val == '-';
        if(neg) val++;
        uint32_t u = 0;
        while(*val >= '0' && *val <= '9') u = u * 10 + (uint32_t)(*val++ - '0');
        if(fields[i].kind == 2) {
            if(sg_freq_valid(u)) *(uint32_t*)fields[i].ptr = u;
            return;
        }
        int32_t v = neg ? -(int32_t)u : (int32_t)u;
        if(v < fields[i].min || v > fields[i].max) return; /* keep the default */
        if(fields[i].kind == 1)
            *(int8_t*)fields[i].ptr = (int8_t)v;
        else if(fields[i].kind == 3)
            *(bool*)fields[i].ptr = v != 0;
        else
            *(uint8_t*)fields[i].ptr = (uint8_t)v;
        return;
    }
}

void settings_load(void) {
    if(!hal_storage_ready()) return;
    HalFile* f = hal_file_open(SETTINGS_FILE, "r");
    if(!f) return;
    char buf[400];
    int n = hal_file_read(f, buf, sizeof(buf) - 1);
    hal_file_close(f);
    if(n <= 0) return;
    buf[n] = '\0';
    char* line = buf;
    while(*line) {
        char* end = strchr(line, '\n');
        if(end) *end = '\0';
        char* eq = strchr(line, '=');
        if(eq) {
            *eq = '\0';
            set_field(line, eq + 1);
        }
        if(!end) break;
        line = end + 1;
    }
}

bool settings_save(void) {
    if(!hal_storage_ready()) return false;
    hal_mkdir(SETTINGS_DIR);
    HalFile* f = hal_file_open(SETTINGS_FILE, "w");
    if(!f) return false;
    bool ok = true;
    for(size_t i = 0; i < N_FIELDS; i++) {
        char line[40];
        long v = fields[i].kind == 2 ? (long)*(uint32_t*)fields[i].ptr :
                 fields[i].kind == 1 ? (long)*(int8_t*)fields[i].ptr :
                 fields[i].kind == 3 ? (long)*(bool*)fields[i].ptr :
                                       (long)*(uint8_t*)fields[i].ptr;
        int n = fmt_snprintf(line, sizeof(line), "%s=%ld\n", fields[i].key, v);
        ok &= hal_file_write(f, line, (size_t)n) == n;
    }
    hal_file_close(f);
    return ok;
}

void settings_apply(void) {
    hal_display_contrast(settings.contrast);
    hal_backlight((uint8_t)(settings.backlight * 255 / 10));
    hal_ext_5v(settings.ext_5v);
    /* give an amplified module time to power up before probing it */
    if(settings.ext_5v) hal_delay_ms(20);
    RadioId want = RADIO_INTERNAL;
    if(settings.radio_mode == RADIO_MODE_EXTERNAL ||
       (settings.radio_mode == RADIO_MODE_AUTO && radio_present(RADIO_EXTERNAL)))
        want = RADIO_EXTERNAL;
    if(!radio_select(want)) radio_select(RADIO_INTERNAL);
}
