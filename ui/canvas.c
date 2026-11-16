#include "canvas.h"
#include "../core/fmt.h"
#include <stdarg.h>
#include <string.h>

static uint8_t fb[CV_W * CV_H / 8];

void cv_clear(void) {
    memset(fb, 0, sizeof(fb));
}

const uint8_t* cv_buffer(void) {
    return fb;
}

void cv_pixel(int x, int y, bool on) {
    if((unsigned)x >= CV_W || (unsigned)y >= CV_H) return;
    uint8_t* b = &fb[(y >> 3) * CV_W + x];
    uint8_t m = (uint8_t)(1u << (y & 7));
    if(on)
        *b |= m;
    else
        *b &= (uint8_t)~m;
}

bool cv_get(int x, int y) {
    if((unsigned)x >= CV_W || (unsigned)y >= CV_H) return false;
    return fb[(y >> 3) * CV_W + x] >> (y & 7) & 1;
}

void cv_hline(int x, int y, int w) {
    for(int i = 0; i < w; i++) cv_pixel(x + i, y, true);
}

void cv_vline(int x, int y, int h) {
    for(int i = 0; i < h; i++) cv_pixel(x, y + i, true);
}

void cv_dotted_hline(int x, int y, int w) {
    for(int i = 0; i < w; i += 2) cv_pixel(x + i, y, true);
}

void cv_line(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for(;;) {
        cv_pixel(x0, y0, true);
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if(e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if(e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void cv_rect(int x, int y, int w, int h) {
    if(w <= 0 || h <= 0) return;
    cv_hline(x, y, w);
    cv_hline(x, y + h - 1, w);
    cv_vline(x, y, h);
    cv_vline(x + w - 1, y, h);
}

void cv_fill(int x, int y, int w, int h, bool on) {
    for(int j = 0; j < h; j++)
        for(int i = 0; i < w; i++) cv_pixel(x + i, y + j, on);
}

void cv_invert(int x, int y, int w, int h) {
    for(int j = 0; j < h; j++)
        for(int i = 0; i < w; i++) cv_pixel(x + i, y + j, !cv_get(x + i, y + j));
}

int cv_text(int x, int y, const FontDef* f, const char* s) {
    for(; *s; s++, x += f->width) {
        unsigned c = (unsigned char)*s;
        if(c < f->first || c >= (unsigned)f->first + f->count) c = '?';
        unsigned idx = (c - f->first) * f->height;
        for(int row = 0; row < f->height; row++) {
            uint16_t bits = f->wide ? ((const uint16_t*)f->rows)[idx + row] :
                                      (uint16_t)(((const uint8_t*)f->rows)[idx + row] << 8);
            for(int col = 0; bits; col++, bits = (uint16_t)(bits << 1))
                if(bits & 0x8000) cv_pixel(x + col, y + row, true);
        }
    }
    return x;
}

int cv_text_width(const FontDef* f, const char* s) {
    return (int)strlen(s) * f->width;
}

void cv_text_right(int x_right, int y, const FontDef* f, const char* s) {
    cv_text(x_right - cv_text_width(f, s), y, f, s);
}

void cv_text_center(int x_center, int y, const FontDef* f, const char* s) {
    cv_text(x_center - cv_text_width(f, s) / 2, y, f, s);
}

int cv_printf(int x, int y, const FontDef* f, const char* fmt, ...) {
    char buf[48];
    va_list ap;
    va_start(ap, fmt);
    fmt_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return cv_text(x, y, f, buf);
}
