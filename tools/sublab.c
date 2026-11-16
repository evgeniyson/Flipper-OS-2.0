/* sublab - analyse Flipper .sub captures on a PC with the firmware's own
 * Sub-GHz engine.
 *
 *   sublab [-a] [-p] file.sub...
 *     -a   analyse every packet, including ones a decoder already recognised
 *     -p   dump the pulse durations of every packet
 */
#include "subghz/analyzer.h"
#include "subghz/presets.h"
#include "subghz/protocol.h"
#include "subghz/region.h"
#include "subghz/sub_file.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    bool analyse_all, dump;
    int packets, decoded, unknown;
    SgDecoded last;
    int repeats;
    bool have_last;
    uint8_t prev_bits[SG_AN_MAX_BITS / 8];
    int prev_n_bits;
} Ctx;

static SgPacketizer packetizer;

static void flush_repeats(Ctx* c) {
    if(!c->have_last) return;
    char text[256];
    sg_decoded_describe(&c->last, text, sizeof(text));
    for(char* s = text; *s; s++)
        if(*s == '\n') *s = ' ';
    printf("  [%s] %s  x%d\n",
           c->last.proto->cls == SG_CLASS_STATIC ? "static" :
           c->last.proto->cls == SG_CLASS_DYNAMIC ? "rolling" : "sensor",
           text, c->repeats);
    c->have_last = false;
}

static void print_clusters(const char* label, const SgCluster* cl, int n) {
    printf("    %s:", label);
    for(int i = 0; i < n; i++) printf(" %uus x%u", cl[i].us, cl[i].count);
    printf("\n");
}

static void on_packet(const SgPacket* pkt, void* ctx) {
    Ctx* c = ctx;
    c->packets++;
    SgDecoded d;
    bool ok = sg_decode_packet(pkt, &d);
    if(ok) {
        c->decoded++;
        if(c->have_last && c->last.proto == d.proto && c->last.data == d.data) {
            c->repeats++;
        } else {
            flush_repeats(c);
            c->last = d;
            c->repeats = 1;
            c->have_last = true;
        }
    } else {
        c->unknown++;
    }
    if(c->dump) {
        printf("  packet %d: %u pulses, gap before %u us, after %u us\n   ", c->packets, pkt->n,
               (unsigned)pkt->gap_before, (unsigned)pkt->gap_after);
        for(int i = 0; i < pkt->n; i++) printf(" %s%u", i & 1 ? "-" : "", pkt->d[i]);
        printf("\n");
    }
    if(ok && !c->analyse_all) return;
    if(!ok) flush_repeats(c);

    static SgAnalysis a;
    sg_analyze(pkt, &a);
    printf("  packet %d: %u pulses, %lu us%s\n", c->packets, pkt->n, (unsigned long)a.length_us,
           pkt->truncated ? " (truncated)" : "");
    print_clusters("high", a.hi, a.n_hi);
    print_clusters("low ", a.lo, a.n_lo);
    printf("    TE ~%u us (%lu baud), encoding: %s\n", a.te, (unsigned long)a.baud,
           sg_encoding_name(a.enc));
    if(a.n_bits) {
        char hex[SG_AN_MAX_BITS / 4 + 2];
        sg_bits_to_hex(a.bits, a.n_bits, hex, sizeof(hex));
        printf("    %u bits: %s\n", a.n_bits, hex);
        if(c->prev_n_bits == a.n_bits) {
            uint8_t mask[SG_AN_MAX_BITS / 8];
            int diff = sg_bits_diff(a.bits, c->prev_bits, a.n_bits, mask);
            if(diff == 0) {
                printf("    same as previous packet\n");
            } else {
                sg_bits_to_hex(mask, a.n_bits, hex, sizeof(hex));
                printf("    %d bits differ from previous, mask: %s\n", diff, hex);
            }
        }
        memcpy(c->prev_bits, a.bits, sizeof(c->prev_bits));
        c->prev_n_bits = a.n_bits;
    }
}

static void on_pulse(sg_pulse_t p, void* ctx) {
    (void)ctx;
    sg_packetizer_feed(&packetizer, p);
}

static int run(const char* path, Ctx* c) {
    FILE* f = fopen(path, "rb");
    if(!f) {
        perror(path);
        return 1;
    }
    static SubParser parser;
    sub_parser_init(&parser, on_pulse, NULL);
    sg_packetizer_init(&packetizer, 0, on_packet, c);
    printf("%s\n", path);
    char buf[4096];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0) sub_parser_feed(&parser, buf, n);
    fclose(f);
    sub_parser_finish(&parser);
    sg_packetizer_flush(&packetizer);
    flush_repeats(c);

    const SubInfo* in = &parser.info;
    if(!in->filetype_ok) printf("  warning: not a Flipper SubGhz file\n");
    printf("  %lu.%03lu MHz, preset %s, %s\n", (unsigned long)(in->frequency / 1000000),
           (unsigned long)(in->frequency / 1000 % 1000), in->preset[0] ? in->preset : "?",
           in->is_raw ? "RAW" : in->protocol);
    if(in->is_raw) {
        printf("  %lu pulses, %d packets: %d decoded, %d unknown\n", (unsigned long)in->pulse_count,
               c->packets, c->decoded, c->unknown);
    } else {
        SgDecoded d;
        if(sub_info_to_decoded(in, &d)) {
            char text[256];
            sg_decoded_describe(&d, text, sizeof(text));
            printf("%s\n", text);
        } else {
            printf("  key file of an unsupported protocol\n");
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    Ctx base = {0};
    int rc = 0, files = 0;
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-a") == 0)
            base.analyse_all = true;
        else if(strcmp(argv[i], "-p") == 0)
            base.dump = true;
        else {
            Ctx c = base;
            rc |= run(argv[i], &c);
            files++;
        }
    }
    if(!files) {
        fprintf(stderr, "usage: sublab [-a] [-p] file.sub...\n");
        return 2;
    }
    return rc;
}
