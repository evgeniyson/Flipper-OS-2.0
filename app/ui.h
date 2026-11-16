#pragma once
/* Screen manager: a stack of screens, the status bar and a few shared widgets. */
#include "../core/input.h"
#include "../ui/canvas.h"

typedef struct {
    const char* title;
    void (*enter)(void);
    void (*leave)(void);
    /* Return false to let the manager handle the event: a short BACK press
     * closes the screen. */
    bool (*input)(InputEvent ev);
    void (*tick)(uint32_t now_ms);
    void (*draw)(void);
} Screen;

void ui_init(const Screen* root);
void ui_push(const Screen* screen);
void ui_pop(void);
void ui_dirty(void); /* request a redraw */
void ui_toast(const char* text); /* short message over the current screen */
void ui_step(void); /* one pass of the main loop */

/* content area below the status bar */
#define UI_TOP  10
#define UI_ROWS 6 /* list rows of UI_ROW_H pixels */
#define UI_ROW_H 9

typedef struct {
    int count, sel, top;
} UiList;

/* Up/Down navigation with auto-repeat. Returns true if the event was used. */
bool ui_list_input(UiList* list, InputEvent ev, int visible);
typedef void (*UiListItem)(int index, int y, bool selected, void* ctx);
void ui_list_draw(const UiList* list, int y, int visible, UiListItem item, void* ctx);
/* Convenience: one text per row, highlighted when selected. */
void ui_list_row(int y, bool selected, const char* left, const char* right);

void ui_format_freq(char* buf, size_t size, uint32_t hz); /* "433.92" */
void ui_bar(int x, int y, int w, int h, int value, int max);

/* Full-screen message with an optional confirmation. */
void ui_message(const char* title, const char* line1, const char* line2);
