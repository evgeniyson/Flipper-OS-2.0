#include "ui.h"
#include "settings.h"
#include "../core/fmt.h"
#include "../hal/hal.h"
#include "../hal/radio.h"
#include <string.h>

#define STACK_MAX 8
#define DIM_AFTER_MS 45000
#define TOAST_MS 1600

static const Screen* stack[STACK_MAX];
static int depth;
static bool dirty;
static char toast[28];
static uint32_t toast_until, last_input, last_draw, last_battery;
static bool dimmed;
static HalBattery battery;
static bool battery_ok;

static void apply_backlight(void) {
    uint8_t level = (uint8_t)(settings.backlight * 255 / 10);
    hal_backlight(dimmed ? level / 8 : level);
}

void ui_init(const Screen* root) {
    depth = 0;
    dimmed = false;
    last_input = hal_millis();
    apply_backlight();
    ui_push(root);
}

void ui_push(const Screen* s) {
    if(depth >= STACK_MAX) return;
    stack[depth++] = s;
    if(s->enter) s->enter();
    dirty = true;
}

void ui_pop(void) {
    if(depth <= 1) return;
    const Screen* s = stack[--depth];
    if(s->leave) s->leave();
    dirty = true;
}

void ui_dirty(void) {
    dirty = true;
}

void ui_toast(const char* text) {
    size_t n = strlen(text);
    if(n >= sizeof(toast)) n = sizeof(toast) - 1;
    memcpy(toast, text, n);
    toast[n] = '\0';
    toast_until = hal_millis() + TOAST_MS;
    dirty = true;
}

static void draw_status_bar(const char* title) {
    cv_text(1, 0, &font_normal, title);
    /* battery: outline 13x7 with a nub, filled by charge */
    int bx = CV_W - 15;
    cv_rect(bx, 1, 13, 7);
    cv_vline(bx + 13, 3, 3);
    if(battery_ok) {
        int fill = battery.percent * 9 / 100;
        cv_fill(bx + 2, 3, fill, 3, true);
        if(battery.charging) cv_invert(bx + 5, 2, 3, 5);
    } else {
        cv_text(bx + 5, 2, &font_small, "?");
    }
    cv_text_right(bx - 3, 2, &font_small, radio_current() == RADIO_EXTERNAL ? "EXT" : "INT");
    cv_hline(0, 9, CV_W);
}

static void draw(void) {
    const Screen* s = stack[depth - 1];
    cv_clear();
    if(s->title) draw_status_bar(s->title);
    if(s->draw) s->draw();
    if(toast[0]) {
        int w = cv_text_width(&font_normal, toast) + 8;
        int x = (CV_W - w) / 2;
        cv_fill(x - 1, 25, w + 2, 15, false);
        cv_rect(x, 26, w, 13);
        cv_text(x + 4, 29, &font_normal, toast);
    }
    hal_display_flush(cv_buffer());
}

void ui_step(void) {
    uint32_t now = hal_millis();
    input_update(hal_keys(), now);
    InputEvent ev;
    while(input_pop(&ev)) {
        last_input = now;
        if(dimmed) {
            /* the first key after dimming only wakes the screen */
            if(ev.type == INPUT_RELEASE) {
                dimmed = false;
                apply_backlight();
            }
            continue;
        }
        const Screen* s = stack[depth - 1];
        bool used = s->input && s->input(ev);
        if(!used && ev.key == KEY_BACK && ev.type == INPUT_SHORT) ui_pop();
        dirty = true;
    }
    if(!dimmed && now - last_input > DIM_AFTER_MS) {
        dimmed = true;
        apply_backlight();
    }
    if(now - last_battery > 5000 || last_battery == 0) {
        last_battery = now ? now : 1;
        battery_ok = hal_battery(&battery);
        dirty = true;
    }
    const Screen* s = stack[depth - 1];
    if(s->tick) s->tick(now);
    if(toast[0] && (int32_t)(now - toast_until) >= 0) {
        toast[0] = '\0';
        dirty = true;
    }
    /* redraw on demand, at most ~30 fps */
    if(dirty && now - last_draw >= 33) {
        dirty = false;
        last_draw = now;
        draw();
    }
}

/* ------------------------------------------------------------ widgets */

bool ui_list_input(UiList* l, InputEvent ev, int visible) {
    if(ev.type != INPUT_PRESS && ev.type != INPUT_REPEAT) return false;
    if(ev.key != KEY_UP && ev.key != KEY_DOWN) return false;
    if(l->count <= 0) return true;
    if(ev.key == KEY_UP)
        l->sel = l->sel > 0 ? l->sel - 1 : (ev.type == INPUT_PRESS ? l->count - 1 : 0);
    else
        l->sel = l->sel < l->count - 1 ? l->sel + 1 : (ev.type == INPUT_PRESS ? 0 : l->count - 1);
    if(l->sel < l->top) l->top = l->sel;
    if(l->sel >= l->top + visible) l->top = l->sel - visible + 1;
    return true;
}

void ui_list_draw(const UiList* l, int y, int visible, UiListItem item, void* ctx) {
    for(int row = 0; row < visible; row++) {
        int idx = l->top + row;
        if(idx >= l->count) break;
        item(idx, y + row * UI_ROW_H, idx == l->sel, ctx);
    }
    if(l->count > visible) {
        /* scroll indicator */
        int track = visible * UI_ROW_H;
        int len = track * visible / l->count;
        int pos = track * l->top / l->count;
        cv_vline(CV_W - 1, y + pos, len < 3 ? 3 : len);
    }
}

void ui_list_row(int y, bool selected, const char* left, const char* right) {
    if(left) cv_text(3, y + 1, &font_normal, left);
    if(right) cv_text_right(CV_W - 4, y + 1, &font_normal, right);
    if(selected) cv_invert(0, y, CV_W - 2, UI_ROW_H);
}

void ui_format_freq(char* buf, size_t size, uint32_t hz) {
    uint32_t khz = (hz + 500) / 1000;
    if(khz % 10)
        fmt_snprintf(buf, size, "%lu.%03lu", (unsigned long)(khz / 1000), (unsigned long)(khz % 1000));
    else
        fmt_snprintf(buf, size, "%lu.%02lu", (unsigned long)(khz / 1000), (unsigned long)(khz % 1000 / 10));
}

void ui_bar(int x, int y, int w, int h, int value, int max) {
    cv_rect(x, y, w, h);
    if(value < 0) value = 0;
    if(value > max) value = max;
    if(max > 0) cv_fill(x + 1, y + 1, (w - 2) * value / max, h - 2, true);
}

/* ------------------------------------------------------------ message screen */

static char msg_title[20], msg_l1[28], msg_l2[28];

static void msg_draw(void) {
    cv_text_center(CV_W / 2, 22, &font_normal, msg_l1);
    cv_text_center(CV_W / 2, 34, &font_normal, msg_l2);
    cv_text_center(CV_W / 2, 54, &font_small, "BACK to close");
}

static bool msg_input(InputEvent ev) {
    if(ev.type == INPUT_SHORT && (ev.key == KEY_OK || ev.key == KEY_BACK)) ui_pop();
    return true;
}

static const Screen msg_screen = {.title = msg_title, .input = msg_input, .draw = msg_draw};

static void copy(char* dst, size_t size, const char* src) {
    size_t n = src ? strlen(src) : 0;
    if(n >= size) n = size - 1;
    if(n) memcpy(dst, src, n);
    dst[n] = '\0';
}

void ui_message(const char* title, const char* line1, const char* line2) {
    copy(msg_title, sizeof(msg_title), title);
    copy(msg_l1, sizeof(msg_l1), line1);
    copy(msg_l2, sizeof(msg_l2), line2);
    ui_push(&msg_screen);
}
