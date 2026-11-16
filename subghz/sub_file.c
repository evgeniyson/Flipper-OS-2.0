#include "sub_file.h"
#include "../core/fmt.h"
#include <string.h>

enum { ST_LINE_START, ST_KEY, ST_VALUE_START, ST_VALUE, ST_RAW, ST_SKIP };

void sub_parser_init(SubParser* p, SubPulseCb cb, void* ctx) {
    memset(p, 0, sizeof(*p));
    p->cb = cb;
    p->ctx = ctx;
    p->state = ST_LINE_START;
}

static uint32_t parse_u32(const char* s) {
    uint32_t v = 0;
    while(*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "AA BB CC" -> bytes; returns the count */
static size_t parse_hex_bytes(const char* s, uint8_t* out, size_t max) {
    size_t n = 0;
    while(*s && n < max) {
        int hi = hex_nibble(s[0]);
        if(hi < 0) {
            s++;
            continue;
        }
        int lo = hex_nibble(s[1]);
        if(lo < 0) break;
        out[n++] = (uint8_t)(hi << 4 | lo);
        s += 2;
    }
    return n;
}

static void copy_str(char* dst, size_t size, const char* src) {
    size_t n = strlen(src);
    if(n >= size) n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void handle_kv(SubParser* p) {
    SubInfo* in = &p->info;
    const char *k = p->key, *v = p->val;
    if(strcmp(k, "Filetype") == 0) {
        in->filetype_ok = strncmp(v, "Flipper SubGhz", 14) == 0;
        in->is_raw = strstr(v, "RAW") != NULL;
    } else if(strcmp(k, "Frequency") == 0) {
        in->frequency = parse_u32(v);
    } else if(strcmp(k, "Preset") == 0) {
        copy_str(in->preset, sizeof(in->preset), v);
    } else if(strcmp(k, "Protocol") == 0) {
        copy_str(in->protocol, sizeof(in->protocol), v);
        if(strcmp(v, "RAW") == 0) in->is_raw = true;
    } else if(strcmp(k, "Bit") == 0) {
        in->bits = (uint8_t)parse_u32(v);
    } else if(strcmp(k, "TE") == 0) {
        in->te = parse_u32(v);
    } else if(strcmp(k, "Key") == 0) {
        uint8_t b[8];
        size_t n = parse_hex_bytes(v, b, sizeof(b));
        in->key = 0;
        for(size_t i = 0; i < n; i++) in->key = in->key << 8 | b[i];
    } else if(strcmp(k, "Custom_preset_data") == 0) {
        in->custom_len = (uint8_t)parse_hex_bytes(v, in->custom, sizeof(in->custom));
    }
}

static void raw_number_done(SubParser* p) {
    if(!p->num_any) return;
    sg_pulse_t v = p->num_neg ? -p->num : p->num;
    p->info.pulse_count++;
    if(p->cb && v) p->cb(v, p->ctx);
    p->num = 0;
    p->num_neg = p->num_any = false;
}

void sub_parser_feed(SubParser* p, const char* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        char c = data[i];
        if(c == '\r') continue;
        switch(p->state) {
        case ST_LINE_START:
            if(c == '\n' || c == ' ') break;
            if(c == '#') {
                p->state = ST_SKIP;
                break;
            }
            p->key_len = 0;
            p->state = ST_KEY;
            /* fallthrough */
        case ST_KEY:
            if(c == ':') {
                p->key[p->key_len] = '\0';
                p->val_len = 0;
                p->state = strcmp(p->key, "RAW_Data") == 0 ? ST_RAW : ST_VALUE_START;
            } else if(c == '\n') {
                p->state = ST_LINE_START;
            } else if((size_t)p->key_len + 1 < sizeof(p->key)) {
                p->key[p->key_len++] = c;
            }
            break;
        case ST_VALUE_START:
            if(c == ' ') break;
            p->state = ST_VALUE;
            /* fallthrough */
        case ST_VALUE:
            if(c == '\n') {
                while(p->val_len && p->val[p->val_len - 1] == ' ') p->val_len--;
                p->val[p->val_len] = '\0';
                handle_kv(p);
                p->state = ST_LINE_START;
            } else if((size_t)p->val_len + 1 < sizeof(p->val)) {
                p->val[p->val_len++] = c;
            }
            break;
        case ST_RAW:
            if(c >= '0' && c <= '9') {
                if(p->num < 100000000) p->num = p->num * 10 + (c - '0');
                p->num_any = true;
            } else if(c == '-') {
                p->num_neg = true;
            } else {
                raw_number_done(p);
                if(c == '\n') p->state = ST_LINE_START;
            }
            break;
        case ST_SKIP:
            if(c == '\n') p->state = ST_LINE_START;
            break;
        }
    }
}

void sub_parser_finish(SubParser* p) {
    if(p->state == ST_RAW) raw_number_done(p);
    if(p->state == ST_VALUE || p->state == ST_VALUE_START) {
        p->val[p->val_len] = '\0';
        handle_kv(p);
    }
    p->state = ST_LINE_START;
}

bool sub_info_to_decoded(const SubInfo* info, SgDecoded* out) {
    const SgProtocol* proto = sg_protocol_by_name(info->protocol);
    if(!proto || info->is_raw || !info->bits) return false;
    memset(out, 0, sizeof(*out));
    out->proto = proto;
    out->data = info->key;
    out->bits = info->bits;
    out->te = (uint16_t)info->te;
    sg_static_fill_fields(out);
    return true;
}

/* ------------------------------------------------------------ writer */

static void put(SubWriter* w, const char* s) {
    if(!w->ok) return;
    size_t n = strlen(s);
    if(w->write(w->ctx, s, n) != (int)n) w->ok = false;
}

static void header(SubWriter* w, const char* type, uint32_t frequency, const char* preset) {
    char line[96];
    fmt_snprintf(line, sizeof(line), "Filetype: Flipper SubGhz %s File\nVersion: 1\n", type);
    put(w, line);
    fmt_snprintf(line, sizeof(line), "Frequency: %lu\n", (unsigned long)frequency);
    put(w, line);
    fmt_snprintf(line, sizeof(line), "Preset: %s\n", preset);
    put(w, line);
}

bool sub_writer_begin_raw(SubWriter* w, SubWriteFn fn, void* ctx, uint32_t frequency, const char* preset) {
    memset(w, 0, sizeof(*w));
    w->write = fn;
    w->ctx = ctx;
    w->ok = true;
    header(w, "RAW", frequency, preset);
    put(w, "Protocol: RAW\n");
    return w->ok;
}

static void emit(SubWriter* w, sg_pulse_t v) {
    char num[16];
    if(w->in_line == 0) put(w, "RAW_Data:");
    fmt_snprintf(num, sizeof(num), " %ld", (long)v);
    put(w, num);
    w->total++;
    if(++w->in_line >= 512) {
        put(w, "\n");
        w->in_line = 0;
    }
}

void sub_writer_pulse(SubWriter* w, sg_pulse_t pulse) {
    if(pulse == 0) return;
    /* merge runs of the same level so the file strictly alternates */
    if(w->pending && (w->pending > 0) == (pulse > 0)) {
        w->pending += pulse;
        return;
    }
    if(w->pending) emit(w, w->pending);
    w->pending = pulse;
}

bool sub_writer_end(SubWriter* w) {
    if(w->pending) emit(w, w->pending);
    w->pending = 0;
    if(w->in_line) put(w, "\n");
    w->in_line = 0;
    return w->ok;
}

bool sub_write_key(SubWriteFn fn, void* ctx, uint32_t frequency, const char* preset, const SgDecoded* d) {
    SubWriter w = {.write = fn, .ctx = ctx, .ok = true};
    char line[96];
    header(&w, "Key", frequency, preset);
    fmt_snprintf(line, sizeof(line), "Protocol: %s\nBit: %u\n", d->proto->name, d->bits);
    put(&w, line);
    fmt_snprintf(
        line, sizeof(line), "Key: %02X %02X %02X %02X %02X %02X %02X %02X\n",
        (unsigned)(d->data >> 56) & 0xFF, (unsigned)(d->data >> 48) & 0xFF,
        (unsigned)(d->data >> 40) & 0xFF, (unsigned)(d->data >> 32) & 0xFF,
        (unsigned)(d->data >> 24) & 0xFF, (unsigned)(d->data >> 16) & 0xFF,
        (unsigned)(d->data >> 8) & 0xFF, (unsigned)d->data & 0xFF);
    put(&w, line);
    fmt_snprintf(line, sizeof(line), "TE: %u\n", d->te);
    put(&w, line);
    return w.ok;
}
