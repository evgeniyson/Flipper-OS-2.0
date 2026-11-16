#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, KEY_BACK, KEY_COUNT } Key;

typedef enum {
    INPUT_PRESS, /* key went down */
    INPUT_RELEASE,
    INPUT_SHORT, /* released before the long-press threshold */
    INPUT_LONG, /* held for INPUT_LONG_MS */
    INPUT_REPEAT, /* still held; fires every INPUT_REPEAT_MS after LONG */
} InputType;

typedef struct {
    uint8_t key;
    uint8_t type;
} InputEvent;

#define INPUT_DEBOUNCE_MS 20
#define INPUT_LONG_MS     450
#define INPUT_REPEAT_MS   110

/* Turn raw key states (bit n = key n is down) into events.
 * Call regularly; pop events until input_pop() returns false. */
void input_update(uint8_t raw_mask, uint32_t now_ms);
bool input_pop(InputEvent* ev);
