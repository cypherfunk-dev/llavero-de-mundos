/* Interfaz común de los mundos (spec §9.2). */
#ifndef LLV_WORLD_H
#define LLV_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "state.h"

/* Contexto efímero de un frame: nunca se guarda. */
typedef struct {
    int64_t now;   /* segundos locales */
    uint32_t ms;   /* reloj monótono en ms, para animaciones */
    uint8_t index; /* posición del mundo en el ciclo de B */
} WorldCtx;

typedef struct {
    const char *name;
    uint8_t daily_actions;                          /* 0 = sin límite */
    void (*tick)(GameState *gs);                    /* macro tick determinista; puede ser NULL */
    void (*render)(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb);
    void (*on_tap)(GameState *gs, const WorldCtx *ctx);  /* puede ser NULL */
    void (*on_hold)(GameState *gs, const WorldCtx *ctx); /* puede ser NULL */
} World;

/* true si 'until' aún no ha llegado; tolera que el contador de ms dé la vuelta (~49 días). */
static inline bool ms_pending(uint32_t ms, uint32_t until)
{
    return (int32_t)(until - ms) > 0;
}

extern const World world_reloj;
extern const World world_jardin;
extern const World world_sala;
extern const World world_musica;
extern const World world_pesca;
extern const World world_cobertizo;

/* Expuesto para tests: "Son las diez y veinticuatro de la mañana." */
void reloj_time_in_words(char *buf, size_t n, int hour, int minute);

#endif
