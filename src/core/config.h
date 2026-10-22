/* Parámetros del llavero. Los marcados 🧪 se ajustan probando (spec §3). */
#ifndef LLV_CONFIG_H
#define LLV_CONFIG_H

/* Pantalla lógica (spec §12): 120x120 escalada x2 en una IPS de 240x240. */
#define LLV_W 120
#define LLV_H 120
#define LLV_HUD_H 9

/* Tiempo del mundo (spec §9.3). */
#define LLV_TICK_SECONDS 600                   /* macro tick: 10 min */
#define LLV_CATCHUP_MAX_TICKS (7 * 24 * 6)     /* tope de catch-up: 7 días */
#define LLV_DAY_START_HOUR 5                   /* el "día" empieza a las 05:00 🧪 */

/* Escasez (spec §3). */
#define LLV_BAG_SIZE 5                         /* 🧪 */
#define LLV_BAG_REFILL_TICKS (8 * 6)           /* +1 objeto cada 8 h 🧪 */

/* Jardín (spec §4.2, §10). Todo lo marcado 🧪 se ajusta mirando el jardín una semana. */
#define LLV_TICKS_PER_HOUR (3600 / LLV_TICK_SECONDS)
#define LLV_TICKS_PER_DAY (86400 / LLV_TICK_SECONDS)
#define LLV_GARDEN_COLS 7                      /* casillas de 16 px: 7x6 caben sobre el HUD */
#define LLV_GARDEN_ROWS 6
#define LLV_GARDEN_CELLS (LLV_GARDEN_COLS * LLV_GARDEN_ROWS)
#define LLV_GARDEN_MAX 32                      /* entidades: si está lleno no nace nada */
#define LLV_GARDEN_ACTIONS 2                   /* acciones del Jardín al día 🧪 */
#define LLV_GROW_PCT 100                       /* velocidad global: 100 = semilla a árbol en ~4 días 🧪 */
#define LLV_WET_MULT 4                         /* con lluvia, las plantas crecen x4 🧪 */
#define LLV_TREE_MAX 5                         /* brotes + jóvenes + árboles a la vez 🧪 */
#define LLV_RAIN_ITEM_TICKS (6 * LLV_TICKS_PER_HOUR)  /* lluvia de una Nube: 6 h 🧪 */
#define LLV_WIND_NATURAL_PCT 30                /* con viento natural, prob. por tick de que caiga la fruta 🧪 */
#define LLV_RAINBOW_PCT 18                     /* prob. de arcoíris al escampar de día 🧪 */
#define LLV_RAINBOW_TICKS (3 * LLV_TICKS_PER_HOUR)
#define LLV_STAR_PERMILLE 3                    /* estrella fugaz por tick de noche 🧪 */

/* Mascota (spec §4.7). */
#define LLV_PET_ACTIONS 1                      /* cuidados al día 🧪 */
#define LLV_PET_CYCLE_DAYS 100                 /* ciclo de vida 🧪 */
#define LLV_PET_CYCLE_TICKS (LLV_PET_CYCLE_DAYS * LLV_TICKS_PER_DAY)

/* Límites fijos de memoria (spec §10). */
#define LLV_DIARY_SIZE 64
#define LLV_WORLD_COUNT 6

/* Controles (spec §2). */
#define LLV_HOLD_MS 450                        /* pulsación larga */
#define LLV_COMBO_MS 150                       /* A+B "a la vez" */
#define LLV_SLEEP_AFTER_MS 20000               /* se duerme tras 20 s sin tocarlo 🧪 */
#define LLV_COLLAPSE_MS 700                    /* animación de colapso al despertar */

/* Cielo (horas locales en minutos desde medianoche). */
#define LLV_SUNRISE_MIN (6 * 60 + 30)
#define LLV_SUNSET_MIN (19 * 60)

/* 0 = hemisferio norte, 1 = sur (invierte las estaciones). */
#ifndef LLV_SOUTH_HEMISPHERE
#define LLV_SOUTH_HEMISPHERE 0
#endif

#endif
