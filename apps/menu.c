#include "apps.h"

static const struct {
    const char* label;
    const Screen* screen;
} items[] = {
    {"Receiver", &receiver_screen},       {"Spectrum", &spectrum_screen},
    {"Frequency finder", &finder_screen}, {"RAW recorder", &recorder_screen},
    {"Saved signals", &saved_screen},     {"Settings", &settings_screen},
};

static UiList list = {.count = ARRAY_SIZE(items)};

static void row(int idx, int y, bool selected, void* ctx) {
    UNUSED(ctx);
    ui_list_row(y, selected, items[idx].label, NULL);
}

static void draw(void) {
    ui_list_draw(&list, UI_TOP, UI_ROWS, row, NULL);
}

static bool input(InputEvent ev) {
    if(ui_list_input(&list, ev, UI_ROWS)) return true;
    if(ev.type == INPUT_SHORT && (ev.key == KEY_OK || ev.key == KEY_RIGHT)) {
        ui_push(items[list.sel].screen);
        return true;
    }
    return ev.key == KEY_BACK; /* nothing to go back to */
}

const Screen menu_screen = {.title = "Flipper OS 2", .input = input, .draw = draw};

/* ------------------------------------------------------------ shared helpers */

void freq_step(uint32_t* hz, int dir, bool fine) {
    if(fine) {
        uint32_t next = *hz + (uint32_t)(dir * 10000);
        if(sg_freq_valid(next)) *hz = next;
        return;
    }
    if(dir > 0) {
        for(size_t i = 0; i < sg_freq_list_count; i++)
            if(sg_freq_list[i] > *hz) {
                *hz = sg_freq_list[i];
                return;
            }
        *hz = sg_freq_list[0];
    } else {
        for(size_t i = sg_freq_list_count; i-- > 0;)
            if(sg_freq_list[i] < *hz) {
                *hz = sg_freq_list[i];
                return;
            }
        *hz = sg_freq_list[sg_freq_list_count - 1];
    }
}

int app_file_write(void* file, const char* data, size_t len) {
    return hal_file_write(file, data, len);
}

void radio_summary(char* buf, size_t size, uint32_t hz) {
    char f[12];
    ui_format_freq(f, sizeof(f), hz);
    fmt_snprintf(buf, size, "%s %s", f, rx_default_preset()->name);
}

/* ------------------------------------------------------------ radio config */

static void v_freq(char* b, size_t n) {
    ui_format_freq(b, n, settings.frequency);
}
static void c_freq(int dir, bool fine) {
    freq_step(&settings.frequency, dir, fine);
}
static void v_preset(char* b, size_t n) {
    fmt_snprintf(b, n, "%s", rx_default_preset()->name);
}
static void c_preset(int dir, bool fine) {
    UNUSED(fine);
    settings.preset = (uint8_t)((settings.preset + sg_preset_count + (size_t)dir) % sg_preset_count);
}
static void v_hop(char* b, size_t n) {
    fmt_snprintf(b, n, "%s", settings.hopping ? "on" : "off");
}
static void c_hop(int dir, bool fine) {
    UNUSED(dir);
    UNUSED(fine);
    settings.hopping = !settings.hopping;
}

static const UiOption cfg_opts[] = {
    {"Frequency", v_freq, c_freq, NULL},
    {"Modulation", v_preset, c_preset, NULL},
    {"Hopping", v_hop, c_hop, NULL},
};
static UiList cfg_list = {.count = ARRAY_SIZE(cfg_opts)};

static bool cfg_input(InputEvent ev) {
    return ui_options_input(&cfg_list, cfg_opts, ev);
}
static void cfg_draw(void) {
    ui_options_draw(&cfg_list, cfg_opts);
    cv_text(2, 57, &font_small, "</> change   hold: 10 kHz steps");
}
static void cfg_leave(void) {
    settings_save();
}

const Screen radio_config_screen = {
    .title = "Radio config", .leave = cfg_leave, .input = cfg_input, .draw = cfg_draw};
