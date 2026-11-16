#include "fmt.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char* buf;
    size_t size;
    size_t len;
} Out;

static void put(Out* o, char c) {
    if(o->len + 1 < o->size) o->buf[o->len] = c;
    o->len++;
}

static void put_padded(Out* o, const char* s, size_t n, int width, bool left, char pad) {
    int fill = width > (int)n ? width - (int)n : 0;
    if(!left)
        while(fill-- > 0) put(o, pad);
    for(size_t i = 0; i < n; i++) put(o, s[i]);
    if(left)
        while(fill-- > 0) put(o, ' ');
}

static size_t utoa_rev(unsigned long long v, unsigned base, bool upper, char* tmp) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while(v);
    return n;
}

int fmt_vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    Out o = {buf, size, 0};
    for(; *fmt; fmt++) {
        if(*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false, zero = false;
        for(;; fmt++) {
            if(*fmt == '-')
                left = true;
            else if(*fmt == '0')
                zero = true;
            else
                break;
        }
        int width = 0;
        if(*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        }
        while(*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int lng = 0;
        while(*fmt == 'l') {
            lng++;
            fmt++;
        }
        char tmp[24], num[26];
        switch(*fmt) {
        case '%':
            put(&o, '%');
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            put_padded(&o, &c, 1, width, left, ' ');
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if(!s) s = "(null)";
            size_t n = 0;
            while(s[n]) n++;
            put_padded(&o, s, n, width, left, ' ');
            break;
        }
        case 'd':
        case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) :
                          lng == 1 ? va_arg(ap, long) :
                                     va_arg(ap, int);
            bool neg = v < 0;
            unsigned long long u = neg ? 0ULL - (unsigned long long)v : (unsigned long long)v;
            size_t n = utoa_rev(u, 10, false, tmp);
            size_t k = 0;
            if(neg) num[k++] = '-';
            if(zero && !left) {
                /* zero padding goes between the sign and the digits */
                int fill = width - (int)(n + k);
                while(fill-- > 0 && k < sizeof(num) - n) num[k++] = '0';
            }
            while(n) num[k++] = tmp[--n];
            put_padded(&o, num, k, width, left, ' ');
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) :
                                   lng == 1 ? va_arg(ap, unsigned long) :
                                              va_arg(ap, unsigned int);
            size_t n = utoa_rev(v, *fmt == 'u' ? 10 : 16, *fmt == 'X', tmp);
            size_t k = 0;
            while(n) num[k++] = tmp[--n];
            put_padded(&o, num, k, width, left, (zero && !left) ? '0' : ' ');
            break;
        }
        case '\0':
            fmt--;
            break;
        default:
            put(&o, '%');
            put(&o, *fmt);
            break;
        }
    }
    if(size) buf[o.len < size ? o.len : size - 1] = '\0';
    return (int)o.len;
}

int fmt_snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = fmt_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
