#pragma once
#include <stdarg.h>
#include <stddef.h>

/* Small, allocation-free printf subset used on both the MCU and the host.
 * Supports: %% %c %s %d %i %u %x %X, length modifiers l / ll,
 * field width, '0' padding and '-' left alignment. No floating point. */
int fmt_vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int fmt_snprintf(char* buf, size_t size, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
