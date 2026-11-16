/* Saved signals: SD card browser, file inspection, packet list and replay. */
#include "apps.h"

#define MAX_ENTRIES 64
#define MAX_PKTS    48

/* ------------------------------------------------------------ file scan */

typedef struct {
    uint16_t pulses;
    const SgProtocol* proto;
    uint64_t data;
    uint8_t enc;
    uint16_t bits;
} PktSummary;

static char file_path[96];
static const char* file_name;
static SubInfo info;
static PktSummary pkts[MAX_PKTS];
static int n_pkts, n_decoded, total_pkts;
static SgDecoded first_decoded;
static int capture_idx;
static SgPacket captured;
static bool scan_ok;

static SgPacketizer scan_packetizer;
static SubParser scan_parser;

static void scan_packet(const SgPacket* pkt, void* ctx) {
    UNUSED(ctx);
    int idx = total_pkts++;
    if(idx == capture_idx) captured = *pkt;
    if(capture_idx >= 0) return; /* second pass: only fetching one packet */
    SgDecoded d;
    bool ok = sg_decode_packet(pkt, &d);
    if(ok && n_decoded++ == 0) first_decoded = d;
    if(idx >= MAX_PKTS) return;
    PktSummary* s = &pkts[n_pkts++];
    s->pulses = pkt->n;
    s->proto = ok ? d.proto : NULL;
    s->data = ok ? d.data : 0;
    if(!ok) {
        static SgAnalysis a;
        sg_analyze(pkt, &a);
        s->enc = (uint8_t)a.enc;
        s->bits = a.n_bits;
    }
}

static void scan_pulse(sg_pulse_t p, void* ctx) {
    UNUSED(ctx);
    sg_packetizer_feed(&scan_packetizer, p);
}

/* Parse the whole file. capture < 0: build the summary; otherwise copy
 * packet number `capture` into `captured`. */
static bool scan(int capture) {
    HalFile* f = hal_file_open(file_path, "r");
    if(!f) return false;
    capture_idx = capture;
    total_pkts = 0;
    if(capture < 0) n_pkts = n_decoded = 0;
    sub_parser_init(&scan_parser, scan_pulse, NULL);
    sg_packetizer_init(&scan_packetizer, 0, scan_packet, NULL);
    char buf[512];
    int n;
    while((n = hal_file_read(f, buf, sizeof(buf))) > 0) sub_parser_feed(&scan_parser, buf, (size_t)n);
    hal_file_close(f);
    sub_parser_finish(&scan_parser);
    sg_packetizer_flush(&scan_packetizer);
    if(capture < 0) info = scan_parser.info;
    return scan_parser.info.filetype_ok;
}

static const SgPreset* file_preset(void) {
    const SgPreset* p = sg_preset_by_sub_name(info.preset);
    return p ? p : &sg_presets[0];
}

/* ------------------------------------------------------------ sending */

static HalFile* tx_file;
static bool tx_eof, tx_active;
static uint32_t tx_sent;
static SubParser tx_parser;

static void tx_pulse(sg_pulse_t p, void* ctx) {
    UNUSED(ctx);
    if(radio_tx_push(p)) tx_sent++;
}

static void send_stop(const char* message) {
    if(tx_file) hal_file_close(tx_file);
    tx_file = NULL;
    if(tx_active) tx_end();
    tx_active = false;
    hal_led(0, 0, 0);
    ui_pop();
    if(message) ui_toast(message);
}

static void send_tick(uint32_t now) {
    UNUSED(now);
    if(!tx_active) return;
    /* keep the transmit queue topped up from the file */
    while(tx_file && !tx_eof && radio_tx_space() > 256) {
        char buf[128];
        int n = hal_file_read(tx_file, buf, sizeof(buf));
        if(n <= 0) {
            tx_eof = true;
            sub_parser_finish(&tx_parser);
            radio_tx_finish();
            break;
        }
        sub_parser_feed(&tx_parser, buf, (size_t)n);
    }
    if(!radio_tx_busy() && (tx_eof || !tx_file)) send_stop("Sent");
    ui_dirty();
}

static void send_draw(void) {
    char line[40], f[12];
    ui_format_freq(f, sizeof(f), info.frequency);
    fmt_snprintf(line, sizeof(line), "%s MHz  %s", f, file_preset()->name);
    cv_text_center(CV_W / 2, 18, &font_normal, line);
    fmt_snprintf(line, sizeof(line), "%lu pulses", (unsigned long)tx_sent);
    cv_text_center(CV_W / 2, 32, &font_normal, line);
    cv_text_center(CV_W / 2, 54, &font_small, "BACK: stop");
}

static bool send_input(InputEvent ev) {
    if(ev.key == KEY_BACK && ev.type == INPUT_SHORT) send_stop("Stopped");
    return true;
}

static const Screen send_screen = {
    .title = "Sending", .input = send_input, .tick = send_tick, .draw = send_draw};

static void send_file(void) {
    TxResult r;
    tx_sent = 0;
    tx_eof = false;
    tx_file = NULL;
    if(info.is_raw) {
        r = tx_begin(info.frequency, file_preset());
        if(r == TX_OK) {
            tx_file = hal_file_open(file_path, "r");
            if(!tx_file) {
                tx_end();
                r = TX_FAILED;
            }
            sub_parser_init(&tx_parser, tx_pulse, NULL);
        }
    } else {
        SgDecoded d;
        if(!sub_info_to_decoded(&info, &d))
            r = TX_FAILED;
        else
            r = tx_send_key(info.frequency, file_preset(), &d, 8);
    }
    if(r != TX_OK) {
        ui_message("Not sent", tx_result_text(r), r == TX_FREQ_BLOCKED ? sg_region_name((SgRegion)settings.region) : "");
        return;
    }
    tx_active = true;
    hal_led(60, 0, 0);
    ui_push(&send_screen);
}

/* ------------------------------------------------------------ packet list */

static UiList pkt_list;

static void pkt_row(int idx, int y, bool selected, void* ctx) {
    UNUSED(ctx);
    char left[36];
    const PktSummary* s = &pkts[idx];
    if(s->proto)
        fmt_snprintf(left, sizeof(left), "%d %s %lX", idx + 1, s->proto->name, (unsigned long)s->data);
    else if(s->bits)
        fmt_snprintf(left, sizeof(left), "%d ? %s %ub", idx + 1, sg_encoding_name((SgEncoding)s->enc), s->bits);
    else
        fmt_snprintf(left, sizeof(left), "%d ? %u pulses", idx + 1, s->pulses);
    left[24] = '\0';
    ui_list_row(y, selected, left, NULL);
}

static void pkt_draw(void) {
    ui_list_draw(&pkt_list, UI_TOP, UI_ROWS, pkt_row, NULL);
}

static bool pkt_input(InputEvent ev) {
    if(ui_list_input(&pkt_list, ev, UI_ROWS)) return true;
    if(ev.key == KEY_OK && ev.type == INPUT_SHORT && pkt_list.count) {
        if(scan(pkt_list.sel)) analyzer_open(&captured);
        return true;
    }
    return false;
}

static const Screen pkt_screen = {.title = "Packets", .input = pkt_input, .draw = pkt_draw};

/* ------------------------------------------------------------ file view */

static UiList act_list;
static bool need_scan, scan_shown, delete_armed;
static const char* const actions[] = {"Send", "Packets", "Delete"};

static bool dir_stale;

static void file_leave(void) {
    dir_stale = true; /* the file may have been deleted */
}

static void file_enter(void) {
    need_scan = true;
    scan_shown = false;
    delete_armed = false;
    act_list = (UiList){.count = 3};
}

static void file_tick(uint32_t now) {
    UNUSED(now);
    if(!need_scan) return;
    if(!scan_shown) {
        scan_shown = true; /* let "Reading..." reach the screen first */
        ui_dirty();
        return;
    }
    need_scan = false;
    scan_ok = scan(-1);
    ui_dirty();
}

static void act_row(int idx, int y, bool selected, void* ctx) {
    UNUSED(ctx);
    ui_list_row(y, selected, idx == 2 && delete_armed ? "Delete - OK to confirm" : actions[idx], NULL);
}

static void file_draw(void) {
    char line[48], f[12];
    cv_text(2, 11, &font_small, file_name);
    if(need_scan) {
        cv_text_center(CV_W / 2, 30, &font_normal, "Reading...");
        return;
    }
    if(!scan_ok) {
        cv_text(2, 22, &font_normal, "Not a .sub file");
        return;
    }
    ui_format_freq(f, sizeof(f), info.frequency);
    fmt_snprintf(line, sizeof(line), "%s MHz  %s  %s", f, file_preset()->name, info.is_raw ? "RAW" : "key");
    cv_text(2, 18, &font_small, line);
    if(info.is_raw) {
        fmt_snprintf(line, sizeof(line), "%lu pulses %d pkts %d decoded", (unsigned long)info.pulse_count,
                     total_pkts, n_decoded);
        cv_text(2, 24, &font_small, line);
        if(n_decoded) {
            fmt_snprintf(line, sizeof(line), "%s %ubit %lX", first_decoded.proto->name, first_decoded.bits,
                         (unsigned long)first_decoded.data);
            cv_text(2, 30, &font_small, line);
        }
    } else {
        fmt_snprintf(line, sizeof(line), "%s %ubit", info.protocol, info.bits);
        cv_text(2, 24, &font_small, line);
        fmt_snprintf(line, sizeof(line), "Key %lX  TE %lu", (unsigned long)info.key, (unsigned long)info.te);
        cv_text(2, 30, &font_small, line);
    }
    ui_list_draw(&act_list, 37, 3, act_row, NULL);
}

static bool file_input(InputEvent ev) {
    if(need_scan) return ev.key != KEY_BACK;
    if(ui_list_input(&act_list, ev, 3)) {
        delete_armed = false;
        return true;
    }
    if(ev.key != KEY_OK || ev.type != INPUT_SHORT || !scan_ok) return false;
    if(act_list.sel == 0) {
        send_file();
    } else if(act_list.sel == 1) {
        if(info.is_raw && n_pkts) {
            pkt_list = (UiList){.count = n_pkts};
            ui_push(&pkt_screen);
        } else {
            ui_toast(info.is_raw ? "No packets found" : "Key file: no packets");
        }
    } else if(!delete_armed) {
        delete_armed = true;
    } else {
        bool ok = hal_file_remove(file_path);
        ui_pop();
        ui_toast(ok ? "Deleted" : "Delete failed");
    }
    return true;
}

static const Screen file_screen = {
    .title = "Signal file", .enter = file_enter, .leave = file_leave, .input = file_input, .tick = file_tick, .draw = file_draw};

/* ------------------------------------------------------------ browser */

static char dir_path[80] = SUBGHZ_DIR;
static HalDirEntry entries[MAX_ENTRIES];
static UiList dir_list;
static bool card_ok;

static void load_dir(void) {
    card_ok = hal_storage_ready();
    int n = card_ok ? hal_dir_list(dir_path, entries, MAX_ENTRIES, 0) : 0;
    dir_list = (UiList){.count = n > 0 ? n : 0};
}

static void browser_enter(void) {
    if(hal_storage_ready()) hal_mkdir(SUBGHZ_DIR);
    load_dir();
}

static void dir_row(int idx, int y, bool selected, void* ctx) {
    UNUSED(ctx);
    char left[32], right[10] = "";
    const HalDirEntry* e = &entries[idx];
    fmt_snprintf(left, sizeof(left), "%s%s", e->is_dir ? "/" : "", e->name);
    left[21] = '\0';
    if(!e->is_dir) fmt_snprintf(right, sizeof(right), "%luk", (unsigned long)((e->size + 1023) / 1024));
    ui_list_row(y, selected, left, right);
}

static void browser_draw(void) {
    if(!card_ok) {
        cv_text_center(CV_W / 2, 28, &font_normal, "No SD card");
        return;
    }
    if(!dir_list.count) {
        cv_text_center(CV_W / 2, 28, &font_normal, "No files here");
        cv_text_center(CV_W / 2, 40, &font_small, dir_path);
        return;
    }
    ui_list_draw(&dir_list, UI_TOP, UI_ROWS, dir_row, NULL);
}

static bool browser_input(InputEvent ev) {
    if(ui_list_input(&dir_list, ev, UI_ROWS)) return true;
    if(ev.type != INPUT_SHORT) return false;
    if(ev.key == KEY_BACK) {
        /* go up one directory until we are back at the top */
        if(strcmp(dir_path, SUBGHZ_DIR) == 0) return false;
        char* slash = strrchr(dir_path, '/');
        if(slash && slash != dir_path) *slash = '\0';
        load_dir();
        return true;
    }
    if(ev.key == KEY_OK && dir_list.count) {
        const HalDirEntry* e = &entries[dir_list.sel];
        if(e->is_dir) {
            size_t len = strlen(dir_path);
            if(len + strlen(e->name) + 2 < sizeof(dir_path)) {
                fmt_snprintf(dir_path + len, sizeof(dir_path) - len, "/%s", e->name);
                load_dir();
            }
        } else {
            fmt_snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, e->name);
            file_name = strrchr(file_path, '/') + 1;
            ui_push(&file_screen);
        }
        return true;
    }
    return false;
}

/* the list may have changed (delete) when a child screen closes */
static void browser_tick(uint32_t now) {
    UNUSED(now);
    if(dir_stale) {
        dir_stale = false;
        int sel = dir_list.sel, top = dir_list.top;
        load_dir();
        if(dir_list.count) {
            dir_list.sel = MIN(sel, dir_list.count - 1);
            dir_list.top = MIN(top, dir_list.sel);
        }
        ui_dirty();
    }
}

const Screen saved_screen = {
    .title = "Saved", .enter = browser_enter, .input = browser_input, .tick = browser_tick, .draw = browser_draw};
