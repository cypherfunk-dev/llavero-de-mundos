/* Motor del llavero: no sabe nada del hardware; la HAL le pasa entradas y tiempo. */
#ifndef LLV_ENGINE_H
#define LLV_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "input.h"
#include "state.h"
#include "world.h"

typedef struct {
    uint8_t buttons; /* BTN_A | BTN_B */
    bool shake;
} RawInput;

typedef struct {
    uint16_t freq;
    uint16_t ms;
} Tone;

#define ENGINE_MAX_TONES 4

typedef struct {
    bool screen_on;
    bool save_now;
    Tone tones[ENGINE_MAX_TONES];
    uint8_t tone_count;
} FrameOut;

typedef struct {
    GameState gs;
    InputState in;
    Framebuffer fb;
    bool asleep;
    uint32_t last_activity_ms;
    uint32_t collapse_until_ms;
    uint32_t diary_until_ms;
    uint32_t toast_until_ms;
    char toast[96];
    Rng fx; /* aleatoriedad solo visual: nunca entra en el estado guardado */
} Engine;

/* Universo nuevo. */
void engine_init(Engine *e, uint64_t seed, int64_t now, uint32_t ms);
/* Universo cargado de un guardado: se despierta (catch-up + colapso). */
void engine_resume(Engine *e, const GameState *loaded, int64_t now, uint32_t ms);
FrameOut engine_frame(Engine *e, RawInput in, int64_t now, uint32_t ms);
void engine_sleep(Engine *e, int64_t now);

/* Avanza el universo hasta 'now' en macro ticks (tope LLV_CATCHUP_MAX_TICKS).
 * Puro y determinista: es lo que se usa tanto en vivo como al despertar. */
int64_t engine_advance(GameState *gs, int64_t now);

const World *engine_world(int index);
void engine_toast(Engine *e, const char *text, uint32_t ms, uint32_t duration);

#endif
