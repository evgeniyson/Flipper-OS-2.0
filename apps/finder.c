/* Frequency finder: hold a remote next to the device and press its button.
 * A fast coarse pass over the common frequencies spots the carrier, a narrow
 * fine pass around it pins the frequency down. */
#include "apps.h"

#define FINE_SPAN  300000
#define FINE_STEP  20000
#define HISTORY    3

static int threshold = -65;
static int coarse_rssi;
static uint32_t found[HISTORY];
static int found_rssi[HISTORY];
static int n_found;
static SgCustomPreset narrow;

static void coarse_mode(void) {
    radio_configure(&sg_presets[0]); /* widest filter */
}

static void enter(void) {
    SgCustomParams want = {.fsk = false, .rx_bw_hz = 58000, .data_rate = 3794};
    sg_preset_build_custom(&narrow, &want);
    coarse_mode();
}

static void leave(void) {
    radio_idle();
    radio_sleep();
}

static void remember(uint32_t hz, int rssi) {
    /* same signal again: just refresh the level */
    if(n_found && (found[0] > hz ? found[0] - hz : hz - found[0]) < 60000) {
        found[0] = hz;
        found_rssi[0] = rssi;
        return;
    }
    for(int i = HISTORY - 1; i > 0; i--) {
        found[i] = found[i - 1];
        found_rssi[i] = found_rssi[i - 1];
    }
    found[0] = hz;
    found_rssi[0] = rssi;
    if(n_found < HISTORY) n_found++;
}

static void tick(uint32_t now) {
    UNUSED(now);
    uint32_t best = 0;
    int best_rssi = -128;
    for(size_t i = 0; i < sg_freq_list_count; i++) {
        int v = radio_measure(sg_freq_list[i]);
        if(v > best_rssi) {
            best_rssi = v;
            best = sg_freq_list[i];
        }
    }
    coarse_rssi = best_rssi;
    if(best_rssi >= threshold) {
        radio_configure(&narrow.preset);
        /* The filter has a flat top, so several steps read about the same
         * level: take the middle of everything within 3 dB of the maximum. */
        static int8_t lvl[2 * FINE_SPAN / FINE_STEP + 1];
        int n = 0, fine_rssi = -128;
        for(uint32_t f = best - FINE_SPAN; f <= best + FINE_SPAN; f += FINE_STEP, n++) {
            int v = sg_freq_valid(f) ? radio_measure(f) : -128;
            lvl[n] = (int8_t)CLAMP(v, -128, 0);
            fine_rssi = MAX(fine_rssi, lvl[n]);
        }
        int64_t sum = 0;
        int hits = 0;
        for(int i = 0; i < n; i++)
            if(lvl[i] >= fine_rssi - 3) {
                sum += (int64_t)i * FINE_STEP;
                hits++;
            }
        uint32_t fine = best - FINE_SPAN + (uint32_t)(sum / hits);
        /* the carrier may have stopped between the two passes */
        if(fine_rssi >= threshold - 10) remember((fine + 5000) / 10000 * 10000, fine_rssi);
        coarse_mode();
    }
    ui_dirty();
}

static void draw(void) {
    char f[12], line[40];
    if(n_found) {
        ui_format_freq(f, sizeof(f), found[0]);
        int x = cv_text(4, 13, &font_big, f);
        cv_text(x + 3, 20, &font_normal, "MHz");
        fmt_snprintf(line, sizeof(line), "%ddBm", found_rssi[0]);
        cv_text_right(CV_W - 2, 13, &font_small, line);
    } else {
        cv_text(4, 16, &font_normal, "Press the remote...");
    }
    for(int i = 1; i < n_found; i++) {
        ui_format_freq(f, sizeof(f), found[i]);
        fmt_snprintf(line, sizeof(line), "%s  %ddBm", f, found_rssi[i]);
        cv_text(4, 23 + i * 8, &font_small, line);
    }
    /* live level with the trigger threshold marked */
    int lvl = CLAMP(coarse_rssi + 100, 0, 70), thr = CLAMP(threshold + 100, 0, 70);
    ui_bar(2, 48, 90, 6, lvl, 70);
    cv_vline(2 + 88 * thr / 70, 46, 10);
    fmt_snprintf(line, sizeof(line), "%ddBm", coarse_rssi);
    cv_text_right(CV_W - 2, 48, &font_small, line);
    fmt_snprintf(line, sizeof(line), "Trig %ddBm (up/down)  OK: use", threshold);
    cv_text(2, 58, &font_small, line);
}

static bool input(InputEvent ev) {
    if(ev.type == INPUT_PRESS && ev.key == KEY_UP && threshold < -35) threshold += 5;
    if(ev.type == INPUT_PRESS && ev.key == KEY_DOWN && threshold > -95) threshold -= 5;
    if(ev.key == KEY_OK && ev.type == INPUT_SHORT && n_found) {
        settings.frequency = found[0];
        settings_save();
        ui_toast("Frequency set");
    }
    return ev.key != KEY_BACK;
}

const Screen finder_screen = {
    .title = "Freq finder", .enter = enter, .leave = leave, .input = input, .tick = tick, .draw = draw};
