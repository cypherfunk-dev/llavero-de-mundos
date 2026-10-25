/* Motor del llavero: no sabe nada del hardware; la HAL le pasa entradas y tiempo. */
#ifndef LLV_ENGINE_H
#define LLV_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "input.h"
#include "music.h"
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

    /* Fase 4: el sintetizador. Efímero: lo que se guarda es gs.mus (semilla, mutaciones, ánimo). */
    Music music;

    /* Estudio de la música (v1.1): efímero; lo que se guarda son los ajustes (gs.mus.studio). */
    bool studio_open;
    uint8_t studio_row;          /* fila seleccionada (StudioRow) */
    uint32_t studio_until_ms;    /* se cierra solo cuando llega */
    uint32_t atap_ms;            /* último toque de A en Música (para el doble toque que abre el Estudio) */
    uint8_t atap_prev_mut;       /* las mutaciones que había antes de ese toque */
    bool atap_valid;

    /* Fase 5: aviso pendiente para cuando despierte (p. ej. la pesca que se cortó al dormirse). */
    char wake_msg[96];
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

/* Música (fase 4, spec §4.4). La plataforma saca el audio llamando a esto cuando su salida pide
 * muestras (hal_audio_wanted/hal_audio_push) y, si no hay salida, a ritmo del reloj: así el
 * visualizador sigue siendo lógico. Devuelve cuántas muestras PCM mono (LLV_AUDIO_RATE) ha escrito;
 * 0 si no suena nada (la música solo suena en el mundo Música y con el llavero despierto; al salir o
 * dormir se desvanece en LLV_MUSIC_FADE_MS y calla). No toca el estado del universo.
 *
 * Quién manda: los pitidos de la interfaz (FrameOut.tones) mandan sobre la música: se mezclan por
 * encima y, mientras suenan, el motor baja la música (music_duck). La música se apaga sola cuando
 * hace falta. */
size_t engine_audio_render(Engine *e, int16_t *buf, size_t max);

/* Avanza el universo hasta 'now' en macro ticks (tope LLV_CATCHUP_MAX_TICKS).
 * Puro y determinista: es lo que se usa tanto en vivo como al despertar. */
int64_t engine_advance(GameState *gs, int64_t now);

const World *engine_world(int index);
/* Estudio: ¿está abierto? (solo se abre en el mundo Música; efímero, no se guarda). */
bool engine_studio_open(const Engine *e);
void engine_studio_close(Engine *e);
void engine_toast(Engine *e, const char *text, uint32_t ms, uint32_t duration);

#endif
