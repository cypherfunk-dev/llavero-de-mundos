/* Convierte el estado crudo de los botones en gestos (spec §2). */
#ifndef LLV_INPUT_H
#define LLV_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define BTN_A 0x01u
#define BTN_B 0x02u

typedef enum {
    GEST_NONE,
    GEST_A_TAP,
    GEST_A_HOLD,
    GEST_B_TAP,
    GEST_B_HOLD,
    GEST_AB,
} Gesture;

typedef struct {
    uint8_t down;          /* botones pulsados en la última actualización */
    uint32_t pressed_at[2];
    bool consumed[2];      /* el botón ya produjo un gesto: al soltarlo no hay toque */
} InputState;

void input_init(InputState *in);
/* Llamar una vez por frame. Devuelve como mucho un gesto. */
Gesture input_update(InputState *in, uint8_t buttons, uint32_t now_ms);
/* Ignora los botones pulsados ahora hasta que se suelten (p. ej. el que despertó). */
void input_swallow(InputState *in);

#endif
