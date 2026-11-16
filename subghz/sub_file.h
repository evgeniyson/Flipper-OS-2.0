#pragma once
#include "protocol.h"

/* Reader and writer for Flipper ".sub" files, both flavours:
 *   "Flipper SubGhz RAW File" - signed pulse durations
 *   "Flipper SubGhz Key File" - a decoded key of a known protocol
 * The reader is a streaming state machine: feed it file chunks of any size. */

typedef struct {
    bool filetype_ok;
    bool is_raw;
    uint32_t frequency;
    char preset[48];
    char protocol[32];
    uint8_t bits;
    uint64_t key;
    uint32_t te;
    uint8_t custom[64]; /* Custom_preset_data: register pairs + power table */
    uint8_t custom_len;
    uint32_t pulse_count;
} SubInfo;

typedef void (*SubPulseCb)(sg_pulse_t pulse, void* ctx);

typedef struct {
    SubInfo info;
    SubPulseCb cb;
    void* ctx;
    uint8_t state;
    char key[24];
    uint8_t key_len;
    char val[224];
    uint8_t val_len;
    int32_t num;
    bool num_neg, num_any;
} SubParser;

void sub_parser_init(SubParser* p, SubPulseCb cb, void* ctx);
void sub_parser_feed(SubParser* p, const char* data, size_t len);
void sub_parser_finish(SubParser* p);
/* Rebuild a decoded signal from a key file header. False if the protocol is unknown. */
bool sub_info_to_decoded(const SubInfo* info, SgDecoded* out);

/* Returns the number of bytes written, or a negative value on error. */
typedef int (*SubWriteFn)(void* ctx, const char* data, size_t len);

typedef struct {
    SubWriteFn write;
    void* ctx;
    bool ok;
    uint16_t in_line;
    sg_pulse_t pending;
    uint32_t total;
} SubWriter;

bool sub_writer_begin_raw(SubWriter* w, SubWriteFn fn, void* ctx, uint32_t frequency, const char* preset);
void sub_writer_pulse(SubWriter* w, sg_pulse_t pulse);
bool sub_writer_end(SubWriter* w);
bool sub_write_key(SubWriteFn fn, void* ctx, uint32_t frequency, const char* preset, const SgDecoded* d);
