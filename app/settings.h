#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { RADIO_MODE_AUTO, RADIO_MODE_INTERNAL, RADIO_MODE_EXTERNAL, RADIO_MODE_COUNT } RadioMode;

typedef struct {
    uint8_t region; /* SgRegion; transmission stays disabled until this is set */
    uint8_t radio_mode; /* RadioMode */
    bool ext_5v;
    uint8_t backlight; /* 0..10 */
    int8_t contrast; /* -8..8 */
    uint32_t frequency;
    uint8_t preset; /* index into sg_presets */
    bool hopping;
} Settings;

extern Settings settings;

void settings_load(void); /* from the SD card; defaults if there is none */
bool settings_save(void);
/* Push display and radio-selection settings to the hardware. */
void settings_apply(void);
