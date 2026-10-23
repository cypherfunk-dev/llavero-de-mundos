/* La conversación de la Sala (spec §4.3): dar pie, escenas a dos voces, preguntas de sí o no
 * con memoria, visitantes y comentarios del universo. Todo es función del GameState y de
 * gs->rng (nada de variables estáticas): el guardado y el determinismo lo cubren. */
#ifndef LLV_TALK_H
#define LLV_TALK_H

#include <stdbool.h>
#include <stddef.h>

#include "phrases.h"
#include "state.h"

typedef enum {
    TALK_NONE,       /* no hizo nada */
    TALK_STARTED,    /* conversación nueva (gasta una de las 2 del día) */
    TALK_ADVANCED,   /* siguiente línea de la escena (gratis) */
    TALK_ANSWERED,   /* contestó una pregunta (gratis) */
    TALK_FILLER,     /* sin conversaciones: frase de relleno con gracia */
    TALK_BELL,       /* el timbre: entra un visitante (gasta una conversación) */
} TalkResult;

/* A (toque): sí si hay una pregunta; si no, sigue la escena o da pie a una conversación. */
TalkResult talk_tap(GameState *gs, const PhraseEnv *env);
/* A (mantener): no si hay una pregunta; si no, toca el timbre. */
TalkResult talk_hold(GameState *gs, const PhraseEnv *env);

/* Frases sueltas que el Director o el motor ponen sin gastar conversación. Devuelven false
 * si no hay ninguna que encaje. */
bool talk_comment(GameState *gs, const PhraseEnv *env);
bool talk_say(GameState *gs, const PhraseEnv *env, uint32_t kinds, uint32_t speakers, int karg);
/* Agitar con la Sala abierta: alguien protesta (gratis; no interrumpe una pregunta). */
bool talk_shake(GameState *gs, const PhraseEnv *env);
/* Un visitante entra (ticks > 0: cuánto se queda). log: lo anota en el diario. */
void talk_visitor(GameState *gs, int spk, int ticks, bool log);
/* El Gato de Schrödinger asoma y dice algo. */
void talk_gato(GameState *gs, const PhraseEnv *env);
/* Tras una ausencia larga la conversación pendiente se olvida. */
void talk_on_wake(GameState *gs, int64_t slept_seconds);

/* Lo que se está diciendo ahora (para pintarlo). false si no hay nada. */
bool talk_text(const GameState *gs, char *buf, size_t n);
bool talk_picaro(const GameState *gs);
/* Un tick de la Sala: los visitantes llegan y se van (World.tick). */
void sala_tick(GameState *gs);

/* Preguntas: texto de metadatos. */
int talk_followup_days(int q);

#endif
