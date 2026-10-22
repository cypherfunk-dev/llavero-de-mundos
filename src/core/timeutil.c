#include "timeutil.h"

#include "config.h"

/* Luna nueva de referencia: 2000-01-06 18:14 UTC. */
#define MOON_REF_NEW 947182440LL

int64_t time_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* Algoritmos de calendario civil de Howard Hinnant (dominio público). */
int64_t time_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = time_floor_div(y, 400);
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void time_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = time_floor_div(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

int64_t time_from_civil(int y, int m, int d, int hh, int mm, int ss)
{
    return time_days_from_civil(y, m, d) * LLV_SECONDS_PER_DAY + hh * 3600 + mm * 60 + ss;
}

CivilTime time_to_civil(int64_t t)
{
    CivilTime c;
    int64_t days = time_floor_div(t, LLV_SECONDS_PER_DAY);
    int64_t secs = t - days * LLV_SECONDS_PER_DAY;
    time_civil_from_days(days, &c.year, &c.month, &c.day);
    c.hour = (int)(secs / 3600);
    c.minute = (int)(secs / 60 % 60);
    c.second = (int)(secs % 60);
    int64_t w = (days + 4) % 7; /* 1970-01-01 fue jueves */
    c.wday = (int)(w < 0 ? w + 7 : w);
    return c;
}

int64_t time_day_key(int64_t t)
{
    return time_floor_div(t - LLV_DAY_START_HOUR * 3600, LLV_SECONDS_PER_DAY);
}

Season time_season(int month, int day, int south)
{
    int md = month * 100 + day;
    Season s;
    if (md >= 320 && md < 621)
        s = SEASON_SPRING;
    else if (md >= 621 && md < 923)
        s = SEASON_SUMMER;
    else if (md >= 923 && md < 1221)
        s = SEASON_AUTUMN;
    else
        s = SEASON_WINTER;
    return south ? (Season)((s + 2) % 4) : s;
}

int32_t time_moon_age(int64_t t)
{
    int64_t a = (t - MOON_REF_NEW) % LLV_SYNODIC_SECONDS;
    if (a < 0)
        a += LLV_SYNODIC_SECONDS;
    return (int32_t)a;
}

int time_moon_phase(int64_t t)
{
    int64_t age = time_moon_age(t);
    return (int)(((age * 8) + LLV_SYNODIC_SECONDS / 2) / LLV_SYNODIC_SECONDS) % 8;
}

int64_t time_until_full_moon(int64_t t)
{
    int64_t half = LLV_SYNODIC_SECONDS / 2;
    int64_t age = time_moon_age(t);
    return age <= half ? half - age : LLV_SYNODIC_SECONDS - age + half;
}
