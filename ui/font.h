#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Fixed-cell bitmap font: `height` rows per glyph, MSB = leftmost pixel. */
typedef struct {
    uint8_t width, height;
    uint8_t first, count;
    bool wide; /* rows are uint16_t instead of uint8_t */
    const void* rows;
} FontDef;

extern const FontDef font_small; /* 4x6 */
extern const FontDef font_normal; /* 5x8 */
extern const FontDef font_bold; /* 6x13 bold */
extern const FontDef font_big; /* 9x15 bold */
