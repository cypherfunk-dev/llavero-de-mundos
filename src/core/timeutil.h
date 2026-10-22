/* Calendario y astronomía de bolsillo, sin libc de tiempo (también corre en el RP2350).
 * Todos los tiempos son "segundos locales": segundos desde 1970-01-01 00:00 en hora local. */
#ifndef LLV_TIMEUTIL_H
#define LLV_TIMEUTIL_H

#include <stdint.h>

typedef struct {
    int year;
    int month;   /* 1..12 */
    int day;     /* 1..31 */
    int hour;
    int minute;
    int second;
    int wday;    /* 0 = domingo .. 6 = sábado */
} CivilTime;

typedef enum { SEASON_SPRING, SEASON_SUMMER, SEASON_AUTUMN, SEASON_WINTER } Season;

#define LLV_SECONDS_PER_DAY 86400
#define LLV_SYNODIC_SECONDS 2551443   /* mes lunar: 29,530588 días */

int64_t time_days_from_civil(int y, int m, int d);
void time_civil_from_days(int64_t z, int *y, int *m, int *d);
int64_t time_from_civil(int y, int m, int d, int hh, int mm, int ss);
CivilTime time_to_civil(int64_t t);

/* Índice del día del llavero: cambia a las LLV_DAY_START_HOUR, no a medianoche. */
int64_t time_day_key(int64_t t);

Season time_season(int month, int day, int south);

/* Edad de la luna en segundos [0, LLV_SYNODIC_SECONDS). 0 = luna nueva. */
int32_t time_moon_age(int64_t t);
/* Fase 0..7: 0 nueva, 2 cuarto creciente, 4 llena, 6 cuarto menguante. */
int time_moon_phase(int64_t t);
/* Segundos que faltan hasta la próxima luna llena (0 si es ahora). */
int64_t time_until_full_moon(int64_t t);

/* División entera que redondea hacia -infinito. */
int64_t time_floor_div(int64_t a, int64_t b);

#endif
