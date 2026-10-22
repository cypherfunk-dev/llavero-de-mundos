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
    EV_KIND_COUNT
} EventKind;

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

    DiaryEntry diary[LLV_DIARY_SIZE];
    uint8_t diary_head;      /* próxima posición a escribir */
    uint8_t diary_count;
} GameState;

void state_init(GameState *gs, uint64_t seed, int64_t now);

/* Un macro tick del universo (recarga de bolsa, cambio de día). */
void state_tick(GameState *gs);

void state_log(GameState *gs, EventKind kind, uint8_t a);
/* i = 0 es el evento más reciente. NULL si no existe. */
const DiaryEntry *state_diary_get(const GameState *gs, int i);

/* Saca un objeto de la bolsa (el preparado si lo hay; si no, uno al azar). */
ItemKind state_bag_take(GameState *gs);

const char *state_item_name(ItemKind k);

/* Guardado portable: little-endian, con cabecera y CRC32. */
#define LLV_SAVE_MAX 1024
size_t state_serialize(const GameState *gs, uint8_t *buf, size_t cap);
/* Devuelve 0 si el guardado es válido y lo carga en gs; distinto de 0 si no. */
int state_deserialize(GameState *gs, const uint8_t *buf, size_t len);

uint32_t state_crc32(const uint8_t *data, size_t len);

#endif
