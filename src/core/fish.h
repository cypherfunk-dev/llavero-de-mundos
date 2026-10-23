/* Pesca (spec §4.5): las especies como tabla de datos (fish_data.c), el motor de selección por
 * contexto, el lanzamiento (gasta una lombriz, 2 al día), el resultado de pescar y el minijuego de
 * un botón. Todo es puro y determinista: aritmética entera, paso fijo y un PCG32 propio que se
 * siembra UNA vez desde gs->rng al lanzar la caña. El minijuego en curso no se guarda. */
#ifndef LLV_FISH_H
#define LLV_FISH_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "state.h"

#define FISH_SPECIES LLV_FISH_SPECIES  /* 0..5: peces de la colección (COLL_PEZ0 + n) */
#define FISH_JUNK 4                    /* basuras simpáticas: bota, lata, alga, calcetín */
#define FISH_TOTAL (FISH_SPECIES + FISH_JUNK)
#define FISH_LEGEND 5                  /* el Koi Lunar: el pez legendario de la luna llena */

/* Contexto de la pesca: cuándo y con qué clima se lanza (bits). */
enum {
    FC_DAY = 1, FC_NIGHT = 2, FC_RAIN = 4, FC_SNOW = 8, FC_FULLMOON = 16, FC_WINTER = 32, FC_WIND = 64,
};

/* Temperamento: cómo se mueve el pez en el minijuego. */
typedef enum { FT_CALM, FT_LAZY, FT_JUMPY, FT_ERRATIC, FT_DARTER, FT_COUNT } FishTemper;

/* Qué deja un pez además de entrar en la colección (barato: nada que desequilibre el universo). */
enum { FG_JOY = 1, FG_COMPOST = 2 };

typedef struct {
    const char *name;      /* "Carpa Resignada" */
    const char *tag;       /* para el diario: cabe en ~8 letras */
    const char *humor;     /* ficha: "humor: resignado" */
    const char *quip;      /* ficha: el comentario */
    uint8_t junk;          /* 1 = basura simpática: no cuenta en la colección */
    uint8_t weight;        /* rareza: peso base (100 común ... 4 rarísimo) */
    uint8_t diff;          /* dificultad 1..10: zona más pequeña, pez más rápido */
    uint8_t temper;        /* FishTemper */
    uint8_t smin, smax;    /* tamaño en cm */
    uint8_t need;          /* FC_* que deben cumplirse todos para poder salir */
    uint8_t forbid;        /* FC_* que lo impiden (ninguno puede estar) */
    uint8_t seasons;       /* máscara SEAS(): estaciones en que sale */
    uint8_t boost_if;      /* si alguno de estos FC_* está activo... */
    uint8_t boost_pct;     /* ...el peso se multiplica por esto/100 (200 = el doble, 50 = la mitad) */
    uint8_t gift_item;     /* ItemKind que cae a la bolsa si cabe (0: nada) */
    uint8_t gift;          /* FG_* */
} FishInfo;

extern const FishInfo FISH_TABLE[FISH_TOTAL];

const FishInfo *fish_info(int idx);
const char *fish_name(int idx);
const char *fish_temper_name(int temper);
/* FC_* de este instante del universo (hora, clima, estación y luna del macro tick actual). */
int fish_ctx(const GameState *gs);
/* Estación (Season) del macro tick actual. */
int fish_season(const GameState *gs);
/* Peso efectivo con ese contexto: 0 = no puede salir. */
int fish_weight(const FishInfo *f, int ctx, int season);
/* Elige qué pica (0..FISH_TOTAL-1). Genérico y determinista: solo gasta el Rng que se le da. */
int fish_pick(int ctx, int season, Rng *r);
/* Tamaño al azar en cm (smin..smax; 0 para las basuras). */
int fish_size(int idx, Rng *r);

/* ---------- lanzar y resolver ---------- */

typedef enum { CAST_OK, CAST_NO_ACTIONS, CAST_NO_WORM } CastResult;

/* Lo que ocurre al lanzar la caña: todo lo que decide el azar, ya sorteado. */
typedef struct {
    uint8_t idx;           /* qué ha picado (0..FISH_TOTAL-1) */
    uint8_t size;          /* tamaño en cm */
    uint16_t wait_ms;      /* cuánto tarda en picar */
    uint32_t game_seed;    /* semilla del minijuego */
} FishPlan;

/* Lanza la caña: comprueba el límite diario (LLV_PESCA_ACTIONS) y que haya una Lombriz en la bolsa
 * (la preparada cuenta; si lo preparado es otra cosa, se queda preparado), la gasta, anota
 * EV_CAST y sortea el lanzamiento con UNA tirada de gs->rng. 'widx' es la posición de la Pesca en
 * el ciclo. Con CAST_NO_* no se toca nada. */
CastResult fish_cast(GameState *gs, int widx, FishPlan *plan, bool *used_prepared);
/* El sorteo de un lanzamiento a partir de su semilla y el contexto (puro; lo usa fish_cast). */
void fish_plan(FishPlan *p, uint32_t seed, int ctx, int season);

/* Resultado de un lanzamiento. */
typedef struct {
    bool won;
    bool new_species;      /* primer pez de esa especie: entra en la colección */
    bool record;           /* récord de tamaño de esa especie */
    bool legend;
    bool complete;         /* con este pez, los 6 están en la colección */
    uint8_t gift_item;     /* objeto que ha caído a la bolsa (0: ninguno o no cupo) */
    bool joy;              /* la mascota se ha alegrado */
    bool compost;          /* la compostera ha aceptado los restos */
} FishResult;

/* Aplica el desenlace: anota el diario (EV_FISH, EV_FISH_LEGEND, EV_FISH_JUNK o EV_FISH_LOST),
 * actualiza capturas y récords, llena la colección y reparte lo barato (objeto, alegría, restos). */
void fish_resolve(GameState *gs, const FishPlan *plan, bool won, FishResult *out);

/* Total de peces distintos en la colección (0..FISH_SPECIES). */
int fish_collected(const GameState *gs);
int fish_total_caught(const GameState *gs);

/* ---------- el minijuego ---------- */

/* Posiciones 0..1000 (abajo = 0). Un paso son LLV_FISH_STEP_MS. */
typedef struct {
    Rng rng;                 /* PRNG propio, sembrado una vez */
    int16_t fpos, fvel;      /* el pez */
    int16_t target;
    uint16_t target_left;
    int16_t bar, bvel;       /* centro de la barra del jugador */
    int16_t zone;            /* alto de la barra */
    int16_t meter;           /* medidor de captura 0..1000 */
    uint16_t steps;
    uint8_t temper, diff;
    uint8_t state;           /* FG_PLAYING / FG_WON / FG_LOST */
} FishGame;

enum { FGS_PLAYING, FGS_WON, FGS_LOST };

void fishgame_init(FishGame *g, uint32_t seed, int temper, int diff);
/* Un paso de LLV_FISH_STEP_MS. hold = A pulsado. Devuelve el estado. */
int fishgame_step(FishGame *g, bool hold);
bool fishgame_in_zone(const FishGame *g);
/* Un jugador simulado perfecto: mira al pez y decide si mantener A (para los tests y la herramienta
 * headless; nunca se usa en el juego). Mantiene al pez en la zona el tiempo suficiente para ganar. */
bool fishgame_ai(const FishGame *g);

/* ---------- arte ---------- */

/* Sprite de 16x12 de un pez o basura (idx de FISH_TABLE). known = false: silueta "???". */
void art_fish(Framebuffer *fb, int x, int y, int idx, bool known, bool flip);
/* Versión de la estantería del Cobertizo (12x9 aprox.). */
void art_fish_mini(Framebuffer *fb, int x, int y, int idx, bool known);
/* A doble tamaño (32x24), para la ficha y la tarjeta de lo pescado. */
void art_fish_big(Framebuffer *fb, int x, int y, int idx, bool known);
/* Silueta de un solo color: la sombra del pez bajo el agua. */
void art_fish_tint(Framebuffer *fb, int x, int y, int idx, Color c, bool flip);
/* Comprueba las filas de todos los sprites de peces (para los tests). */
int art_fish_selfcheck(void);

#endif
