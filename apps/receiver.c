/* Receiver: listens on one frequency (or hops), decodes what it can and keeps
 * a history. Anything it cannot decode can be opened in the analyser. */
#include "apps.h"

#define HISTORY 16

typedef struct {
    SgDecoded d;
    uint16_t repeats;
    int8_t rssi;
    uint32_t freq;
} Entry;

static Entry history[HISTORY];
static int n_history;
static uint32_t unknown_count;
static SgPacket last_unknown;
static bool have_unknown;
static UiList list;
static int rssi = -128;
static uint32_t rssi_at, hop_at, signal_at;
static size_t hop_idx;
static Entry detail;

static void on_packet(const SgPacket* pkt, void* ctx) {
    UNUSED(ctx);
    signal_at = hal_millis();
    SgDecoded d;
    if(!sg_decode_packet(pkt, &d)) {
        /* keep the most interesting recent unknown: skip tiny noise bursts */
        if(pkt->n >= 24) {
            unknown_count++;
            last_unknown = *pkt;
            have_unknown = true;
        }
        return;
    }
    for(int i = 0; i < n_history; i++) {
        if(history[i].d.proto == d.proto && history[i].d.data == d.data) {
            if(history[i].repeats < 9999) history[i].repeats++;
            history[i].rssi = (int8_t)CLAMP(rssi, -127, 0);
            return;
        }
    }
    if(n_history < HISTORY) n_history++;
    for(int i = n_history - 1; i > 0; i--) history[i] = history[i - 1];
    history[0] = (Entry){d, 1, (int8_t)CLAMP(rssi, -127, 0), rx_frequency()};
    if(list.sel > 0 && list.sel < n_history - 1) list.sel++; /* keep the selection on its entry */
    hal_led(0, 40, 0);
}

static void start(void) {
    rx_start(settings.frequency, rx_default_preset(), on_packet, NULL, NULL);
}

static void enter(void) {
    start();
    hop_at = hal_millis();
}

static void leave(void) {
    rx_stop();
    hal_led(0, 0, 0);
}

static void tick(uint32_t now) {
    /* coming back from the config screen: apply what changed */
    if(!rx_running()) start();
    rx_poll();
    if(now - rssi_at >= 100) {
        rssi_at = now;
        rssi = radio_rssi();
        if(now - signal_at > 150) hal_led(0, 0, 0);
        ui_dirty();
    }
    /* hopping: move on unless something is being received right now */
    if(settings.hopping && now - hop_at >= 150 && now - signal_at > 400 && rssi < -75) {
        hop_at = now;
        hop_idx = (hop_idx + 1) % sg_freq_hop_list_count;
        rx_retune(sg_freq_hop_list[hop_idx]);
    }
}

static void row(int idx, int y, bool selected, void* ctx) {
    UNUSED(ctx);
    char left[32], right[8];
    if(idx < n_history) {
        const Entry* e = &history[idx];
        if(e->d.bits > 32)
            fmt_snprintf(left, sizeof(left), "%s %08lX..", e->d.proto->name, (unsigned long)(e->d.data >> 32));
        else
            fmt_snprintf(left, sizeof(left), "%s %lX", e->d.proto->name, (unsigned long)e->d.data);
        left[20] = '\0';
        fmt_snprintf(right, sizeof(right), "x%u", e->repeats);
    } else {
        fmt_snprintf(left, sizeof(left), "? Unknown signals");
        fmt_snprintf(right, sizeof(right), "x%lu", (unsigned long)MIN(unknown_count, 9999u));
    }
    ui_list_row(y, selected, left, right);
}

static void draw(void) {
    char line[40], f[12];
    ui_format_freq(f, sizeof(f), rx_frequency());
    fmt_snprintf(line, sizeof(line), "%s %s%s", f, rx_default_preset()->name, settings.hopping ? " HOP" : "");
    cv_text(1, 11, &font_small, line);
    fmt_snprintf(line, sizeof(line), "%d", rssi);
    cv_text_right(CV_W - 1, 11, &font_small, line);
    ui_bar(72, 11, 38, 5, CLAMP(rssi + 100, 0, 70), 70);

    list.count = n_history + (have_unknown ? 1 : 0);
    if(list.count == 0) {
        cv_text_center(CV_W / 2, 30, &font_normal, "Listening...");
        cv_text_center(CV_W / 2, 56, &font_small, "LEFT: frequency and modulation");
        return;
    }
    if(list.sel >= list.count) list.sel = list.count - 1;
    ui_list_draw(&list, 18, 5, row, NULL);
}

/* ------------------------------------------------------------ detail */

static void detail_draw(void) {
    char text[200];
    sg_decoded_describe(&detail.d, text, sizeof(text));
    int y = UI_TOP;
    for(char* line = text; line && *line && y < 50;) {
        char* end = strchr(line, '\n');
        if(end) *end = '\0';
        cv_text(2, y, &font_normal, line);
        y += 8;
        line = end ? end + 1 : NULL;
    }
    char f[12], bottom[40];
    ui_format_freq(f, sizeof(f), detail.freq);
    fmt_snprintf(bottom, sizeof(bottom), "%s MHz  %ddBm", f, detail.rssi);
    cv_text(2, 50, &font_small, bottom);
    bool can_send = detail.d.proto->cls == SG_CLASS_STATIC;
    cv_text(2, 58, &font_small, can_send ? "OK: save   RIGHT: send" : "OK: save   (rolling: no TX)");
}

static void save_key(void) {
    char path[64];
    if(!hal_storage_ready()) {
        ui_toast("No SD card");
        return;
    }
    hal_mkdir(SUBGHZ_DIR);
    char name[16];
    fmt_snprintf(name, sizeof(name), "%s", detail.d.proto->name);
    for(char* c = name; *c; c++)
        if(*c == ' ') *c = '_';
    fmt_snprintf(path, sizeof(path), SUBGHZ_DIR "/%s_%lX.sub", name, (unsigned long)(detail.d.data & 0xFFFFFFFFu));
    HalFile* f = hal_file_open(path, "w");
    bool ok = f != NULL;
    if(f) {
        ok = sub_write_key(app_file_write, f, detail.freq, rx_default_preset()->sub_name, &detail.d);
        hal_file_close(f);
    }
    ui_toast(ok ? "Saved" : "Save failed");
}

static bool sending;

static void detail_tick(uint32_t now) {
    UNUSED(now);
    if(sending && !radio_tx_busy()) {
        sending = false;
        tx_end();
        hal_led(0, 0, 0);
        ui_toast("Sent");
    }
}

static bool detail_input(InputEvent ev) {
    if(sending) return true;
    if(ev.type != INPUT_SHORT) return false;
    if(ev.key == KEY_OK) {
        save_key();
        return true;
    }
    if(ev.key == KEY_RIGHT) {
        TxResult r = tx_send_key(detail.freq, rx_default_preset(), &detail.d, 8);
        if(r == TX_OK) {
            sending = true;
            hal_led(60, 0, 0);
        } else {
            ui_toast(tx_result_text(r));
        }
        return true;
    }
    return false;
}

static void detail_leave(void) {
    if(sending) tx_end();
    sending = false;
    hal_led(0, 0, 0);
}

static const Screen detail_screen = {
    .title = "Signal", .leave = detail_leave, .input = detail_input, .tick = detail_tick, .draw = detail_draw};

/* ------------------------------------------------------------ */

static bool input(InputEvent ev) {
    if(ui_list_input(&list, ev, 5)) return true;
    if(ev.type == INPUT_SHORT && ev.key == KEY_LEFT) {
        rx_stop(); /* restarted with the new settings on return */
        ui_push(&radio_config_screen);
        return true;
    }
    if(ev.type == INPUT_SHORT && ev.key == KEY_OK && list.count) {
        if(list.sel < n_history) {
            detail = history[list.sel];
            rx_stop();
            ui_push(&detail_screen);
        } else {
            rx_stop();
            analyzer_open(&last_unknown);
        }
        return true;
    }
    if(ev.type == INPUT_LONG && ev.key == KEY_RIGHT) {
        n_history = 0;
        unknown_count = 0;
        have_unknown = false;
        list.sel = list.top = 0;
        ui_toast("History cleared");
        return true;
    }
    return false;
}

const Screen receiver_screen = {
    .title = "Receiver", .enter = enter, .leave = leave, .input = input, .tick = tick, .draw = draw};
