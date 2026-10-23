/* El Director de Sorpresas (spec §7) y el bono de paciencia (spec §3.2).
 * En cada mirada que cuente (pasados LLV_DIR_COOLDOWN_S desde la anterior) tira un dado con
 * gs->rng: normal, comentario del universo, sorpresa (§6) o algo muy raro. Reglas: nunca dos
 * sorpresas seguidas, seguro de pena (tras un día entero sin sorpresa, la siguiente mirada trae
 * una) y elige el mundo que se abre al despertar. Todo se anota en GameState.dir. */
#ifndef LLV_DIRECTOR_H
#define LLV_DIRECTOR_H

#include <stdbool.h>

#include "phrases.h"
#include "state.h"

/* Se llama una vez al despertar, tras el catch-up. 'slept' son los segundos desde la mirada
 * anterior; 'first' es la primera mirada de la vida del llavero (sin sorpresas). */
void director_wake(GameState *gs, const PhraseEnv *env, int64_t slept, bool first);
/* Bono de paciencia: un tick mientras duerme (spec §3.2). */
void director_patience(GameState *gs);
/* Fuerza una sorpresa (tests, herramientas y el código secreto). */
void director_force(GameState *gs, const PhraseEnv *env, int surprise, int arg);
const char *surprise_name(int surprise);
/* Frase fija de una sorpresa (cartel, glitch...): índice en el banco o -1. */
int director_phrase(const GameState *gs, uint32_t kinds, int speaker);
/* Mundo que habría que abrir ahora según las novedades (-1: el que estaba). */
int director_pick_world(const GameState *gs);

#endif
