#pragma once
/* A list of "label: value" rows; Left/Right changes a value, OK activates. */
#include "ui.h"

typedef struct {
    const char* label;
    void (*value)(char* buf, size_t size); /* NULL: action row */
    void (*change)(int dir, bool fine); /* dir: -1 / +1; fine: key is being held */
    void (*activate)(void);
} UiOption;

bool ui_options_input(UiList* list, const UiOption* opts, InputEvent ev);
void ui_options_draw(const UiList* list, const UiOption* opts);
