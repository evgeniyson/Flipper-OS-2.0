/* Signal analyser: what can be said about one packet without knowing its
 * protocol. Three pages: summary, duration histogram, extracted bits. */
#include "apps.h"

#define HIST_BINS 62
#define HIST_MIN  50
#define HIST_MAX  20000

static SgPacket packet;
static SgAnalysis an;
static SgDecoded decoded;
static bool is_decoded;
static int page;

static void clusters(char* buf, size_t size, const char* label, const SgCluster* c, int n) {
    int len = fmt_snprintf(buf, size, "%s", label);
    for(int i = 0; i < n && i < 4 && (size_t)len < size; i++)
        len += fmt_snprintf(buf + len, size - (size_t)len, " %u", c[i].us);
}

static void draw_summary(void) {
    char line[48];
    fmt_snprintf(line, sizeof(line), "%u pulses  %lu.%lu ms", packet.n,
                 (unsigned long)(an.length_us / 1000), (unsigned long)(an.length_us % 1000 / 100));
    cv_text(2, 11, &font_normal, line);
    fmt_snprintf(line, sizeof(line), "TE %uus  %lu Bd", an.te, (unsigned long)an.baud);
    cv_text(2, 20, &font_normal, line);
    fmt_snprintf(line, sizeof(line), "%s  %u bits", sg_encoding_name(an.enc), an.n_bits);
    cv_text(2, 29, &font_normal, line);
    clusters(line, sizeof(line), "Hi:", an.hi, an.n_hi);
    cv_text(2, 39, &font_small, line);
    clusters(line, sizeof(line), "Lo:", an.lo, an.n_lo);
    cv_text(2, 46, &font_small, line);
    if(is_decoded) {
        fmt_snprintf(line, sizeof(line), "= %s %ubit", decoded.proto->name, decoded.bits);
        cv_text(2, 53, &font_small, line);
    }
}

static void draw_hist_row(int y, int h, bool high) {
    uint16_t bins[HIST_BINS];
    sg_histogram(&packet, high, bins, HIST_BINS, HIST_MIN, HIST_MAX);
    uint16_t max = 1;
    for(int i = 0; i < HIST_BINS; i++) max = MAX(max, bins[i]);
    for(int i = 0; i < HIST_BINS; i++) {
        if(!bins[i]) continue;
        int bar = 1 + bins[i] * (h - 1) / max;
        cv_fill(2 + i * 2, y + h - bar, 2, bar, true);
    }
    cv_hline(2, y + h, HIST_BINS * 2);
    cv_text_right(CV_W, y + h / 2 - 3, &font_small, high ? "H" : "L");
}

static void draw_histogram(void) {
    draw_hist_row(11, 18, true);
    draw_hist_row(32, 18, false);
    /* logarithmic time axis */
    static const uint16_t marks[] = {100, 1000, 10000};
    static const char* const labels[] = {"100u", "1ms", "10ms"};
    for(int m = 0; m < 3; m++) {
        int bin = 0;
        while(bin < HIST_BINS && sg_histogram_bin_us(bin, HIST_BINS, HIST_MIN, HIST_MAX) < marks[m]) bin++;
        cv_vline(2 + bin * 2, 51, 3);
        cv_text(2 + bin * 2 - 6, 56, &font_small, labels[m]);
    }
}

static void draw_bits(void) {
    char hex[SG_AN_MAX_BITS / 4 + 2], line[32];
    fmt_snprintf(line, sizeof(line), "%u bits, %s", an.n_bits, sg_encoding_name(an.enc));
    cv_text(2, 11, &font_small, line);
    if(!an.n_bits) {
        cv_text(2, 26, &font_normal, "No bit pattern found");
        return;
    }
    int n = sg_bits_to_hex(an.bits, an.n_bits, hex, sizeof(hex));
    /* 24 hex digits per row in groups of four */
    for(int rowi = 0; rowi * 24 < n && rowi < 6; rowi++) {
        int x = 2;
        for(int i = 0; i < 24 && rowi * 24 + i < n; i++) {
            char c[2] = {hex[rowi * 24 + i], 0};
            x = cv_text(x, 19 + rowi * 7, &font_small, c);
            if(i % 4 == 3) x += 3;
        }
    }
}

static void draw(void) {
    if(page == 0)
        draw_summary();
    else if(page == 1)
        draw_histogram();
    else
        draw_bits();
    static const char* const names[] = {"1/3 summary >", "< 2/3 timing >", "< 3/3 bits"};
    if(page != 1) cv_text_right(CV_W, 58, &font_small, names[page]);
}

static bool input(InputEvent ev) {
    if(ev.type != INPUT_PRESS) return false;
    if(ev.key == KEY_RIGHT && page < 2) page++;
    if(ev.key == KEY_LEFT && page > 0) page--;
    return ev.key == KEY_LEFT || ev.key == KEY_RIGHT;
}

static const Screen analyzer_screen = {.title = "Analyzer", .input = input, .draw = draw};

void analyzer_open(const SgPacket* pkt) {
    packet = *pkt;
    sg_analyze(&packet, &an);
    is_decoded = sg_decode_packet(&packet, &decoded);
    page = 0;
    ui_push(&analyzer_screen);
}
