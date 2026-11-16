#include "options.h"

bool ui_options_input(UiList* list, const UiOption* opts, InputEvent ev) {
    if(ui_list_input(list, ev, UI_ROWS)) return true;
    const UiOption* o = &opts[list->sel];
    if((ev.key == KEY_LEFT || ev.key == KEY_RIGHT) && o->change) {
        if(ev.type == INPUT_SHORT) o->change(ev.key == KEY_LEFT ? -1 : 1, false);
        if(ev.type == INPUT_REPEAT) o->change(ev.key == KEY_LEFT ? -1 : 1, true);
        return true;
    }
    if(ev.key == KEY_OK && ev.type == INPUT_SHORT) {
        if(o->activate)
            o->activate();
        else if(o->change)
            o->change(1, false);
        return true;
    }
    return false;
}

static void row(int idx, int y, bool selected, void* ctx) {
    const UiOption* o = &((const UiOption*)ctx)[idx];
    char val[20] = "";
    if(o->value) o->value(val, sizeof(val));
    ui_list_row(y, selected, o->label, o->value ? val : ">");
}

void ui_options_draw(const UiList* list, const UiOption* opts) {
    ui_list_draw(list, UI_TOP, UI_ROWS, row, (void*)opts);
}
