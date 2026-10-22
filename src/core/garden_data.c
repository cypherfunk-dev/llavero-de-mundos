/* Reglas del jardín como datos (spec §4.2). Para añadir una cadena nueva basta con
 * añadir filas aquí: el motor (garden.c) no cambia.
 *
 *   # especie     etapa  dura   siguiente
 *   manzana       fresca 2 d    podrida
 *   manzana       podrida 1 d   brote 40 % | hongo 30 % | nada 30 %
 *
 * Las duraciones se dan en horas (H) o ticks y se escalan con LLV_GROW_PCT. */
#include "garden.h"

#define SCALE(t) ((uint16_t)((t) * 100 / LLV_GROW_PCT))
#define H(n) SCALE((n) * LLV_TICKS_PER_HOUR)
#define TICKS(n) ((uint16_t)(n))

/* Resultados: se convierte en... */
#define TO(p, s, t) {.pct = (p), .sp = (s), .st = (t)}
#define TO_EV(p, s, t, e) {.pct = (p), .sp = (s), .st = (t), .ev = (e)}
#define GONE(p) {.pct = (p)}
/* ...y además aparece algo en otra casilla. */
#define TO_SPAWN(p, s, t, s2, t2, nr, e) \
    {.pct = (p), .sp = (s), .st = (t), .sp2 = (s2), .st2 = (t2), .near = (nr), .ev = (e)}

const GardenRule GARDEN_RULES[SP_COUNT][GARDEN_STAGES] = {
    /* semilla -> brote | flor | nada (no germinó) */
    [SP_SEMILLA] = {
        [0] = {"Semilla", "germina", H(14), RF_WET | RF_SLOW_WINTER,
               {TO_EV(55, SP_BROTE, 0, EV_SPROUT), TO(25, SP_FLOR, 0), GONE(20)}},
    },
    /* brote -> árbol joven -> árbol: ~4 días de semilla a árbol */
    [SP_BROTE] = {
        [0] = {"Brote", "crece", H(36), RF_WET | RF_SLOW_WINTER, {TO(100, SP_ARBOL_JOVEN, 0)}},
    },
    [SP_ARBOL_JOVEN] = {
        [0] = {"Árbol joven", "se hace árbol", H(48), RF_WET | RF_SLOW_WINTER,
               {TO_EV(100, SP_ARBOL, 0, EV_TREE_ADULT)}},
    },
    /* árbol -> con fruta -> (viento o por su peso) suelta una manzana */
    [SP_ARBOL] = {
        [0] = {"Árbol", "echa fruta", H(60), RF_WET | RF_SLOW_WINTER,
               {TO(97, SP_ARBOL, 1), TO(3, SP_ARBOL, 2)}},
        [1] = {"Árbol con fruta", "suelta fruta", H(96), RF_WET | RF_SLOW_WINTER,
               {TO_SPAWN(100, SP_ARBOL, 0, SP_MANZANA, 0, 1, EV_APPLE_FALL)}},
        /* de viejo se vuelve tocón: sale un hongo, un brote o se va (el jardín se renueva) */
        [2] = {"Árbol viejo", "se vuelve tocón", H(72), RF_WET,
               {TO_EV(55, SP_HONGO, 0, EV_MUSHROOM), TO_EV(30, SP_BROTE, 0, EV_SPROUT), GONE(15)}},
    },
    /* manzana fresca -2 d-> podrida -1 d-> brote 40 % | hongo 30 % | nada 30 % */
    [SP_MANZANA] = {
        [0] = {"Manzana", "se pudre", H(48), RF_WET, {TO_EV(100, SP_MANZANA, 1, EV_APPLE_ROT)}},
        [1] = {"Manzana podrida", "se deshace", H(24), RF_WET,
               {TO_EV(40, SP_BROTE, 0, EV_SPROUT), TO_EV(30, SP_HONGO, 0, EV_MUSHROOM), GONE(30)}},
    },
    [SP_HONGO] = {
        [0] = {"Hongo", "crece", H(24), RF_WET, {TO(100, SP_HONGO, 1)}},
        [1] = {"Hongo grande", "suelta esporas", H(72), RF_WET,
               {GONE(70), TO_SPAWN(30, SP_NONE, 0, SP_HONGO, 0, 1, 0)}},
    },
    [SP_FLOR] = {
        [0] = {"Capullo", "florece", H(24), RF_WET | RF_SLOW_WINTER,
               {TO(92, SP_FLOR, 1), TO_EV(8, SP_FLOR, 3, EV_BLOOM)}},
        [1] = {"Flor", "se marchita", H(72), RF_SLOW_WINTER, {TO(100, SP_FLOR, 2)}},
        [2] = {"Flor marchita", "deja semilla", H(24), 0,
               {TO_SPAWN(35, SP_NONE, 0, SP_SEMILLA, 0, 1, 0), GONE(65)}},
        [3] = {"Flor rara", "se marchita", H(96), RF_SLOW_WINTER, {TO(100, SP_FLOR, 2)}},
    },
    [SP_ABEJA] = {
        [0] = {"Abeja", "se va", TICKS(4), 0, {GONE(100)}},
    },
    [SP_PAJARO] = {
        [0] = {"Pájaro", "se va", TICKS(12), 0, {GONE(100)}},
        /* el alpiste lo atrae: se queda un rato y deja semillas */
        [1] = {"Pájaro goloso", "se va", TICKS(14), 0,
               {{.pct = 50, .sp2 = SP_SEMILLA, .near = 0, .ev = EV_BIRD_SEED, .item = ITEM_SEMILLA},
                {.pct = 50, .sp2 = SP_SEMILLA, .near = 0, .ev = EV_BIRD_SEED}}},
    },
    [SP_LOMBRIZ] = {
        [0] = {"Lombriz", "se entierra", H(30), 0, {GONE(100)}},
    },
    /* lluvia -> charco -> rana (rara) */
    [SP_CHARCO] = {
        [0] = {"Charco", "se seca", H(4), 0, {TO_EV(15, SP_RANA, 0, EV_FROG), GONE(85)}},
    },
    [SP_RANA] = {
        [0] = {"Rana", "salta lejos", H(30), 0, {GONE(100)}},
    },
    [SP_PIEDRA] = {
        [0] = {"Piedra con musgo", NULL, 0, 0, {{0}}},
    },
    [SP_HOJAS] = {
        [0] = {"Hojas secas", "se descomponen", H(48), 0, {GONE(100)}},
    },
    [SP_NIEVE] = {
        [0] = {"Nieve", "se derrite", H(36), RF_ONLY_WINTER, {GONE(100)}},
    },
};

/* Sucesos: el viento tira la fruta y se lleva las hojas. */
const GardenReact GARDEN_REACT[] = {
    {SP_ARBOL, 1, TRG_WIND, 100,
     TO_SPAWN(100, SP_ARBOL, 0, SP_MANZANA, 0, 1, EV_APPLE_FALL)},
    {SP_HOJAS, 0, TRG_WIND, 100, GONE(100)},
    {SP_NONE, 0, 0, 0, {0}},
};

/* Actores que buscan algo en el jardín. */
const GardenInteract GARDEN_INTERACT[] = {
    /* el pájaro se come la manzana y deja una semilla en otro sitio */
    {SP_PAJARO, 0, SP_MANZANA, 0, 14, SP_NONE, 0,
     {.pct = 100, .sp2 = SP_SEMILLA, .ev = EV_BIRD_SEED, .item = ITEM_SEMILLA}},
    {SP_PAJARO, 0, SP_MANZANA, 1, 18, SP_NONE, 0,
     {.pct = 100, .sp2 = SP_SEMILLA, .ev = EV_BIRD_SEED, .item = ITEM_SEMILLA}},
    /* la lombriz convierte la manzana podrida en abono: las plantas crecen antes */
    {SP_LOMBRIZ, 0, SP_MANZANA, 1, 15, SP_NONE, 0,
     {.pct = 100, .sp = SP_LOMBRIZ, .st = 0, .ev = EV_COMPOST, .eff = EFF_ABONO}},
    /* la abeja poliniza: nace una semilla lejos */
    {SP_ABEJA, 0, SP_FLOR, 1, 45, SP_FLOR, 1,
     {.pct = 100, .sp2 = SP_SEMILLA}},
    {SP_NONE, 0, 0, 0, 0, 0, 0, {0}},
};

/* Apariciones espontáneas: los animales y el clima. */
const GardenSpawn GARDEN_SPAWN[] = {
    {SP_ABEJA, 0, 14, SW_DAY | SW_DRY, SEAS(SEASON_SPRING) | SEAS(SEASON_SUMMER) | SEAS(SEASON_AUTUMN),
     SP_FLOR, 1, EV_BEE, 0},
    {SP_PAJARO, 0, 6, SW_DAY | SW_DRY, SEAS_ALL, SP_NONE, 0, EV_BIRD, 0},
    {SP_PAJARO, 0, 10, SW_DAY | SW_DRY, SEAS_ALL, SP_MANZANA, ST_ANY, EV_BIRD, 0},
    {SP_CHARCO, 0, 120, SW_RAIN, SEAS_ALL, SP_NONE, 0, 0, 0},
    {SP_LOMBRIZ, 0, 45, SW_RAIN, SEAS_ALL, SP_NONE, 0, EV_WORM, ITEM_LOMBRIZ},
    {SP_HONGO, 0, 14, SW_RAIN, SEAS(SEASON_SPRING) | SEAS(SEASON_AUTUMN), SP_NONE, 0, EV_MUSHROOM, 0},
    {SP_HOJAS, 0, 20, SW_DAY, SEAS(SEASON_AUTUMN), SP_ARBOL, ST_ANY, 0, 0},
    {SP_NIEVE, 0, 70, SW_SNOW, SEAS(SEASON_WINTER), SP_NONE, 0, 0, 0},
    {SP_SEMILLA, 0, 80, SW_WIND, SEAS_ALL, SP_NONE, 0, 0, 0},
    {SP_FLOR, 0, 3, SW_DAY | SW_DRY, SEAS(SEASON_SPRING) | SEAS(SEASON_SUMMER), SP_NONE, 0, 0, 0},
    {SP_NONE, 0, 0, 0, 0, 0, 0, 0, 0},
};

/* Topes por especie: así el jardín se limpia solo y nunca se llena (spec §10). */
const SpeciesInfo GARDEN_SPECIES[SP_COUNT] = {
    [SP_SEMILLA] = {3, 0},   [SP_BROTE] = {LLV_TREE_MAX, 1}, [SP_ARBOL_JOVEN] = {LLV_TREE_MAX, 1},
    [SP_ARBOL] = {LLV_TREE_MAX, 1}, [SP_MANZANA] = {4, 0},  [SP_HONGO] = {4, 0},
    [SP_FLOR] = {5, 0},      [SP_ABEJA] = {2, 0},          [SP_PAJARO] = {1, 0},
    [SP_LOMBRIZ] = {2, 0},   [SP_CHARCO] = {3, 0},         [SP_RANA] = {1, 0},
    [SP_PIEDRA] = {3, 0},    [SP_HOJAS] = {4, 0},          [SP_NIEVE] = {5, 0},
};

/* Lo que se apunta en la colección la primera vez que aparece. */
const GardenDiscover GARDEN_DISCOVER[] = {
    {SP_MANZANA, ST_ANY, COLL_MANZANA}, {SP_HONGO, ST_ANY, COLL_HONGO},
    {SP_FLOR, 1, COLL_FLOR}, {SP_FLOR, 3, COLL_FLOR_RARA},
    {SP_ABEJA, ST_ANY, COLL_ABEJA}, {SP_PAJARO, ST_ANY, COLL_PAJARO},
    {SP_LOMBRIZ, ST_ANY, COLL_LOMBRIZ}, {SP_RANA, ST_ANY, COLL_RANA},
    {SP_NONE, 0, 0},
};

const uint8_t GARDEN_WEATHER[4][2] = {
    [SEASON_SPRING] = {3, 6}, [SEASON_SUMMER] = {2, 5},
    [SEASON_AUTUMN] = {4, 7}, [SEASON_WINTER] = {3, 5},
};
