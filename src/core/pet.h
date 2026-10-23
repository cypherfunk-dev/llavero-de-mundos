/* La mascota anti-Tamagotchi (spec §4.7): ridícula, lenta y sin prisa. Nunca muere.
 * Todo el estado vive en GameState.pet; aquí está la lógica y los textos de la ficha. */
#ifndef LLV_PET_H
#define LLV_PET_H

#include <stdbool.h>

#include "state.h"

/* Elige la mascota (solo si aún no hay una). true si se eligió. */
bool pet_choose(GameState *gs, PetKind kind);
/* Un macro tick de la mascota (World.tick del Cobertizo). */
void pet_tick(GameState *gs);

const char *pet_name(PetKind kind);          /* "HONGO" */
const char *pet_blurb(PetKind kind);         /* una línea para la pantalla de elección */
int pet_stage(const Pet *p);                 /* 0..pet_stage_count-1 */
int pet_stage_count(PetKind kind);
const char *pet_stage_name(const Pet *p);
int pet_day(const Pet *p);                   /* día del ciclo, desde 1 */
int pet_height_tenths(const Pet *p);         /* décimas de píxel (hongo y semilla) */
int pet_worms(const Pet *p);                 /* compostera */
const char *pet_mood(const Pet *p);
const char *pet_thought(const Pet *p);       /* "" si nunca dice nada */

/* Líneas de la ficha solemne (cada una cabe en la pantalla): devuelve cuántas escribió. */
int pet_ficha(const Pet *p, char lines[][24], int max);

/* Alegra a la mascota (humor +amount, tope 255): un pez raro, algo barato del resto del universo. */
void pet_cheer(GameState *gs, int amount);

/* Gusto de cada mascota por cada objeto: -1 raro, 0 le da igual, 1 le gusta, 2 le encanta. */
int pet_like(PetKind kind, ItemKind item);

typedef enum { CARE_OK, CARE_NONE_LEFT, CARE_NO_PET } CareResult;
/* El cuidado diario: ofrece el objeto preparado (si hay) o saluda. 'widx' es la
 * posición del Cobertizo en el ciclo. 'msg' recibe el texto para el toast. */
CareResult pet_care(GameState *gs, int widx, char *msg, size_t n);

#endif
