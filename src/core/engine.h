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
    bool led;        /* LED de aviso encendido: pasó algo raro mientras no mirabas (spec §3.2) */
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
    char toast[192];
    Rng fx; /* aleatoriedad solo visual: nunca entra en el estado guardado */

    /* Fase 3: todo lo de abajo es efímero (visual o de entrada); nada entra en el guardado. */
    uint32_t shake_until_ms;     /* el último agitado, para las frases de "agitar" */
    uint32_t collapse_total_ms;  /* duración del colapso en curso */
    uint32_t glitch_until_ms;    /* el cartucho sucio se arregla solo */
    uint32_t boot_start_ms, boot_until_ms;
    uint32_t coin_until_ms;
    uint8_t seen_cur;            /* última sorpresa de la que el motor ya se enteró */
    char code_hist[8];           /* últimos toques del código secreto */
    uint8_t code_n;
    uint8_t code_prog;           /* cuántos toques del código lleva acertados */
    uint32_t code_last_ms;
    GameState code_snap;         /* estado antes de la primera A del código (por si se completa) */
    bool code_snap_valid;
    uint8_t battery;
    bool charging;
    uint32_t steps;
    FrameOut *out;               /* donde los mundos dejan sus pitidos durante un fotograma */
} Engine;

/* Universo nuevo. */
void engine_init(Engine *e, uint64_t seed, int64_t now, uint32_t ms);
/* Universo cargado de un guardado: se despierta (catch-up + colapso). */
void engine_resume(Engine *e, const GameState *loaded, int64_t now, uint32_t ms);
FrameOut engine_frame(Engine *e, RawInput in, int64_t now, uint32_t ms);
/* El cuerpo del llavero (spec §5.1, §3.3): la HAL lo lee de los sensores y se lo pasa antes de
 * cada fotograma. battery 1..100 (0 = no se sabe), charging, y el podómetro del IMU: un
 * contador que solo crece (el motor reparte las diferencias por día; 0 = sin sensor). */
void engine_set_body(Engine *e, uint8_t battery, bool charging, uint32_t steps);
void engine_sleep(Engine *e, int64_t now);

/* Avanza el universo hasta 'now' en macro ticks (tope LLV_CATCHUP_MAX_TICKS).
 * Puro y determinista: es lo que se usa tanto en vivo como al despertar. */
int64_t engine_advance(GameState *gs, int64_t now);

const World *engine_world(int index);
void engine_toast(Engine *e, const char *text, uint32_t ms, uint32_t duration);

#endif
