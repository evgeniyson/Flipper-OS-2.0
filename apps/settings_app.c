#include "apps.h"

static void v_region(char* b, size_t n) {
    fmt_snprintf(b, n, "%s", sg_region_name((SgRegion)settings.region));
}
static void c_region(int dir, bool fine) {
    UNUSED(fine);
    settings.region = (uint8_t)((settings.region + SG_REGION_COUNT + dir) % SG_REGION_COUNT);
}

static void v_radio(char* b, size_t n) {
    static const char* const names[] = {"Auto", "Internal", "External"};
    fmt_snprintf(b, n, "%s", names[settings.radio_mode % RADIO_MODE_COUNT]);
}
static void c_radio(int dir, bool fine) {
    UNUSED(fine);
    settings.radio_mode = (uint8_t)((settings.radio_mode + RADIO_MODE_COUNT + dir) % RADIO_MODE_COUNT);
    settings_apply();
    if(settings.radio_mode == RADIO_MODE_EXTERNAL && radio_current() != RADIO_EXTERNAL)
        ui_toast("No external module");
}

static void v_5v(char* b, size_t n) {
    fmt_snprintf(b, n, "%s", settings.ext_5v ? "on" : "off");
}
static void c_5v(int dir, bool fine) {
    UNUSED(dir);
    UNUSED(fine);
    settings.ext_5v = !settings.ext_5v;
    settings_apply();
}

static void v_light(char* b, size_t n) {
    fmt_snprintf(b, n, "%u", settings.backlight);
}
static void c_light(int dir, bool fine) {
    UNUSED(fine);
    settings.backlight = (uint8_t)CLAMP((int)settings.backlight + dir, 0, 10);
    hal_backlight((uint8_t)(settings.backlight * 255 / 10));
}

static void v_contrast(char* b, size_t n) {
    fmt_snprintf(b, n, "%d", settings.contrast);
}
static void c_contrast(int dir, bool fine) {
    UNUSED(fine);
    settings.contrast = (int8_t)CLAMP(settings.contrast + dir, -8, 8);
    hal_display_contrast(settings.contrast);
}

static void v_battery(char* b, size_t n) {
    HalBattery bat;
    if(hal_battery(&bat))
        fmt_snprintf(b, n, "%u%% %u.%02uV%s", bat.percent, bat.millivolts / 1000, bat.millivolts % 1000 / 10,
                     bat.charging ? "+" : "");
    else
        fmt_snprintf(b, n, "?");
}

static void v_modules(char* b, size_t n) {
    fmt_snprintf(b, n, "%s%s", radio_present(RADIO_INTERNAL) ? "INT " : "", radio_present(RADIO_EXTERNAL) ? "EXT" : "");
}

static void v_version(char* b, size_t n) {
    fmt_snprintf(b, n, FOS_VERSION);
}

static void a_dfu(void) {
    settings_save();
    hal_reboot_dfu();
}
static void a_off(void) {
    settings_save();
    hal_power_off();
}

static const UiOption opts[] = {
    {"TX region", v_region, c_region, NULL},
    {"Radio", v_radio, c_radio, NULL},
    {"5V on GPIO", v_5v, c_5v, NULL},
    {"Backlight", v_light, c_light, NULL},
    {"Contrast", v_contrast, c_contrast, NULL},
    {"Battery", v_battery, NULL, NULL},
    {"Radios found", v_modules, NULL, NULL},
    {"Version", v_version, NULL, NULL},
    {"Reboot to DFU", NULL, NULL, a_dfu},
    {"Power off", NULL, NULL, a_off},
};
static UiList list = {.count = ARRAY_SIZE(opts)};

static bool input(InputEvent ev) {
    return ui_options_input(&list, opts, ev);
}
static void draw(void) {
    ui_options_draw(&list, opts);
}
static void leave(void) {
    if(!settings_save() && hal_storage_ready()) ui_toast("Settings not saved");
}

const Screen settings_screen = {.title = "Settings", .leave = leave, .input = input, .draw = draw};
