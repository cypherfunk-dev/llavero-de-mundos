/* Estado global del universo (spec §9.1, §10). Todo de tamaño fijo, sin malloc.
 * Solo cambia en macro ticks o por acciones del usuario: así lo simulado en vivo
 * y lo recalculado al despertar coinciden por construcción. */
#ifndef LLV_STATE_H
#define LLV_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "rng.h"

typedef enum {
    ITEM_NONE,
    ITEM_NUBE,
    ITEM_VIENTO,
    ITEM_SEMILLA,
    ITEM_ALPISTE,
    ITEM_LOMBRIZ,
    ITEM_DESEO,
    ITEM_COUNT
} ItemKind;

typedef enum {
    EV_NONE,
    EV_BAG_REFILL,     /* a = objeto */
    EV_BAG_FULL_LOST,  /* a = objeto que no cupo */
    EV_NEW_DAY,
    EV_WAKE,           /* a = horas dormido (tope 255) */
    /* Fase 2: jardín */
    EV_ITEM_USED,      /* a = objeto que gastaste */
    EV_APPLE_FALL,
    EV_APPLE_ROT,
    EV_SPROUT,         /* nace un brote */
    EV_TREE_ADULT,     /* un árbol joven se hace árbol */
    EV_MUSHROOM,
    EV_BLOOM,          /* florece una flor rara */
    EV_BIRD,
    EV_BIRD_SEED,      /* el pájaro deja una semilla */
    EV_BEE,
    EV_WORM,
    EV_FROG,
    EV_RAIN,           /* a = 1 si la trajo una Nube */
    EV_WIND,
    EV_SNOW,
    EV_RAINBOW,
    EV_STAR,
    EV_COMPOST,        /* una lombriz abona */
    EV_DISCOVER,       /* a = hueco de la colección */
    /* Fase 2: mascota */
    EV_PET_CHOSEN,     /* a = PetKind */
    EV_PET_STAGE,      /* a = etapa nueva */
    EV_PET_GEN,        /* a = generación nueva */
    EV_PET_CARE,
    EV_PET_WORD,
    EV_PET_SPORES,
    EV_PET_COMPOST,
    EV_KIND_COUNT
} EventKind;

/* Huecos de la colección del Cobertizo (bitset de 16). Los peces llegan en la fase 5. */
typedef enum {
    COLL_MANZANA, COLL_HONGO, COLL_FLOR, COLL_FLOR_RARA, COLL_ABEJA, COLL_PAJARO,
    COLL_LOMBRIZ, COLL_RANA, COLL_ESTRELLA, COLL_ARCOIRIS,
    COLL_PEZ0, COLL_PEZ1, COLL_PEZ2, COLL_PEZ3, COLL_PEZ4, COLL_PEZ5,
    COLL_COUNT
} CollSlot;

typedef enum { W_CLEAR, W_RAIN, W_WIND, W_SNOW } Weather;
typedef enum { SKY_NONE, SKY_STAR, SKY_RAINBOW } SkyEvent;

typedef enum {
    PET_NONE,          /* aún sin elegir */
    PET_HONGO,
    PET_SEMILLA,
    PET_PIEDRA,
    PET_COMPOSTERA,
    PET_COUNT
} PetKind;

/* Una cosa viva del jardín (spec §4.2): especie + etapa + cuenta atrás. */
typedef struct {
    uint8_t sp;        /* Species (garden.h); 0 = hueco libre */
    uint8_t st;        /* etapa dentro de la especie */
    uint8_t cell;      /* casilla 0..LLV_GARDEN_CELLS-1 */
    uint16_t left;     /* ticks hasta la siguiente etapa (0 = estable) */
} Entity;

/* La mascota (spec §4.7). Nunca muere: solo cambia de humor. */
typedef struct {
    uint8_t kind;      /* PetKind */
    uint8_t gen;       /* generación, empieza en 1 */
    uint16_t age;      /* ticks dentro del ciclo actual */
    uint8_t happy;     /* 0..255; solo decide el humor */
    uint8_t thought;   /* índice del último pensamiento (por mascota) */
    uint16_t word_left;  /* hongo: ticks hasta su próxima palabra */
    uint16_t task_left;  /* ticks hasta su próxima gracia (esporas, abono) */
    uint8_t stage_seen;  /* para anotar los cambios de etapa */
} Pet;

typedef struct {
    int64_t tick;
    uint8_t kind;
    uint8_t a;
} DiaryEntry;

typedef struct {
    uint64_t seed;
    Rng rng;
    int64_t tick;            /* último macro tick procesado (segundos locales / LLV_TICK_SECONDS) */
    int64_t day_key;         /* día del llavero en curso (empieza a las 05:00) */
    int64_t last_seen;       /* segundos locales de la última vez que se miró */

    uint8_t bag[LLV_BAG_SIZE];
    uint8_t bag_count;
    uint8_t prepared;        /* objeto preparado desde el Cobertizo (fase 2) */
    uint16_t refill_left;    /* ticks hasta el próximo objeto */

    uint8_t actions_used[LLV_WORLD_COUNT];
    uint8_t current_world;
    uint8_t reloj_style;

    uint32_t looks_today;
    uint32_t looks_total;

    /* Jardín (fase 2) */
    Entity ents[LLV_GARDEN_MAX];
    uint8_t weather;         /* Weather */
    uint8_t sky;             /* SkyEvent */
    uint16_t weather_left;   /* ticks que dura el clima actual */
    uint8_t sky_left;
    uint16_t collection;     /* bitset de CollSlot */

    Pet pet;

    DiaryEntry diary[LLV_DIARY_SIZE];
    uint8_t diary_head;      /* próxima posición a escribir */
    uint8_t diary_count;
    uint32_t diary_total;    /* eventos anotados en toda la vida (no se pisa) */
} GameState;

void state_init(GameState *gs, uint64_t seed, int64_t now);

/* Un macro tick del universo (recarga de bolsa, cambio de día). */
void state_tick(GameState *gs);

void state_log(GameState *gs, EventKind kind, uint8_t a);
/* Texto corto (cabe en ~14 letras) para el diario. false si ese evento no se muestra. */
int state_event_text(char *buf, size_t n, const DiaryEntry *d);
/* Añade un objeto a la bolsa si cabe. true si entró. */
int state_bag_add(GameState *gs, ItemKind item);
/* Apunta un descubrimiento en la colección (y lo anota). true si era nuevo. */
int state_collect(GameState *gs, CollSlot slot);
const char *state_coll_name(CollSlot slot);
/* i = 0 es el evento más reciente. NULL si no existe. */
const DiaryEntry *state_diary_get(const GameState *gs, int i);

/* Saca un objeto de la bolsa (el preparado si lo hay; si no, uno al azar). */
ItemKind state_bag_take(GameState *gs);

const char *state_item_name(ItemKind k);

/* Guardado portable: little-endian, con cabecera y CRC32. El estado completo debe
 * quedarse por debajo de 4 KB (spec §10); este tope se puede ajustar al compilar. */
#ifndef LLV_SAVE_MAX
#define LLV_SAVE_MAX 2048
#endif
size_t state_serialize(const GameState *gs, uint8_t *buf, size_t cap);
/* Devuelve 0 si el guardado es válido y lo carga en gs; distinto de 0 si no. */
int state_deserialize(GameState *gs, const uint8_t *buf, size_t len);

uint32_t state_crc32(const uint8_t *data, size_t len);

#endif
