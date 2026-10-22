#include "input.h"

#include <string.h>

#include "config.h"

void input_init(InputState *in)
{
    memset(in, 0, sizeof(*in));
}

void input_swallow(InputState *in)
{
    if (in->down & BTN_A)
        in->consumed[0] = true;
    if (in->down & BTN_B)
        in->consumed[1] = true;
}

Gesture input_update(InputState *in, uint8_t buttons, uint32_t now_ms)
{
    static const uint8_t MASK[2] = {BTN_A, BTN_B};
    uint8_t pressed = (uint8_t)(buttons & ~in->down);
    uint8_t released = (uint8_t)(in->down & ~buttons);

    for (int i = 0; i < 2; i++) {
        if (pressed & MASK[i]) {
            in->pressed_at[i] = now_ms;
            in->consumed[i] = false;
        }
    }
    uint8_t prev = in->down;
    in->down = buttons;

    /* A+B: ambos pulsados y el segundo llegó poco después del primero. */
    if ((buttons & (BTN_A | BTN_B)) == (BTN_A | BTN_B) && (pressed & (BTN_A | BTN_B)) &&
        !in->consumed[0] && !in->consumed[1]) {
        uint32_t a = in->pressed_at[0], b = in->pressed_at[1];
        uint32_t gap = a > b ? a - b : b - a;
        if (gap <= LLV_COMBO_MS) {
            in->consumed[0] = in->consumed[1] = true;
            return GEST_AB;
        }
    }

    for (int i = 0; i < 2; i++) {
        if ((released & MASK[i]) && (prev & MASK[i])) {
            bool was_consumed = in->consumed[i];
            in->consumed[i] = false;
            if (!was_consumed)
                return i == 0 ? GEST_A_TAP : GEST_B_TAP;
        }
    }

    for (int i = 0; i < 2; i++) {
        if ((buttons & MASK[i]) && !in->consumed[i] &&
            now_ms - in->pressed_at[i] >= LLV_HOLD_MS) {
            in->consumed[i] = true;
            return i == 0 ? GEST_A_HOLD : GEST_B_HOLD;
        }
    }
    return GEST_NONE;
}
