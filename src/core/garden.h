/* Jardín (spec §4.2): motor de reglas genérico + tablas de datos.
 * Las reglas viven en garden_data.c: aquí solo está el formato y la API. */
#ifndef LLV_GARDEN_H
#define LLV_GARDEN_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"
#include "timeutil.h"

typedef enum {
    SP_NONE,
    SP_SEMILLA,
    SP_BROTE,
    SP_ARBOL_JOVEN,
    SP_ARBOL,        /* etapas: 0 árbol, 1 árbol con fruta, 2 árbol viejo */
    SP_MANZANA,      /* 0 fresca, 1 podrida */
    SP_HONGO,        /* 0 pequeño, 1 adulto */
    SP_FLOR,         /* 0 capullo, 1 flor, 2 marchita, 3 flor rara */
    SP_ABEJA,
    SP_PAJARO,       /* 0 de paso, 1 atraído por el alpiste */
    SP_LOMBRIZ,
    SP_CHARCO,
    SP_RANA,
    SP_PIEDRA,       /* piedra con musgo */
    SP_HOJAS,        /* hojas secas */
    SP_NIEVE,
    SP_COUNT
} Species;

#define GARDEN_STAGES 4
#define GARDEN_OUTCOMES 3
#define ST_ANY 255

/* Banderas de una regla. */
enum {
    RF_WET = 1,          /* con lluvia, el reloj corre LLV_WET_MULT veces más rápido */
    RF_SLOW_WINTER = 2,  /* en invierno solo avanza 1 de cada 4 ticks */
    RF_ONLY_WINTER = 4,  /* fuera del invierno se acaba enseguida (nieve) */
};

enum { EFF_NONE, EFF_ABONO };  /* efectos especiales de un resultado */

/* Un posible resultado de una etapa, de una reacción o de una interacción. */
typedef struct {
    uint8_t pct;     /* probabilidad en %; la suma de una fila es 100. 0 = fin de la lista */
    uint8_t sp, st;  /* en qué se convierte (SP_NONE: desaparece; igual que ahora: se queda) */
    uint8_t sp2, st2;/* además, aparece esto en otra casilla libre (SP_NONE: nada) */
    uint8_t near;    /* 1: la casilla de lo que aparece es vecina; 0: cualquiera */
    uint8_t ev;      /* EventKind que se anota (0: ninguno) */
    uint8_t item;    /* ItemKind que cae a la bolsa si cabe (0: ninguno) */
    uint8_t eff;     /* EFF_* */
} Outcome;

/* Una fila de la tabla (spec §4.2): "entidad etapa dura siguiente". */
typedef struct {
    const char *name;   /* para la lupa; NULL = esta etapa no existe */
    const char *then;   /* lo que pasará ("se pudre"); NULL = estable */
    uint16_t dur;       /* ticks hasta lo siguiente; 0 = estable */
    uint8_t flags;      /* RF_* */
    Outcome next[GARDEN_OUTCOMES];
} GardenRule;

typedef enum { TRG_WIND } Trigger;

/* Algo que le pasa a una etapa cuando ocurre un suceso (viento...). */
typedef struct {
    uint8_t sp, st;     /* sp == SP_NONE termina la tabla */
    uint8_t trig;
    uint8_t pct;        /* probabilidad base; se escala con la fuerza del suceso */
    Outcome out;
} GardenReact;

/* Un actor busca un objetivo en cualquier casilla y, con cierta probabilidad por tick, actúa. */
typedef struct {
    uint8_t asp, ast;   /* actor; asp == SP_NONE termina la tabla */
    uint8_t tsp, tst;   /* objetivo (tst puede ser ST_ANY) */
    uint8_t pct;
    uint8_t rsp, rst;   /* en qué queda el objetivo (SP_NONE: desaparece) */
    Outcome actor;      /* qué le pasa al actor */
} GardenInteract;

/* Condiciones de una aparición espontánea. */
enum {
    SW_DAY = 1, SW_NIGHT = 2, SW_RAIN = 4, SW_DRY = 8, SW_WIND = 16, SW_SNOW = 32,
};
#define SEAS(s) (1u << (s))
#define SEAS_ALL 0x0F

typedef struct {
    uint8_t sp, st;     /* sp == SP_NONE termina la tabla */
    uint8_t permille;   /* probabilidad por tick */
    uint8_t when;       /* SW_*: todas deben cumplirse */
    uint8_t seasons;    /* máscara de estaciones */
    uint8_t need_sp, need_st;  /* requiere que exista esto (SP_NONE: nada) */
    uint8_t ev;
    uint8_t item;
} GardenSpawn;

typedef struct {
    uint8_t cap;        /* máximo de esta especie a la vez */
    uint8_t group;      /* 0 = sola; 1 = árboles (comparten el tope LLV_TREE_MAX) */
} SpeciesInfo;

typedef struct {
    uint8_t sp, st;     /* sp == SP_NONE termina la tabla */
    uint8_t slot;       /* CollSlot */
} GardenDiscover;

/* Tablas (garden_data.c). */
extern const GardenRule GARDEN_RULES[SP_COUNT][GARDEN_STAGES];
extern const GardenReact GARDEN_REACT[];
extern const GardenInteract GARDEN_INTERACT[];
extern const GardenSpawn GARDEN_SPAWN[];
extern const SpeciesInfo GARDEN_SPECIES[SP_COUNT];
extern const GardenDiscover GARDEN_DISCOVER[];
/* Clima natural por estación, en por mil por tick: [0] lluvia (nieve en invierno), [1] viento. */
extern const uint8_t GARDEN_WEATHER[4][2];

const GardenRule *garden_rule(uint8_t sp, uint8_t st);

/* ---------- API ---------- */

void garden_init(GameState *gs);
/* Un macro tick del jardín (World.tick). Determinista: solo usa gs->rng y gs->tick. */
void garden_tick(GameState *gs);

/* Cuántas entidades hay; st == ST_ANY cuenta todas las etapas. */
int garden_count(const GameState *gs);
int garden_count_sp(const GameState *gs, uint8_t sp, uint8_t st);
const Entity *garden_at(const GameState *gs, int cell);
/* Hace nacer algo en una casilla libre (cerca de 'near' si es >= 0). NULL si no cabe.
 * force ignora el tope de la especie (no el de 32). */
Entity *garden_place(GameState *gs, uint8_t sp, uint8_t st, int near, bool force);
/* Quita una entidad. */
void garden_remove(GameState *gs, Entity *e);
/* Estación, de día y hora del macro tick actual. */
Season garden_season(const GameState *gs);
bool garden_is_day(const GameState *gs);

typedef enum { ACT_OK, ACT_NO_ACTIONS, ACT_NO_ITEMS } ActResult;
/* La acción del Jardín (A, toque): límite diario, saca un objeto (el preparado si lo hay)
 * y aplica su efecto. 'widx' es la posición del Jardín en el ciclo de mundos. */
ActResult garden_act(GameState *gs, int widx, ItemKind *used, int *was_prepared, const char **effect);

/* Cuánto falta para que la regla cambie, en ticks (para la lupa). */
int garden_ticks_left(const GameState *gs, const Entity *e);

#endif
