#include "main.h"
#include "settings.h"
#include "ui.h"
#include "../apps/apps.h"

void fos_start(void) {
    settings_load();
    settings_apply();
    ui_init(&menu_screen);
}

void fos_step(void) {
    ui_step();
    hal_idle();
}
