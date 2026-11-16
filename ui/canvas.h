#pragma once
#include "font.h"
#include <stddef.h>

/* 128x64 monochrome frame buffer and drawing primitives.
 * The buffer is laid out exactly as the ST7567 expects it: 8 pages of 128
 * column bytes, bit 0 of each byte being the top pixel of the page. */
#define CV_W 128
#define CV_H 64

void cv_clear(void);
const uint8_t* cv_buffer(void);

void cv_pixel(int x, int y, bool on);
bool cv_get(int x, int y);
void cv_hline(int x, int y, int w);
void cv_vline(int x, int y, int h);
void cv_line(int x0, int y0, int x1, int y1);
void cv_rect(int x, int y, int w, int h);
void cv_fill(int x, int y, int w, int h, bool on);
void cv_invert(int x, int y, int w, int h);
void cv_dotted_hline(int x, int y, int w);

/* Text: (x, y) is the top-left corner of the first cell. Returns the x after
 * the last character. Pixels are only set, never cleared. */
int cv_text(int x, int y, const FontDef* font, const char* s);
int cv_text_width(const FontDef* font, const char* s);
void cv_text_right(int x_right, int y, const FontDef* font, const char* s);
void cv_text_center(int x_center, int y, const FontDef* font, const char* s);
int cv_printf(int x, int y, const FontDef* font, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));
