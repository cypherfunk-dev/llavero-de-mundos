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
    uint8_t buttons; /* botones pulsados ahora (BTN_A | BTN_B), p. ej. para respirar */
    /* El cuerpo del llavero (lo pone la HAL, spec §5.1) y estado de la mirada. */
    bool shaken;       /* lo han agitado hace un momento */
    uint8_t battery;   /* 1..100; 0 = no se sabe */
    bool charging;
    bool absent;       /* sorpresa "salí, vuelvo en 5 min": este mundo está vacío */
    /* Pitido (spec §5.2: un pitido por letra). Puede ser NULL (tests). */
    void (*tone)(void *user, uint16_t freq, uint16_t ms);
    /* Mensaje breve sobre la escena (lo dibuja el motor). Puede ser NULL (tests). */
    void (*toast)(void *user, const char *text, uint32_t ms, uint32_t duration);
    void *user;
} WorldCtx;

static inline void world_toast(const WorldCtx *ctx, const char *text, uint32_t duration)
{
    if (ctx->toast)
        ctx->toast(ctx->user, text, ctx->ms, duration);
}

typedef struct {
    const char *name;
    uint8_t daily_actions;                          /* 0 = sin límite */
    void (*tick)(GameState *gs);                    /* macro tick determinista; puede ser NULL */
    void (*render)(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb);
    void (*on_tap)(GameState *gs, const WorldCtx *ctx);  /* puede ser NULL */
    void (*on_hold)(GameState *gs, const WorldCtx *ctx); /* puede ser NULL */
    void (*on_shake)(GameState *gs, const WorldCtx *ctx);/* agitar con este mundo abierto; puede ser NULL */
} World;

/* true si 'until' aún no ha llegado; tolera que el contador de ms dé la vuelta (~49 días).
 * until == 0 significa "nada pendiente": sin esto, pasadas ~25 días de ms (la mitad del
 * rango) un temporizador a cero parecería estar siempre en marcha. */
static inline bool ms_pending(uint32_t ms, uint32_t until)
{
    return until != 0 && (int32_t)(until - ms) > 0;
}

extern const World world_reloj;
extern const World world_jardin;
extern const World world_sala;
extern const World world_musica;
extern const World world_pesca;
extern const World world_cobertizo;

/* Ayudas de interfaz del Cobertizo para tests y herramientas: el cursor es solo visual. */
void jardin_ui_reset(void);
void cobertizo_ui_reset(void);
void sala_ui_reset(void);
/* El menú de hábitos del Cobertizo (mantener A sobre la colección). */
bool cobertizo_menu_open(void);
bool cobertizo_on_pet(const GameState *gs);
bool cobertizo_breathing(void);

/* Expuesto para tests: "Son las diez y veinticuatro de la mañana." */
void reloj_time_in_words(char *buf, size_t n, int hour, int minute);

#endif
