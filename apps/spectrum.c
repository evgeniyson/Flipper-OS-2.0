/* Spectrum analyser: sweeps RSSI over a span around a centre frequency. */
#include "apps.h"

#define BINS      128
#define PLOT_TOP  18
#define PLOT_H    38
#define DB_MIN    (-105)
#define DB_MAX    (-30)
#define BINS_PER_TICK 8

static const uint32_t spans[] = {1000000, 2000000, 5000000, 10000000, 20000000, 50000000};
static uint8_t span_idx = 3;
static uint32_t center;
static int8_t level[BINS], peak[BINS];
static bool have[BINS];
static int pos, sweeps;
static bool hold;
static SgCustomPreset preset;

static uint32_t bin_freq(int i) {
    uint32_t span = spans[span_idx];
    return center - span / 2 + (uint32_t)(((uint64_t)span * (uint32_t)i) / BINS);
}

static void restart(void) {
    memset(have, 0, sizeof(have));
    for(int i = 0; i < BINS; i++) peak[i] = -128;
    pos = sweeps = 0;
    /* resolution bandwidth: about two bins, within what the chip offers */
    SgCustomParams want = {
        .fsk = false, .rx_bw_hz = CLAMP(spans[span_idx] / (BINS / 2), 58000u, 812000u), .data_rate = 3794};
    sg_preset_build_custom(&preset, &want);
    radio_configure(&preset.preset);
}

static void enter(void) {
    center = settings.frequency;
    restart();
}

static void leave(void) {
    radio_idle();
    radio_sleep();
}

static void tick(uint32_t now) {
    UNUSED(now);
    for(int k = 0; k < BINS_PER_TICK; k++) {
        uint32_t f = bin_freq(pos);
        if(sg_freq_valid(f)) {
            int v = CLAMP(radio_measure(f), -127, 0);
            level[pos] = (int8_t)v;
            have[pos] = true;
            if(v > peak[pos]) peak[pos] = (int8_t)v;
        } else {
            have[pos] = false;
        }
        if(++pos >= BINS) {
            pos = 0;
            sweeps++;
        }
    }
    ui_dirty();
}

static int peak_bin(void) {
    int best = -1;
    for(int i = 0; i < BINS; i++) {
        if(!have[i]) continue;
        int v = hold ? peak[i] : level[i];
        if(best < 0 || v > (hold ? peak[best] : level[best])) best = i;
    }
    return best;
}

static int db_to_y(int db) {
    db = CLAMP(db, DB_MIN, DB_MAX);
    return PLOT_TOP + PLOT_H - 1 - (db - DB_MIN) * (PLOT_H - 1) / (DB_MAX - DB_MIN);
}

static void draw(void) {
    char a[12], b[40];
    ui_format_freq(a, sizeof(a), bin_freq(0));
    cv_text(1, 11, &font_small, a);
    ui_format_freq(a, sizeof(a), bin_freq(BINS));
    cv_text_right(CV_W, 11, &font_small, a);
    ui_format_freq(a, sizeof(a), center);
    cv_text_center(CV_W / 2, 11, &font_small, a);

    for(int i = 0; i < BINS; i++) {
        if(!have[i]) {
            if((i & 3) == 0) cv_pixel(i, PLOT_TOP + PLOT_H - 1, true); /* not tunable */
            continue;
        }
        int y = db_to_y(level[i]);
        cv_vline(i, y, PLOT_TOP + PLOT_H - y);
        if(hold && peak[i] > level[i]) cv_pixel(i, db_to_y(peak[i]), true);
    }
    cv_pixel(pos, PLOT_TOP, true); /* sweep cursor */

    int pk = peak_bin();
    if(pk >= 0) {
        ui_format_freq(a, sizeof(a), bin_freq(pk));
        fmt_snprintf(b, sizeof(b), "Pk %s %ddBm", a, hold ? peak[pk] : level[pk]);
        cv_text(0, 58, &font_small, b);
    }
    uint32_t span = spans[span_idx];
    fmt_snprintf(b, sizeof(b), "%s%luM", hold ? "HOLD " : "", (unsigned long)(span / 1000000));
    cv_text_right(CV_W, 58, &font_small, b);
}

static bool input(InputEvent ev) {
    bool step = ev.type == INPUT_PRESS || ev.type == INPUT_REPEAT;
    if(step && (ev.key == KEY_LEFT || ev.key == KEY_RIGHT)) {
        uint32_t delta = spans[span_idx] / 8;
        uint32_t next = ev.key == KEY_LEFT ? center - delta : center + delta;
        center = CLAMP(next, 300000000u, 928000000u);
        restart();
        return true;
    }
    if(ev.type == INPUT_PRESS && (ev.key == KEY_UP || ev.key == KEY_DOWN)) {
        if(ev.key == KEY_UP && span_idx > 0) span_idx--;
        if(ev.key == KEY_DOWN && (size_t)span_idx + 1 < ARRAY_SIZE(spans)) span_idx++;
        restart();
        return true;
    }
    if(ev.key == KEY_OK && ev.type == INPUT_SHORT) {
        hold = !hold;
        for(int i = 0; i < BINS; i++) peak[i] = -128;
        return true;
    }
    if(ev.key == KEY_OK && ev.type == INPUT_LONG) {
        /* take the strongest frequency to the receiver */
        int pk = peak_bin();
        if(pk >= 0) {
            settings.frequency = bin_freq(pk);
            settings_save();
            ui_pop();
            ui_push(&receiver_screen);
        }
        return true;
    }
    return false;
}

const Screen spectrum_screen = {
    .title = "Spectrum", .enter = enter, .leave = leave, .input = input, .tick = tick, .draw = draw};
