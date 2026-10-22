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
