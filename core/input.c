#include "input.h"

#define QUEUE 16
static InputEvent queue[QUEUE];
static uint8_t q_head, q_tail;

static struct {
    bool raw, stable, long_sent;
    uint32_t changed_at, pressed_at, repeat_at;
} keys[KEY_COUNT];

static void push(uint8_t key, uint8_t type) {
    uint8_t next = (uint8_t)((q_head + 1) % QUEUE);
    if(next == q_tail) return; /* full: drop */
    queue[q_head] = (InputEvent){key, type};
    q_head = next;
}

void input_update(uint8_t raw_mask, uint32_t now) {
    for(uint8_t k = 0; k < KEY_COUNT; k++) {
        bool raw = raw_mask >> k & 1;
        if(raw != keys[k].raw) {
            keys[k].raw = raw;
            keys[k].changed_at = now;
        }
        if(raw != keys[k].stable && now - keys[k].changed_at >= INPUT_DEBOUNCE_MS) {
            keys[k].stable = raw;
            if(raw) {
                keys[k].pressed_at = now;
                keys[k].long_sent = false;
                push(k, INPUT_PRESS);
            } else {
                if(!keys[k].long_sent) push(k, INPUT_SHORT);
                push(k, INPUT_RELEASE);
            }
        }
        if(keys[k].stable) {
            if(!keys[k].long_sent && now - keys[k].pressed_at >= INPUT_LONG_MS) {
                keys[k].long_sent = true;
                keys[k].repeat_at = now;
                push(k, INPUT_LONG);
            } else if(keys[k].long_sent && now - keys[k].repeat_at >= INPUT_REPEAT_MS) {
                keys[k].repeat_at = now;
                push(k, INPUT_REPEAT);
            }
        }
    }
}

bool input_pop(InputEvent* ev) {
    if(q_tail == q_head) return false;
    *ev = queue[q_tail];
    q_tail = (uint8_t)((q_tail + 1) % QUEUE);
    return true;
}
