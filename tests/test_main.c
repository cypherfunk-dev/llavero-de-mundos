/* Tests del motor, sin framework externo. Salida != 0 si algo falla. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/art.h"
#include "../src/core/cast.h"
#include "../src/core/director.h"
#include "../src/core/engine.h"
#include "../src/core/font.h"
#include "../src/core/garden.h"
#include "../src/core/habits.h"
#include "../src/core/pet.h"
#include "../src/core/phrases.h"
#include "../src/core/talk.h"
#include "../src/core/timeutil.h"

static int checks, failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            printf("    FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long va_ = (long long)(a), vb_ = (long long)(b);                    \
        checks++;                                                                \
        if (va_ != vb_) {                                                        \
            failures++;                                                          \
            printf("    FALLO %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, \
                   va_, vb_);                                                    \
        }                                                                        \
    } while (0)

#define CHECK_STR(a, b)                                                          \
    do {                                                                         \
        const char *sa_ = (a), *sb_ = (b);                                       \
        checks++;                                                                \
        if (strcmp(sa_, sb_) != 0) {                                             \
            failures++;                                                          \
            printf("    FALLO %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, sa_, sb_); \
        }                                                                        \
    } while (0)

typedef void (*TestFn)(void);
static int run_test(const char *name, TestFn fn)
{
    int before = failures;
    fn();
    printf("[%s] %s\n", failures == before ? " OK " : "FAIL", name);
    return failures == before;
}

/* Serializa un estado; devuelve la longitud. */
static size_t ser(const GameState *gs, uint8_t *buf)
{
    size_t n = state_serialize(gs, buf, LLV_SAVE_MAX);
    CHECK(n > 0);
    return n;
}

static int same_state(const GameState *a, const GameState *b)
{
    uint8_t x[LLV_SAVE_MAX], y[LLV_SAVE_MAX];
    size_t nx = state_serialize(a, x, sizeof(x)), ny = state_serialize(b, y, sizeof(y));
    return nx > 0 && nx == ny && memcmp(x, y, nx) == 0;
}

/* Referencia: 2024-03-10 08:00 local (alineado a un macro tick). */
static int64_t t0(void) { return time_from_civil(2024, 3, 10, 8, 0, 0); }

/* ---------- determinismo ---------- */

static void test_determinism_live_vs_catchup(void)
{
    const int64_t days[] = {1, 3, 6, 7};
    for (unsigned k = 0; k < sizeof(days) / sizeof(days[0]); k++) {
        int64_t end = t0() + days[k] * 86400;
        GameState live, batch;
        state_init(&live, 1234, t0());
        state_init(&batch, 1234, t0());
        /* En vivo: cada segundo, como un bucle de fotogramas. */
        for (int64_t t = t0(); t <= end; t++)
            engine_advance(&live, t);
        int64_t ran = engine_advance(&batch, end);
        CHECK_EQ(ran, days[k] * 144);
        CHECK(same_state(&live, &batch));
    }
}

static void test_determinism_irregular_steps(void)
{
    Rng r;
    rng_seed(&r, 99, 1);
    int64_t end = t0() + 6 * 86400 + 12345;
    GameState a, b;
    state_init(&a, 77, t0());
    state_init(&b, 77, t0());
    int64_t t = t0();
    while (t < end) {
        t += 1 + (int64_t)rng_below(&r, 5000); /* pasos de hasta 83 min */
        if (t > end)
            t = end;
        engine_advance(&a, t);
    }
    engine_advance(&b, end);
    CHECK(same_state(&a, &b));
}

static void test_determinism_sleep_wake(void)
{
    /* A mira cada 100 ms mientras está despierto; B solo al despertar y al dormir.
     * Ambos duermen y despiertan a las mismas horas: el estado debe ser idéntico. */
    Engine a, b;
    uint32_t ms_a = 1000, ms_b = 1000;
    engine_init(&a, 4242, t0(), ms_a);
    engine_init(&b, 4242, t0(), ms_b);
    engine_sleep(&a, t0());
    engine_sleep(&b, t0());
    RawInput none = {0, false}, shake = {0, true};
    Rng r;
    rng_seed(&r, 5, 5);
    int64_t now = t0();
    for (int session = 0; session < 40; session++) {
        now += 600 + (int64_t)rng_below(&r, 4 * 3600 * 3); /* ausencia */
        int64_t look = 2 + (int64_t)rng_below(&r, 8);       /* mirada de 2-9 s */
        /* B: despertar, un fotograma al final, dormir. */
        ms_b += 100000;
        engine_frame(&b, shake, now, ms_b);
        engine_frame(&b, none, now + look, ms_b + (uint32_t)look * 1000);
        engine_sleep(&b, now + look);
        /* A: despertar y fotogramas cada 100 ms. */
        ms_a += 100000;
        engine_frame(&a, shake, now, ms_a);
        for (int64_t ms = 100; ms <= look * 1000; ms += 100)
            engine_frame(&a, none, now + ms / 1000, ms_a + (uint32_t)ms);
        engine_sleep(&a, now + look);
        now += look;
        CHECK(a.asleep && b.asleep);
    }
    CHECK(same_state(&a.gs, &b.gs));
    CHECK_EQ(a.gs.looks_total, 41);
}

static void test_catchup_cap(void)
{
    GameState gs;
    state_init(&gs, 9, t0());
    int64_t end = t0() + 30 * 86400;
    int64_t ran = engine_advance(&gs, end);
    CHECK_EQ(ran, LLV_CATCHUP_MAX_TICKS);
    CHECK_EQ(gs.tick, time_floor_div(end, LLV_TICK_SECONDS));

    /* Pasarse del tope da lo mismo que quedarse exactamente en él partiendo del mismo punto:
     * el resultado depende solo de los 7 días finales, no de cuánto duró la ausencia. */
    GameState a, b;
    state_init(&a, 9, t0());
    state_init(&b, 9, t0());
    engine_advance(&a, t0() + 8 * 86400);
    engine_advance(&b, t0() + 20 * 86400);
    CHECK_EQ(a.tick - b.tick, -12 * 144);

    /* Justo en el tope no se recorta nada. */
    GameState c;
    state_init(&c, 9, t0());
    CHECK_EQ(engine_advance(&c, t0() + 7 * 86400), LLV_CATCHUP_MAX_TICKS);
    CHECK_EQ(engine_advance(&c, t0() + 7 * 86400 + 599), 0);
    CHECK_EQ(engine_advance(&c, t0() + 7 * 86400 + 600), 1);
    /* El tiempo hacia atrás no hace nada. */
    CHECK_EQ(engine_advance(&c, t0()), 0);
}

static void test_wake_logs_hours(void)
{
    Engine e;
    engine_init(&e, 1, t0(), 1000);
    engine_sleep(&e, t0());
    RawInput shake = {0, true};
    engine_frame(&e, shake, t0() + 5 * 3600 + 100, 5000);
    CHECK(!e.asleep);
    const DiaryEntry *d = state_diary_get(&e.gs, 0);
    CHECK(d && d->kind == EV_WAKE && d->a == 5);
}

/* ---------- guardado ---------- */

static void fix_crc(uint8_t *buf, size_t len)
{
    uint32_t c = state_crc32(buf, len - 4);
    for (int i = 0; i < 4; i++)
        buf[len - 4 + i] = (uint8_t)(c >> (8 * i));
}

static void test_crc32_vector(void)
{
    CHECK_EQ(state_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);
}

static void test_save_roundtrip(void)
{
    GameState gs, back;
    state_init(&gs, 0xDEADBEEFCAFEull, t0());
    engine_advance(&gs, t0() + 3 * 86400);
    state_bag_take(&gs);
    gs.current_world = 3;
    gs.reloj_style = 4;
    gs.looks_today = 7;
    gs.looks_total = 1000;
    gs.actions_used[1] = 2;
    gs.last_seen = t0() + 123456789;
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = ser(&gs, buf);
    CHECK(n > 0 && n <= LLV_SAVE_MAX);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK(same_state(&gs, &back));
    CHECK(back.seed == gs.seed && back.tick == gs.tick && back.rng.state == gs.rng.state);
    CHECK(back.bag_count == gs.bag_count && back.diary_count == gs.diary_count);
    CHECK(back.actions_used[1] == 2 && back.reloj_style == 4 && back.last_seen == gs.last_seen);
    /* Un estado cargado sigue la misma trayectoria que el original. */
    engine_advance(&gs, t0() + 5 * 86400);
    engine_advance(&back, t0() + 5 * 86400);
    CHECK(same_state(&gs, &back));
    /* Buffer demasiado pequeño: falla limpio. */
    CHECK_EQ(state_serialize(&gs, buf, 20), 0);
}

static void test_save_rejects_bad(void)
{
    GameState gs, out;
    state_init(&gs, 5, t0());
    uint8_t buf[LLV_SAVE_MAX], bad[LLV_SAVE_MAX];
    size_t n = ser(&gs, buf);

    CHECK_EQ(state_deserialize(&out, buf, n), 0);

    /* CRC corrupto: cualquier bit cambiado en cualquier posición. */
    for (size_t i = 0; i < n; i++) {
        memcpy(bad, buf, n);
        bad[i] ^= 0x10;
        CHECK(state_deserialize(&out, bad, n) != 0);
    }
    /* Versión errónea (con CRC recalculado). */
    memcpy(bad, buf, n);
    bad[4] = 99;
    fix_crc(bad, n);
    CHECK(state_deserialize(&out, bad, n) != 0);
    /* Magia errónea. */
    memcpy(bad, buf, n);
    bad[0] = 'X';
    fix_crc(bad, n);
    CHECK(state_deserialize(&out, bad, n) != 0);
    /* Longitud del payload errónea (con CRC recalculado). */
    memcpy(bad, buf, n);
    bad[6] = (uint8_t)(bad[6] + 1);
    fix_crc(bad, n);
    CHECK(state_deserialize(&out, bad, n) != 0);
    /* Truncado y alargado. */
    CHECK(state_deserialize(&out, buf, n - 1) != 0);
    CHECK(state_deserialize(&out, buf, 0) != 0);
    CHECK(state_deserialize(&out, buf, 11) != 0);
    memcpy(bad, buf, n);
    memset(bad + n, 0, 8);
    CHECK(state_deserialize(&out, bad, n + 8) != 0);
    /* Valores fuera de rango con CRC válido (bag_count = 200). */
    memcpy(bad, buf, n);
    GameState evil = gs;
    evil.bag_count = 200;
    size_t m = ser(&evil, bad);
    CHECK(state_deserialize(&out, bad, m) != 0);
    /* Un rechazo no toca el destino. */
    GameState keep = gs;
    keep.looks_total = 31337;
    out = keep;
    CHECK(state_deserialize(&out, bad, m) != 0);
    CHECK(out.looks_total == 31337);
}

/* ---------- timeutil ---------- */

static void test_time_civil(void)
{
    CHECK_EQ(time_days_from_civil(1970, 1, 1), 0);
    CHECK_EQ(time_days_from_civil(2000, 3, 1), 11017);
    CHECK_EQ(time_days_from_civil(1969, 12, 31), -1);
    int y, m, d;
    time_civil_from_days(19782, &y, &m, &d); /* 2024-02-29 */
    CHECK(y == 2024 && m == 2 && d == 29);
    time_civil_from_days(-1, &y, &m, &d);
    CHECK(y == 1969 && m == 12 && d == 31);

    CivilTime c = time_to_civil(1700000000); /* 2023-11-14 22:13:20, martes */
    CHECK(c.year == 2023 && c.month == 11 && c.day == 14);
    CHECK(c.hour == 22 && c.minute == 13 && c.second == 20 && c.wday == 2);
    c = time_to_civil(0);
    CHECK(c.wday == 4 && c.hour == 0); /* jueves */
    c = time_to_civil(-1);
    CHECK(c.year == 1969 && c.hour == 23 && c.minute == 59 && c.second == 59 && c.wday == 3);
    CHECK_EQ(time_from_civil(2023, 11, 14, 22, 13, 20), 1700000000);
    /* Ida y vuelta durante varios años, incluidos bisiestos. */
    for (int64_t day = 10000; day < 25000; day += 7) {
        time_civil_from_days(day, &y, &m, &d);
        CHECK_EQ(time_days_from_civil(y, m, d), day);
    }
    CHECK_EQ(time_floor_div(-1, 600), -1);
    CHECK_EQ(time_floor_div(-600, 600), -1);
    CHECK_EQ(time_floor_div(599, 600), 0);
}

static void test_time_day_key(void)
{
    int64_t a = time_from_civil(2024, 3, 10, 4, 59, 59);
    int64_t b = time_from_civil(2024, 3, 10, 5, 0, 0);
    CHECK_EQ(time_day_key(b), time_day_key(a) + 1);
    /* De las 05:00 a las 04:59 del día siguiente es el mismo día del llavero. */
    CHECK_EQ(time_day_key(time_from_civil(2024, 3, 10, 23, 59, 59)), time_day_key(b));
    CHECK_EQ(time_day_key(time_from_civil(2024, 3, 11, 4, 59, 59)), time_day_key(b));
    CHECK_EQ(time_day_key(time_from_civil(2024, 3, 11, 5, 0, 0)), time_day_key(b) + 1);

    /* En el motor: las acciones se reponen al cruzar las 05:00, no a medianoche. */
    GameState gs;
    state_init(&gs, 1, time_from_civil(2024, 3, 10, 23, 0, 0));
    gs.actions_used[1] = 2;
    engine_advance(&gs, time_from_civil(2024, 3, 11, 1, 0, 0));
    CHECK_EQ(gs.actions_used[1], 2);
    engine_advance(&gs, time_from_civil(2024, 3, 11, 4, 50, 0));
    CHECK_EQ(gs.actions_used[1], 2);
    engine_advance(&gs, time_from_civil(2024, 3, 11, 5, 0, 0));
    CHECK_EQ(gs.actions_used[1], 0);
}

static void test_time_seasons(void)
{
    CHECK_EQ(time_season(3, 19, 0), SEASON_WINTER);
    CHECK_EQ(time_season(3, 20, 0), SEASON_SPRING);
    CHECK_EQ(time_season(6, 20, 0), SEASON_SPRING);
    CHECK_EQ(time_season(6, 21, 0), SEASON_SUMMER);
    CHECK_EQ(time_season(9, 22, 0), SEASON_SUMMER);
    CHECK_EQ(time_season(9, 23, 0), SEASON_AUTUMN);
    CHECK_EQ(time_season(12, 20, 0), SEASON_AUTUMN);
    CHECK_EQ(time_season(12, 21, 0), SEASON_WINTER);
    CHECK_EQ(time_season(1, 15, 0), SEASON_WINTER);
    CHECK_EQ(time_season(1, 15, 1), SEASON_SUMMER);
    CHECK_EQ(time_season(4, 1, 1), SEASON_AUTUMN);
}

static void test_time_moon(void)
{
    const int64_t half = LLV_SYNODIC_SECONDS / 2;
    /* Luna llena real: 2024-01-25 17:54 UTC. El modelo es de mes medio: tolera ~12 h. */
    int64_t full = time_from_civil(2024, 1, 25, 17, 54, 0);
    int64_t err = time_moon_age(full) - half;
    if (err < 0)
        err = -err;
    CHECK(err < 12 * 3600);
    CHECK_EQ(time_moon_phase(full), 4);
    CHECK_EQ(time_moon_phase(full + 3 * 86400), 5);
    CHECK_EQ(time_moon_phase(full - 3 * 86400), 3);
    /* Luna nueva real: 2024-01-11 11:57 UTC. */
    CHECK_EQ(time_moon_phase(time_from_civil(2024, 1, 11, 12, 0, 0)), 0);
    /* Cuarto creciente (17 ene) y menguante (3 feb). */
    CHECK_EQ(time_moon_phase(time_from_civil(2024, 1, 18, 3, 0, 0)), 2);
    CHECK_EQ(time_moon_phase(time_from_civil(2024, 2, 2, 23, 0, 0)), 6);
    /* Periodicidad y rango, también antes de 1970. */
    for (int64_t t = -5000000000LL; t < 5000000000LL; t += 123457789) {
        int32_t age = time_moon_age(t);
        CHECK(age >= 0 && age < LLV_SYNODIC_SECONDS);
        int ph = time_moon_phase(t);
        CHECK(ph >= 0 && ph < 8);
        CHECK_EQ(time_moon_age(t + LLV_SYNODIC_SECONDS), age);
        int64_t u = time_until_full_moon(t);
        CHECK(u >= 0 && u < LLV_SYNODIC_SECONDS);
        CHECK_EQ(time_moon_age(t + u), half);
    }
}

/* ---------- input ---------- */

static void test_input(void)
{
    InputState in;
    input_init(&in);
    /* Toque de A. */
    CHECK_EQ(input_update(&in, BTN_A, 1000), GEST_NONE);
    CHECK_EQ(input_update(&in, BTN_A, 1100), GEST_NONE);
    CHECK_EQ(input_update(&in, 0, 1150), GEST_A_TAP);
    CHECK_EQ(input_update(&in, 0, 1200), GEST_NONE);
    /* Toque de B. */
    input_update(&in, BTN_B, 2000);
    CHECK_EQ(input_update(&in, 0, 2080), GEST_B_TAP);
    /* Mantener A: dispara al llegar al umbral y al soltar no hay toque. */
    input_update(&in, BTN_A, 3000);
    CHECK_EQ(input_update(&in, BTN_A, 3000 + LLV_HOLD_MS - 1), GEST_NONE);
    CHECK_EQ(input_update(&in, BTN_A, 3000 + LLV_HOLD_MS), GEST_A_HOLD);
    CHECK_EQ(input_update(&in, BTN_A, 3000 + LLV_HOLD_MS + 500), GEST_NONE);
    CHECK_EQ(input_update(&in, 0, 4500), GEST_NONE);
    /* Mantener B. */
    input_update(&in, BTN_B, 5000);
    CHECK_EQ(input_update(&in, BTN_B, 5000 + LLV_HOLD_MS), GEST_B_HOLD);
    CHECK_EQ(input_update(&in, 0, 6000), GEST_NONE);
    /* A+B: B llega dentro de la ventana; sin toques sueltos al soltar. */
    input_update(&in, BTN_A, 7000);
    CHECK_EQ(input_update(&in, BTN_A | BTN_B, 7000 + LLV_COMBO_MS - 10), GEST_AB);
    CHECK_EQ(input_update(&in, BTN_A | BTN_B, 7000 + LLV_HOLD_MS + 100), GEST_NONE);
    CHECK_EQ(input_update(&in, BTN_B, 8000), GEST_NONE);
    CHECK_EQ(input_update(&in, 0, 8010), GEST_NONE);
    /* A+B pulsados en el mismo fotograma. */
    CHECK_EQ(input_update(&in, BTN_A | BTN_B, 9000), GEST_AB);
    input_update(&in, 0, 9050);
    /* Demasiado separados: no es A+B. La B es un toque y la A, una pulsación larga. */
    input_update(&in, BTN_A, 10000);
    CHECK_EQ(input_update(&in, BTN_A | BTN_B, 10000 + LLV_COMBO_MS + 100), GEST_NONE);
    CHECK_EQ(input_update(&in, BTN_A, 10000 + LLV_COMBO_MS + 150), GEST_B_TAP);
    CHECK_EQ(input_update(&in, BTN_A, 10000 + LLV_HOLD_MS), GEST_A_HOLD);
    input_update(&in, 0, 11000);
    /* Swallow: el botón que despertó no actúa al soltarse, pero el siguiente toque sí. */
    input_update(&in, BTN_A, 12000);
    input_swallow(&in);
    CHECK_EQ(input_update(&in, 0, 12050), GEST_NONE);
    input_update(&in, BTN_A, 12100);
    CHECK_EQ(input_update(&in, 0, 12150), GEST_A_TAP);
    /* Swallow también evita la pulsación larga. */
    input_update(&in, BTN_B, 13000);
    input_swallow(&in);
    CHECK_EQ(input_update(&in, BTN_B, 13000 + LLV_HOLD_MS + 100), GEST_NONE);
    /* Contador de ms que da la vuelta. */
    input_update(&in, 0, 13500);
    input_update(&in, BTN_A, 0xFFFFFFF0u);
    CHECK_EQ(input_update(&in, 0, 20), GEST_A_TAP);
    input_update(&in, BTN_A, 0xFFFFFFF0u);
    CHECK_EQ(input_update(&in, BTN_A, 0xFFFFFFF0u + LLV_HOLD_MS), GEST_A_HOLD);
}

static void test_ms_pending(void)
{
    CHECK(ms_pending(1000, 2000));
    CHECK(!ms_pending(2000, 2000));
    CHECK(!ms_pending(3000, 2000));
    /* El contador de ms da la vuelta cada ~49 días. */
    CHECK(ms_pending(0xFFFFFF00u, 0x100u));
    CHECK(!ms_pending(0x200u, 0x100u));
    /* Un temporizador a cero es "nada pendiente" a cualquier hora: con el contador en la
     * segunda mitad del rango (> 24,8 días encendido) antes parecía siempre en marcha. */
    CHECK(!ms_pending(5000, 0));
    CHECK(!ms_pending(0x90000000u, 0));
    CHECK(!ms_pending(0xFFFFFFF0u, 0));
    /* Y en el motor: tras 30 días de ms no aparecen ni el diario ni mensajes fantasma. */
    static Engine e;
    uint32_t ms = 0x9C000000u; /* ~30 días */
    int64_t now = time_from_civil(2026, 10, 22, 12, 0, 0);
    engine_init(&e, 1, now, ms);
    engine_sleep(&e, now);
    RawInput shake = {0, true}, none = {0, false};
    engine_frame(&e, shake, now + 100, ms += 100);
    for (int i = 0; i < 60; i++)
        engine_frame(&e, none, now + 100, ms += 33);
    e.gs.current_world = 1;
    engine_frame(&e, none, now + 100, ms += 33);
    CHECK(e.diary_until_ms == 0 && e.toast_until_ms == 0);
    CHECK(!ms_pending(ms, e.diary_until_ms) && !ms_pending(ms, e.toast_until_ms));
}

/* ---------- rng ---------- */

static void test_rng(void)
{
    /* Vectores de referencia de PCG32 (pcg32-demo: seed 42, seq 54). */
    Rng r;
    rng_seed(&r, 42, 54);
    CHECK_EQ(rng_next(&r), 0xa15c02b7u);
    CHECK_EQ(rng_next(&r), 0x7b47f409u);
    CHECK_EQ(rng_next(&r), 0xba1d3330u);
    CHECK_EQ(rng_next(&r), 0x83d2f293u);
    CHECK_EQ(rng_next(&r), 0xbfa4784bu);
    CHECK_EQ(rng_next(&r), 0xcbed606eu);

    /* Reproducible, y las secuencias distintas difieren. */
    Rng a, b, c;
    rng_seed(&a, 7, 1);
    rng_seed(&b, 7, 1);
    rng_seed(&c, 8, 1);
    int diff = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t x = rng_next(&a);
        CHECK_EQ(x, rng_next(&b));
        diff += x != rng_next(&c);
    }
    CHECK(diff > 90);

    /* rng_below: en rango, cubre todos los valores y es razonablemente uniforme. */
    Rng u;
    rng_seed(&u, 1, 2);
    int hist[6] = {0};
    for (int i = 0; i < 6000; i++) {
        uint32_t v = rng_below(&u, 6);
        CHECK(v < 6);
        if (v < 6)
            hist[v]++;
    }
    for (int k = 0; k < 6; k++)
        CHECK(hist[k] > 850 && hist[k] < 1150);
    CHECK_EQ(rng_below(&u, 0), 0);
    CHECK_EQ(rng_below(&u, 1), 0);
    for (int i = 0; i < 1000; i++)
        CHECK(rng_below(&u, 0x80000001u) < 0x80000001u);
}

/* ---------- bolsa ---------- */

static int count_events(const GameState *gs, EventKind k)
{
    int n = 0;
    for (int i = 0; i < gs->diary_count; i++)
        n += state_diary_get(gs, i)->kind == k;
    return n;
}

static void test_bag(void)
{
    GameState gs;
    state_init(&gs, 31, t0());
    CHECK_EQ(gs.bag_count, 2);
    const int64_t H = 3600;
    /* No hay recarga antes de 8 h; a las 8 h, exactamente una. */
    engine_advance(&gs, t0() + 8 * H - LLV_TICK_SECONDS);
    CHECK_EQ(gs.bag_count, 2);
    engine_advance(&gs, t0() + 8 * H);
    CHECK_EQ(gs.bag_count, 3);
    CHECK_EQ(count_events(&gs, EV_BAG_REFILL), 1);
    const DiaryEntry *d = state_diary_get(&gs, 0);
    CHECK(d && d->kind == EV_BAG_REFILL && d->a > ITEM_NONE && d->a < ITEM_COUNT);
    CHECK_EQ(gs.bag[2], d->a);
    /* Cada 8 h otra más, hasta el tope de 5. */
    engine_advance(&gs, t0() + 16 * H);
    CHECK_EQ(gs.bag_count, 4);
    engine_advance(&gs, t0() + 24 * H);
    CHECK_EQ(gs.bag_count, 5);
    CHECK_EQ(count_events(&gs, EV_BAG_FULL_LOST), 0);
    /* Con la bolsa llena, la recarga se pierde y se anota. */
    engine_advance(&gs, t0() + 32 * H);
    CHECK_EQ(gs.bag_count, LLV_BAG_SIZE);
    CHECK_EQ(count_events(&gs, EV_BAG_FULL_LOST), 1);
    engine_advance(&gs, t0() + 56 * H);
    CHECK_EQ(gs.bag_count, LLV_BAG_SIZE);
    CHECK_EQ(count_events(&gs, EV_BAG_FULL_LOST), 4);
    for (int i = 0; i < gs.bag_count; i++)
        CHECK(gs.bag[i] > ITEM_NONE && gs.bag[i] < ITEM_COUNT);
    /* Sacar uno y que la siguiente recarga lo reponga. */
    ItemKind got = state_bag_take(&gs);
    CHECK(got > ITEM_NONE && got < ITEM_COUNT);
    CHECK_EQ(gs.bag_count, 4);
    CHECK_EQ(gs.bag[4], ITEM_NONE);
    engine_advance(&gs, t0() + 64 * H);
    CHECK_EQ(gs.bag_count, 5);
}

static void test_bag_prepared(void)
{
    GameState gs;
    state_init(&gs, 3, t0());
    gs.bag[0] = ITEM_NUBE;
    gs.bag[1] = ITEM_LOMBRIZ;
    gs.bag[2] = ITEM_SEMILLA;
    gs.bag_count = 3;
    gs.prepared = ITEM_LOMBRIZ;
    CHECK_EQ(state_bag_take(&gs), ITEM_LOMBRIZ);
    CHECK_EQ(gs.prepared, ITEM_NONE);
    CHECK_EQ(gs.bag_count, 2);
    CHECK_EQ(gs.bag[0], ITEM_NUBE);
    CHECK_EQ(gs.bag[1], ITEM_SEMILLA);
    CHECK_EQ(gs.bag[2], ITEM_NONE);
    /* Preparado que ya no está en la bolsa: se saca otro y se limpia. */
    gs.prepared = ITEM_DESEO;
    ItemKind k = state_bag_take(&gs);
    CHECK(k == ITEM_NUBE || k == ITEM_SEMILLA);
    CHECK_EQ(gs.prepared, ITEM_NONE);
    CHECK_EQ(gs.bag_count, 1);
    /* Vaciar y pedir de más. */
    CHECK(state_bag_take(&gs) != ITEM_NONE);
    CHECK_EQ(gs.bag_count, 0);
    CHECK_EQ(state_bag_take(&gs), ITEM_NONE);
    CHECK_EQ(gs.bag_count, 0);
}

static void test_diary_circular(void)
{
    GameState gs;
    state_init(&gs, 1, t0());
    for (int i = 0; i < LLV_DIARY_SIZE + 10; i++) {
        gs.tick = i;
        state_log(&gs, EV_WAKE, (uint8_t)i);
    }
    CHECK_EQ(gs.diary_count, LLV_DIARY_SIZE);
    CHECK_EQ(state_diary_get(&gs, 0)->tick, LLV_DIARY_SIZE + 9);
    CHECK_EQ(state_diary_get(&gs, LLV_DIARY_SIZE - 1)->tick, 10);
    CHECK(state_diary_get(&gs, LLV_DIARY_SIZE) == NULL);
    CHECK(state_diary_get(&gs, -1) == NULL);
}

/* ---------- Reloj ---------- */

static void test_reloj_words(void)
{
    char b[96];
    reloj_time_in_words(b, sizeof(b), 10, 24);
    CHECK_STR(b, "Son las diez y veinticuatro de la mañana.");
    reloj_time_in_words(b, sizeof(b), 13, 0);
    CHECK_STR(b, "Es la una en punto del mediodía.");
    reloj_time_in_words(b, sizeof(b), 0, 0);
    CHECK_STR(b, "Son las doce en punto de la noche.");
    reloj_time_in_words(b, sizeof(b), 17, 45);
    CHECK_STR(b, "Son las seis menos cuarto de la tarde.");
    reloj_time_in_words(b, sizeof(b), 7, 30);
    CHECK_STR(b, "Son las siete y media de la mañana.");
    reloj_time_in_words(b, sizeof(b), 23, 59);
    CHECK_STR(b, "Son las doce menos un minuto de la noche.");
    reloj_time_in_words(b, sizeof(b), 3, 15);
    CHECK_STR(b, "Son las tres y cuarto de la madrugada.");
    reloj_time_in_words(b, sizeof(b), 12, 1);
    CHECK_STR(b, "Son las doce y un minuto del mediodía.");
    /* Ninguna hora produce basura ni desborda un buffer pequeño. */
    for (int h = 0; h < 24; h++)
        for (int m = 0; m < 60; m++) {
            reloj_time_in_words(b, sizeof(b), h, m);
            CHECK(strlen(b) > 10 && strlen(b) < 60 && b[strlen(b) - 1] == '.');
            CHECK(strstr(b, "(null)") == NULL);
        }
}

/* ---------- el motor en conjunto ---------- */

static void test_engine_gestures(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t0();
    engine_init(&e, 1, now, ms);
    RawInput none = {0, false};
    /* Pasada la animación de colapso, B (toque) cambia de mundo y A (toque) cambia el estilo. */
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.current_world, 0);
    RawInput a = {BTN_A, false}, b = {BTN_B, false};
    engine_frame(&e, a, now, ms += 33);
    FrameOut o = engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.reloj_style, 1);
    CHECK(o.screen_on);
    engine_frame(&e, b, now, ms += 33);
    engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.current_world, 1);
    /* Se duerme solo y pide guardar; despierta con A sin actuar. */
    int saves = 0;
    for (int i = 0; i < 20000 / 33 + 40; i++)
        saves += engine_frame(&e, none, now, ms += 33).save_now;
    CHECK_EQ(saves, 1);
    CHECK(e.asleep);
    o = engine_frame(&e, none, now, ms += 33);
    CHECK(!o.screen_on);
    engine_frame(&e, a, now, ms += 33);
    CHECK(!e.asleep);
    engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.current_world, 1);
}


/* ======================= fase 2: jardín, bolsa, Cobertizo y mascota ======================= */

/* Helpers de la fase 2. */
static void fill_bag(GameState *gs, Rng *r)
{
    while (gs->bag_count < LLV_BAG_SIZE)
        state_bag_add(gs, (ItemKind)(1 + rng_below(r, ITEM_COUNT - 1)));
}

static Entity *ent_put(GameState *gs, uint8_t sp, uint8_t st, int cell, uint16_t left)
{
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        if (gs->ents[i].sp == SP_NONE) {
            gs->ents[i] = (Entity){sp, st, (uint8_t)cell, left};
            return &gs->ents[i];
        }
    return NULL;
}

static void ent_clear(GameState *gs)
{
    memset(gs->ents, 0, sizeof(gs->ents));
}

/* Invariantes del jardín: tope de 32, casillas únicas y en rango, especies y etapas válidas. */
static int garden_ok(const GameState *gs)
{
    uint64_t used = 0;
    int n = 0;
    for (int i = 0; i < LLV_GARDEN_MAX; i++) {
        const Entity *e = &gs->ents[i];
        if (e->sp == SP_NONE)
            continue;
        n++;
        if (e->sp >= SP_COUNT || e->st >= GARDEN_STAGES || !garden_rule(e->sp, e->st))
            return 0;
        if (e->cell >= LLV_GARDEN_CELLS || (used >> e->cell & 1))
            return 0;
        used |= 1ull << e->cell;
    }
    return n <= LLV_GARDEN_MAX && n == garden_count(gs);
}

static int count_ev(const GameState *gs, EventKind k)
{
    return count_events(gs, k);
}

/* Cuenta los eventos del diario sin perder ninguno aunque el diario circular los pise:
 * hay que llamar a ev_update al menos cada ~60 eventos (una vez al día sobra). */
typedef struct {
    uint32_t seen;
    uint32_t n[EV_KIND_COUNT];
} EvCount;

static void ev_update(EvCount *c, const GameState *gs)
{
    uint32_t fresh = gs->diary_total - c->seen;
    uint32_t k = fresh > gs->diary_count ? gs->diary_count : fresh;
    CHECK(fresh <= LLV_DIARY_SIZE); /* si no, el contador habría perdido eventos */
    for (uint32_t i = k; i-- > 0;)
        c->n[state_diary_get(gs, (int)i)->kind]++;
    c->seen = gs->diary_total;
}

/* Referencia en otoño (el 22 de octubre de 2026, 05:00): hay hojas, lluvia y fruta. */
static int64_t t_autumn(void) { return time_from_civil(2026, 10, 22, 5, 0, 0); }

static void test_tables(void)
{
    for (int sp = 1; sp < SP_COUNT; sp++) {
        CHECK(GARDEN_SPECIES[sp].cap > 0);
        int stages = 0;
        for (int st = 0; st < GARDEN_STAGES; st++) {
            const GardenRule *r = garden_rule((uint8_t)sp, (uint8_t)st);
            if (!r)
                continue;
            stages++;
            int sum = 0;
            for (int k = 0; k < GARDEN_OUTCOMES && r->next[k].pct; k++) {
                const Outcome *o = &r->next[k];
                sum += o->pct;
                CHECK(o->sp < SP_COUNT && o->sp2 < SP_COUNT);
                if (o->sp)
                    CHECK(garden_rule(o->sp, o->st) != NULL);
                if (o->sp2)
                    CHECK(garden_rule(o->sp2, o->st2) != NULL);
                CHECK(o->ev < EV_KIND_COUNT && o->item < ITEM_COUNT);
            }
            if (r->dur) {
                CHECK_EQ(sum, 100);
                CHECK(r->then != NULL);
            } else {
                CHECK_EQ(sum, 0);
            }
        }
        CHECK(stages > 0);
    }
    for (const GardenReact *r = GARDEN_REACT; r->sp; r++) {
        CHECK(garden_rule(r->sp, r->st) != NULL);
        CHECK(r->out.pct > 0);
    }
    for (const GardenInteract *it = GARDEN_INTERACT; it->asp; it++) {
        CHECK(garden_rule(it->asp, it->ast) != NULL);
        CHECK(it->tsp != SP_NONE && it->pct > 0 && it->pct <= 100);
    }
    for (const GardenSpawn *sp = GARDEN_SPAWN; sp->sp; sp++) {
        CHECK(garden_rule(sp->sp, sp->st) != NULL);
        CHECK(sp->permille > 0 && sp->seasons != 0);
    }
    for (const GardenDiscover *d = GARDEN_DISCOVER; d->sp; d++)
        CHECK(d->slot < COLL_COUNT);
    /* Los sprites tienen las filas del tamaño que dicen. */
    CHECK_EQ(art_selfcheck(), 0);
    /* Todo evento tiene texto para el diario y cabe junto a "hace 3 h". */
    for (int k = 1; k < EV_KIND_COUNT; k++) {
        DiaryEntry d = {0, (uint8_t)k, 1};
        char buf[32] = "";
        CHECK(state_event_text(buf, sizeof(buf), &d));
        CHECK(strlen(buf) > 0 && gfx_text_width(buf) <= 84);
    }
    CHECK_EQ(garden_season(&(GameState){.tick = t_autumn() / LLV_TICK_SECONDS}), SEASON_AUTUMN);
}

static void test_state_size(void)
{
    GameState gs;
    state_init(&gs, 1, t_autumn());
    CHECK(sizeof(GameState) < 4096);
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = ser(&gs, buf);
    CHECK(n > 0 && n < 4096 && n <= LLV_SAVE_MAX);
    printf("    (GameState: %zu bytes en memoria, %zu en el guardado)\n", sizeof(GameState), n);
}

static void test_garden_init(void)
{
    GameState gs;
    state_init(&gs, 5, t_autumn());
    CHECK(garden_ok(&gs));
    CHECK(garden_count(&gs) >= 6);
    CHECK_EQ(gs.collection, 0); /* el jardín inicial no cuenta como descubierto */
    CHECK_EQ(garden_count_sp(&gs, SP_PIEDRA, ST_ANY), 2);
    /* Otra semilla, otro jardín. */
    GameState other;
    state_init(&other, 6, t_autumn());
    CHECK(!same_state(&gs, &other));
}

/* ---------- determinismo con todo el estado nuevo ---------- */

typedef struct {
    int64_t at;
    int kind; /* 0 jardín, 1 preparar, 2 cuidar mascota */
} Act;

static void do_act(GameState *gs, const Act *a)
{
    char msg[96];
    switch (a->kind) {
    case 0: garden_act(gs, 1, NULL, NULL, NULL); break;
    case 1:
        if (gs->bag_count)
            gs->prepared = gs->bag[gs->bag_count - 1];
        break;
    default: pet_care(gs, 5, msg, sizeof(msg)); break;
    }
}

static int make_acts(Act *acts, int max, int64_t start, int days, uint64_t seed)
{
    Rng r;
    rng_seed(&r, seed, 3);
    int n = 0;
    int64_t t = start;
    while (n < max) {
        t += 600 + (int64_t)rng_below(&r, 6 * 3600);
        if (t >= start + (int64_t)days * 86400)
            break;
        acts[n].at = t;
        acts[n].kind = (int)rng_below(&r, 3);
        n++;
    }
    return n;
}

static void test_garden_determinism_live_vs_catchup(void)
{
    const int64_t days[] = {1, 3, 7};
    for (unsigned k = 0; k < sizeof(days) / sizeof(days[0]); k++) {
        int64_t end = t_autumn() + days[k] * 86400;
        GameState live, batch;
        state_init(&live, 4321, t_autumn());
        state_init(&batch, 4321, t_autumn());
        pet_choose(&live, PET_HONGO);
        pet_choose(&batch, PET_HONGO);
        for (int64_t t = t_autumn(); t <= end; t++)
            engine_advance(&live, t);
        CHECK_EQ(engine_advance(&batch, end), days[k] * 144);
        CHECK(same_state(&live, &batch));
        CHECK(garden_ok(&live));
    }
}

static void test_garden_determinism_with_actions(void)
{
    /* Mismas acciones a las mismas horas: A avanza segundo a segundo; B salta de acción en
     * acción. El estado debe coincidir byte a byte con jardín, bolsa, clima y mascota. */
    for (int kind = PET_HONGO; kind < PET_COUNT; kind++) {
        static Act acts[256];
        int n = make_acts(acts, 256, t_autumn(), 12, 100 + (uint64_t)kind);
        GameState a, b;
        state_init(&a, 777 + (uint64_t)kind, t_autumn());
        state_init(&b, 777 + (uint64_t)kind, t_autumn());
        pet_choose(&a, (PetKind)kind);
        pet_choose(&b, (PetKind)kind);
        int64_t end = t_autumn() + 12 * 86400;
        int next = 0;
        for (int64_t t = t_autumn(); t <= end; t++) {
            engine_advance(&a, t);
            while (next < n && acts[next].at == t) {
                do_act(&a, &acts[next]);
                next++;
            }
        }
        for (int i = 0; i < n; i++) {
            engine_advance(&b, acts[i].at);
            do_act(&b, &acts[i]);
        }
        engine_advance(&b, end);
        /* Los dos recorrieron los 12 días (más allá del tope de catch-up por tramos). */
        CHECK(same_state(&a, &b));
        CHECK(garden_ok(&a));
        CHECK(garden_count(&a) > 0);
    }
}

static void gesture(Engine *e, uint8_t btn, int64_t now, uint32_t *ms, bool dense, uint32_t hold_ms)
{
    RawInput down = {btn, false}, up = {0, false};
    uint32_t t0 = *ms;
    if (dense) {
        for (uint32_t t = 0; t < hold_ms; t += 33)
            engine_frame(e, down, now, t0 + t);
        engine_frame(e, up, now, t0 + hold_ms);
        for (uint32_t t = 100; t <= 400; t += 100)
            engine_frame(e, up, now, t0 + hold_ms + t);
    } else {
        engine_frame(e, down, now, t0);
        engine_frame(e, down, now, t0 + hold_ms);
        engine_frame(e, up, now, t0 + hold_ms + 20);
    }
    *ms = t0 + hold_ms + 400;
}

/* Un guion de gestos aleatorio (pero el mismo siempre). 'dense' pinta un fotograma cada 33 ms;
 * si no, solo los justos. Guarda una foto del estado tras cada paso. */
#define SCRIPT_STEPS 60
static uint8_t snaps[2][SCRIPT_STEPS][LLV_SAVE_MAX];
static size_t snap_len[2][SCRIPT_STEPS];

static void run_gesture_script(Engine *e, bool dense, int slot)
{
    uint32_t ms = 1000;
    jardin_ui_reset();
    cobertizo_ui_reset();
    engine_init(e, 99, t_autumn(), ms);
    RawInput none = {0, false};
    int64_t now = t_autumn();
    Rng r;
    rng_seed(&r, 21, 2);
    for (int step = 0; step < SCRIPT_STEPS; step++) {
        now += 60 + (int64_t)rng_below(&r, 4 * 3600);
        ms += 5000;
        engine_frame(e, none, now, ms);
        int world = (int)rng_below(&r, 3); /* 0: jardín, 1: cobertizo (preparar), 2: cobertizo (mascota) */
        for (int i = 0; i < 6 && e->gs.current_world != (world == 0 ? 1 : 5); i++)
            gesture(e, BTN_B, now, &ms, dense, 80);
        if (e->gs.current_world == 1) {
            gesture(e, BTN_A, now, &ms, dense, 80);
            gesture(e, BTN_A, now, &ms, dense, LLV_HOLD_MS + 100);
        } else if (e->gs.current_world == 5) {
            if (e->gs.pet.kind == PET_NONE)
                gesture(e, BTN_A, now, &ms, dense, LLV_HOLD_MS + 100);
            gesture(e, BTN_A, now, &ms, dense, LLV_HOLD_MS + 100);
        }
        engine_sleep(e, now + 20);
        now += 20;
        snap_len[slot][step] = state_serialize(&e->gs, snaps[slot][step], LLV_SAVE_MAX);
    }
}

static void test_engine_determinism_gestures(void)
{
    /* Dos motores reciben los mismos gestos a las mismas horas; uno pinta fotogramas todo el
     * rato y el otro solo los justos. El estado (jardín, bolsa, mascota) debe ser idéntico
     * tras cada paso, byte a byte. */
    static Engine a, b;
    run_gesture_script(&a, true, 0);
    run_gesture_script(&b, false, 1);
    for (int step = 0; step < SCRIPT_STEPS; step++) {
        CHECK(snap_len[0][step] > 0 && snap_len[0][step] == snap_len[1][step]);
        CHECK(memcmp(snaps[0][step], snaps[1][step], snap_len[0][step]) == 0);
    }
    CHECK(garden_ok(&a.gs));
    CHECK(a.gs.pet.kind != PET_NONE);
    CHECK(a.gs.looks_total >= SCRIPT_STEPS);
    CHECK(a.gs.actions_used[1] + a.gs.actions_used[5] + a.gs.diary_total > 0);
}

/* ---------- límite de 32 y vida a largo plazo ---------- */

static void test_garden_never_overflows(void)
{
    /* 1.200 días (3,3 años), con muchos objetos gastados (se rompe el límite diario a
     * propósito) y mirando las invariantes en cada macro tick. */
    GameState gs;
    state_init(&gs, 2024, t_autumn());
    pet_choose(&gs, PET_COMPOSTERA);
    Rng r;
    rng_seed(&r, 5, 5);
    int64_t t = t_autumn();
    int max_seen = 0, used = 0, sum = 0, ticks = 0;
    for (int day = 0; day < 1200; day++) {
        for (int k = 0; k < 6; k++) { /* 6 usos al día: mucho más de lo permitido */
            fill_bag(&gs, &r);
            gs.actions_used[1] = 0;
            used += garden_act(&gs, 1, NULL, NULL, NULL) == ACT_OK;
        }
        for (int h = 0; h < 144; h++) {
            t += LLV_TICK_SECONDS;
            engine_advance(&gs, t);
            int c = garden_count(&gs);
            if (c > max_seen)
                max_seen = c;
            sum += c;
            ticks++;
            if (!garden_ok(&gs)) {
                CHECK(0 && "invariante del jardin rota");
                return;
            }
        }
    }
    CHECK(used >= 7000);
    CHECK(max_seen <= LLV_GARDEN_MAX);
    CHECK(max_seen >= 10);              /* sí hay vida */
    CHECK(sum / ticks >= 6);            /* y no se queda vacío */
    printf("    (1200 dias, uso extremo: media %d cosas, maximo %d, %d usos)\n", sum / ticks, max_seen, used);
}

static void test_garden_realistic_week_is_alive(void)
{
    /* Uso normal (2 acciones al día, como manda el límite) durante 400 días: el jardín vive,
     * no se vacía y no se satura. */
    GameState gs;
    state_init(&gs, 4040, t_autumn());
    pet_choose(&gs, PET_HONGO);
    int64_t t = t_autumn();
    long sum = 0, ticks = 0;
    int max_seen = 0, min_seen = 99;
    for (int day = 0; day < 400; day++) {
        for (int h = 0; h < 144; h++) {
            t += LLV_TICK_SECONDS;
            engine_advance(&gs, t);
            if (day >= 10) {
                int c = garden_count(&gs);
                sum += c;
                ticks++;
                if (c > max_seen)
                    max_seen = c;
                if (c < min_seen)
                    min_seen = c;
            }
            if (h == 20 || h == 90) /* dos miradas al día */
                garden_act(&gs, 1, NULL, NULL, NULL);
        }
    }
    CHECK(sum / ticks >= 9 && sum / ticks <= 24);
    CHECK(min_seen >= 4);
    CHECK(max_seen <= LLV_GARDEN_MAX);
    printf("    (400 dias, uso normal: media %ld cosas, entre %d y %d)\n", sum / ticks, min_seen, max_seen);
}

static void test_garden_full_spawns_nothing(void)
{
    GameState gs;
    state_init(&gs, 9, t_autumn());
    ent_clear(&gs);
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        ent_put(&gs, SP_PIEDRA, 0, i, 0);
    CHECK_EQ(garden_count(&gs), LLV_GARDEN_MAX);
    CHECK(garden_place(&gs, SP_FLOR, 0, -1, true) == NULL);
    CHECK(garden_place(&gs, SP_SEMILLA, 0, 3, false) == NULL);
    /* Ni los objetos ni el tiempo hacen nacer nada en un jardín lleno. */
    Rng r;
    rng_seed(&r, 1, 1);
    for (int i = 0; i < 40; i++) {
        fill_bag(&gs, &r);
        gs.actions_used[1] = 0;
        garden_act(&gs, 1, NULL, NULL, NULL);
        for (int k = 0; k < 60; k++)
            garden_tick(&gs);
        CHECK_EQ(garden_count(&gs), LLV_GARDEN_MAX);
        CHECK(garden_ok(&gs));
    }
    /* Las piedras no cambian: siguen siendo las 32. */
    CHECK_EQ(garden_count_sp(&gs, SP_PIEDRA, ST_ANY), LLV_GARDEN_MAX);
}

static void test_species_caps(void)
{
    /* Sin tocar nada durante 300 días, los topes por especie se cumplen siempre. */
    GameState gs;
    state_init(&gs, 31, t_autumn());
    int64_t t = t_autumn();
    for (int day = 0; day < 300; day++) {
        t += 86400;
        engine_advance(&gs, t);
        for (int sp = 1; sp < SP_COUNT; sp++)
            CHECK(garden_count_sp(&gs, (uint8_t)sp, ST_ANY) <= GARDEN_SPECIES[sp].cap + 1);
        int trees = garden_count_sp(&gs, SP_BROTE, ST_ANY) + garden_count_sp(&gs, SP_ARBOL_JOVEN, ST_ANY) +
                    garden_count_sp(&gs, SP_ARBOL, ST_ANY);
        CHECK(trees <= LLV_TREE_MAX);
    }
}

/* ---------- acciones: límite de 2 al día y reinicio a las 05:00 ---------- */

static void test_garden_action_limit(void)
{
    int64_t t = time_from_civil(2026, 10, 22, 4, 30, 0);
    GameState gs;
    state_init(&gs, 8, t);
    Rng r;
    rng_seed(&r, 3, 3);
    fill_bag(&gs, &r);
    CHECK_EQ(garden_act(&gs, 1, NULL, NULL, NULL), ACT_OK);
    CHECK_EQ(garden_act(&gs, 1, NULL, NULL, NULL), ACT_OK);
    CHECK_EQ(gs.actions_used[1], 2);
    CHECK_EQ(gs.bag_count, 3);
    CHECK_EQ(garden_act(&gs, 1, NULL, NULL, NULL), ACT_NO_ACTIONS);
    CHECK_EQ(gs.bag_count, 3); /* no gasta nada */
    CHECK_EQ(gs.actions_used[1], 2);
    /* A las 04:50 sigue el mismo día del llavero; a las 05:00, no. */
    engine_advance(&gs, time_from_civil(2026, 10, 22, 4, 50, 0));
    fill_bag(&gs, &r);
    CHECK_EQ(garden_act(&gs, 1, NULL, NULL, NULL), ACT_NO_ACTIONS);
    engine_advance(&gs, time_from_civil(2026, 10, 22, 5, 0, 0));
    CHECK_EQ(gs.actions_used[1], 0);
    fill_bag(&gs, &r);
    CHECK_EQ(garden_act(&gs, 1, NULL, NULL, NULL), ACT_OK);
    CHECK_EQ(garden_act(&gs, 1, NULL, NULL, NULL), ACT_OK);
    CHECK_EQ(garden_act(&gs, 1, NULL, NULL, NULL), ACT_NO_ACTIONS);
    /* Bolsa vacía: no es "descansa" sino "sin objetos" (y tampoco gasta la acción). */
    GameState empty;
    state_init(&empty, 8, t);
    empty.bag_count = 0;
    memset(empty.bag, 0, sizeof(empty.bag));
    CHECK_EQ(garden_act(&empty, 1, NULL, NULL, NULL), ACT_NO_ITEMS);
    CHECK_EQ(empty.actions_used[1], 0);
}

static int has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

static void test_engine_garden_messages(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = time_from_civil(2026, 10, 22, 12, 0, 0);
    engine_init(&e, 4, now, ms);
    RawInput none = {0, false};
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = 1;
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK(has(e.toast, "vac")); /* "La bolsa está vacía" */
    CHECK_EQ(e.gs.actions_used[1], 0);
    state_bag_add(&e.gs, ITEM_NUBE);
    state_bag_add(&e.gs, ITEM_VIENTO);
    state_bag_add(&e.gs, ITEM_SEMILLA);
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK(has(e.toast, "Sacaste: "));
    CHECK_EQ(e.gs.actions_used[1], 2 - 1);
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK_EQ(e.gs.actions_used[1], 2);
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK(has(e.toast, "descansa"));
    CHECK(has(e.toast, "17 h")); /* de las 12:00 a las 05:00 del día siguiente */
    CHECK_EQ(e.gs.actions_used[1], 2);
    CHECK_EQ(e.gs.bag_count, 1);
}

static void test_prepared_item(void)
{
    GameState gs;
    state_init(&gs, 3, t_autumn());
    gs.bag_count = 0;
    memset(gs.bag, 0, sizeof(gs.bag));
    state_bag_add(&gs, ITEM_NUBE);
    state_bag_add(&gs, ITEM_LOMBRIZ);
    state_bag_add(&gs, ITEM_SEMILLA);
    gs.prepared = ITEM_LOMBRIZ;
    ItemKind used;
    int prep = 0;
    const char *msg = "";
    CHECK_EQ(garden_act(&gs, 1, &used, &prep, &msg), ACT_OK);
    CHECK_EQ(used, ITEM_LOMBRIZ);
    CHECK_EQ(prep, 1);
    CHECK_EQ(gs.prepared, ITEM_NONE);
    CHECK_EQ(gs.bag_count, 2);
    CHECK(strlen(msg) > 0);
    CHECK(garden_count_sp(&gs, SP_LOMBRIZ, ST_ANY) >= 1);
    /* El diario lo anota. */
    const DiaryEntry *d = NULL;
    for (int i = 0; i < gs.diary_count && !d; i++)
        if (state_diary_get(&gs, i)->kind == EV_ITEM_USED)
            d = state_diary_get(&gs, i);
    CHECK(d && d->a == ITEM_LOMBRIZ);
    /* Sin preparado: aleatorio, y prep = 0. */
    CHECK_EQ(garden_act(&gs, 1, &used, &prep, &msg), ACT_OK);
    CHECK_EQ(prep, 0);
    CHECK(used == ITEM_NUBE || used == ITEM_SEMILLA);
}

/* ---------- efectos de los objetos ---------- */

static void quiet_state(GameState *gs, int64_t t)
{
    state_init(gs, 12, t);
    ent_clear(gs);
    gs->weather = W_CLEAR;
    gs->weather_left = 0;
    gs->bag_count = 0;
    memset(gs->bag, 0, sizeof(gs->bag));
}

static void test_item_effects(void)
{
    GameState gs;
    ItemKind used;
    int prep;
    const char *msg;
    /* Nube: lluvia. */
    quiet_state(&gs, t_autumn() + 8 * 3600);
    state_bag_add(&gs, ITEM_NUBE);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(gs.weather, W_RAIN);
    CHECK(gs.weather_left >= LLV_RAIN_ITEM_TICKS);
    CHECK_EQ(count_ev(&gs, EV_RAIN), 1);
    /* Nube en invierno: nieva. */
    quiet_state(&gs, time_from_civil(2026, 1, 15, 10, 0, 0));
    state_bag_add(&gs, ITEM_NUBE);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(gs.weather, W_SNOW);
    /* Viento: cae la fruta (junto al árbol) y vuelan semillas. */
    quiet_state(&gs, t_autumn() + 8 * 3600);
    ent_put(&gs, SP_ARBOL, 1, 10, 500);
    ent_put(&gs, SP_ARBOL, 1, 20, 500);
    state_bag_add(&gs, ITEM_VIENTO);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(garden_count_sp(&gs, SP_ARBOL, 1), 0);
    CHECK_EQ(garden_count_sp(&gs, SP_ARBOL, 0), 2);
    CHECK_EQ(garden_count_sp(&gs, SP_MANZANA, 0), 2);
    CHECK(garden_count_sp(&gs, SP_SEMILLA, ST_ANY) >= 1);
    CHECK_EQ(count_ev(&gs, EV_APPLE_FALL), 2);
    CHECK(has(msg, "fruta"));
    CHECK(garden_ok(&gs));
    /* Semilla: planta algo. */
    quiet_state(&gs, t_autumn());
    state_bag_add(&gs, ITEM_SEMILLA);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(garden_count(&gs), 1);
    CHECK(has(msg, "Plantaste"));
    /* Alpiste: baja un pájaro goloso; con uno ya presente, se queda. */
    quiet_state(&gs, t_autumn());
    state_bag_add(&gs, ITEM_ALPISTE);
    state_bag_add(&gs, ITEM_ALPISTE);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(garden_count_sp(&gs, SP_PAJARO, 1), 1);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(garden_count_sp(&gs, SP_PAJARO, ST_ANY), 1);
    CHECK(state_collect(&gs, COLL_PAJARO) == 0); /* ya descubierto al llegar */
    /* Lombriz: abono (las plantas tienen menos que esperar) y una lombriz. */
    quiet_state(&gs, t_autumn());
    ent_put(&gs, SP_BROTE, 0, 4, 100);
    state_bag_add(&gs, ITEM_LOMBRIZ);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(garden_count_sp(&gs, SP_LOMBRIZ, ST_ANY), 1);
    CHECK_EQ(gs.ents[0].left, 50);
    /* Deseo: de día arcoíris, de noche estrella fugaz; y siempre una flor rara. */
    quiet_state(&gs, time_from_civil(2026, 10, 22, 12, 0, 0));
    state_bag_add(&gs, ITEM_DESEO);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(gs.sky, SKY_RAINBOW);
    CHECK(gs.collection & (1u << COLL_ARCOIRIS));
    CHECK_EQ(garden_count_sp(&gs, SP_FLOR, 3), 1);
    CHECK(gs.collection & (1u << COLL_FLOR_RARA));
    quiet_state(&gs, time_from_civil(2026, 10, 22, 23, 0, 0));
    state_bag_add(&gs, ITEM_DESEO);
    garden_act(&gs, 1, &used, &prep, &msg);
    CHECK_EQ(gs.sky, SKY_STAR);
    CHECK(gs.collection & (1u << COLL_ESTRELLA));
    CHECK(has(msg, "estrella"));
}

/* ---------- cadenas ---------- */

/* Una manzana podrida con una sola vuelta de reloj por delante: ¿en qué se convierte? */
static void test_rotten_apple_chain_statistics(void)
{
    const int N = 20000;
    int brote = 0, hongo = 0, nada = 0;
    GameState gs;
    state_init(&gs, 77, time_from_civil(2026, 4, 10, 10, 0, 0)); /* primavera, sin nieve */
    for (int i = 0; i < N; i++) {
        ent_clear(&gs);
        gs.weather = W_CLEAR;
        gs.weather_left = 0;
        ent_put(&gs, SP_MANZANA, 1, 17, 1);
        garden_tick(&gs);
        /* Otras cosas pueden nacer solas ese tick, pero ni brotes ni hongos (sin lluvia). */
        if (garden_count_sp(&gs, SP_BROTE, ST_ANY))
            brote++;
        else if (garden_count_sp(&gs, SP_HONGO, ST_ANY))
            hongo++;
        else
            nada++;
    }
    CHECK_EQ(brote + hongo + nada, N);
    CHECK(abs(brote * 100 / N - 40) <= 2);
    CHECK(abs(hongo * 100 / N - 30) <= 2);
    CHECK(abs(nada * 100 / N - 30) <= 2);
    printf("    (podrida -> brote %d %%, hongo %d %%, nada %d %%)\n", brote * 100 / N, hongo * 100 / N, nada * 100 / N);
}

/* Ticks hasta que 'e' cambie de etapa, con cielo despejado y sin pájaros que se la coman. */
static int ticks_until_change(GameState *gs, Entity *e, int max)
{
    uint8_t sp = e->sp, st = e->st;
    for (int k = 1; k <= max; k++) {
        gs->weather = W_CLEAR;
        gs->weather_left = 0;
        garden_tick(gs);
        for (int j = 0; j < LLV_GARDEN_MAX; j++)
            if (gs->ents[j].sp == SP_PAJARO)
                garden_remove(gs, &gs->ents[j]);
        if (e->sp != sp || e->st != st)
            return k;
    }
    return -1;
}

static void test_apple_chain_timing(void)
{
    /* Fresca ~2 días (288 ticks) -> podrida ~1 día (144) -> brote | hongo | nada, con ±25 %.
     * En invierno no llueve (no hay lombrices ni lluvia que acelere) y se quitan los pájaros. */
    GameState gs;
    state_init(&gs, 5, time_from_civil(2026, 1, 15, 10, 0, 0));
    int min1 = 1 << 30, max1 = 0, min2 = 1 << 30, max2 = 0, kept = 0;
    long sum1 = 0, sum2 = 0;
    for (int i = 0; i < 300; i++) {
        ent_clear(&gs);
        ent_put(&gs, SP_ARBOL, 1, 9, 1);
        garden_tick(&gs); /* el árbol suelta su manzana al acabarse su cuenta */
        Entity *apple = NULL;
        for (int j = 0; j < LLV_GARDEN_MAX; j++)
            if (gs.ents[j].sp == SP_MANZANA)
                apple = &gs.ents[j];
        CHECK(apple != NULL);
        if (!apple)
            return;
        int fresh = ticks_until_change(&gs, apple, 500);
        if (fresh < 0 || apple->sp != SP_MANZANA || apple->st != 1)
            continue; /* un pájaro se la llevó: no cuenta */
        int rot = ticks_until_change(&gs, apple, 500);
        if (rot < 0)
            continue;
        kept++;
        sum1 += fresh;
        sum2 += rot;
        if (fresh < min1) min1 = fresh;
        if (fresh > max1) max1 = fresh;
        if (rot < min2) min2 = rot;
        if (rot > max2) max2 = rot;
    }
    CHECK(kept >= 150);
    CHECK(min1 >= 216 - 2 && max1 <= 360);
    CHECK(min2 >= 108 && max2 <= 180);
    CHECK(abs((int)(sum1 / kept) - 288) < 12); /* ~2 días */
    CHECK(abs((int)(sum2 / kept) - 144) < 8);
}

static void test_wind_drops_fruit(void)
{
    GameState gs;
    state_init(&gs, 5, time_from_civil(2026, 4, 10, 10, 0, 0));
    ent_clear(&gs);
    ent_put(&gs, SP_ARBOL, 1, 9, 400);
    ent_put(&gs, SP_HOJAS, 0, 20, 400);
    gs.weather = W_WIND;
    gs.weather_left = 100;
    int fell = 0;
    for (int k = 0; k < 40 && !fell; k++) {
        gs.weather = W_WIND;
        gs.weather_left = 100;
        garden_tick(&gs);
        fell = garden_count_sp(&gs, SP_MANZANA, 0) > 0;
    }
    CHECK(fell);
    CHECK_EQ(garden_count_sp(&gs, SP_ARBOL, 0), 1);
    CHECK(count_ev(&gs, EV_APPLE_FALL) >= 1);
    CHECK(gs.collection & (1u << COLL_MANZANA));
    /* La manzana cayó al lado del árbol. */
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        if (gs.ents[i].sp == SP_MANZANA) {
            int dx = abs(gs.ents[i].cell % LLV_GARDEN_COLS - 9 % LLV_GARDEN_COLS);
            int dy = abs(gs.ents[i].cell / LLV_GARDEN_COLS - 9 / LLV_GARDEN_COLS);
            CHECK(dx <= 1 && dy <= 1);
        }
}

static void test_bird_eats_apple_and_leaves_seed(void)
{
    int ate = 0;
    for (int seed = 0; seed < 40; seed++) {
        GameState gs;
        state_init(&gs, (uint64_t)seed, time_from_civil(2026, 4, 10, 10, 0, 0));
        ent_clear(&gs);
        gs.bag_count = 0;
        memset(gs.bag, 0, sizeof(gs.bag));
        ent_put(&gs, SP_MANZANA, 0, 3, 200);
        ent_put(&gs, SP_PAJARO, 0, 30, 12);
        for (int k = 0; k < 14; k++) {
            gs.weather = W_CLEAR;
            garden_tick(&gs);
        }
        if (garden_count_sp(&gs, SP_MANZANA, ST_ANY) == 0 && count_ev(&gs, EV_BIRD_SEED) == 1) {
            ate++;
            CHECK(garden_count_sp(&gs, SP_SEMILLA, ST_ANY) >= 1);
            int in_bag = 0;
            for (int k = 0; k < gs.bag_count; k++)
                in_bag += gs.bag[k] == ITEM_SEMILLA;
            CHECK_EQ(in_bag, 1); /* y deja una semilla en la bolsa */
        }
    }
    CHECK(ate >= 15); /* 12 ticks al 18 % por tick: la mayoría de las veces se la come */
}

static void test_rain_speeds_growth(void)
{
    GameState dry, wet;
    state_init(&dry, 3, time_from_civil(2026, 4, 10, 10, 0, 0));
    wet = dry;
    ent_clear(&dry);
    ent_clear(&wet);
    Entity *a = ent_put(&dry, SP_BROTE, 0, 5, 200);
    Entity *b = ent_put(&wet, SP_BROTE, 0, 5, 200);
    for (int k = 0; k < 40; k++) {
        dry.weather = W_CLEAR;
        wet.weather = W_RAIN;
        wet.weather_left = 50;
        garden_tick(&dry);
        garden_tick(&wet);
    }
    CHECK_EQ(a->left, 160);
    CHECK_EQ(b->left, 200 - 40 * LLV_WET_MULT);
    /* Las ranas salen de los charcos que deja la lluvia. */
    int frogs = 0;
    for (int seed = 0; seed < 200; seed++) {
        GameState gs;
        state_init(&gs, (uint64_t)seed, time_from_civil(2026, 4, 10, 10, 0, 0));
        ent_clear(&gs);
        ent_put(&gs, SP_CHARCO, 0, 5, 1);
        gs.weather = W_CLEAR;
        garden_tick(&gs);
        frogs += garden_count_sp(&gs, SP_RANA, ST_ANY);
    }
    CHECK(frogs >= 12 && frogs <= 36); /* ~12 % */
}

static int simulate_days(GameState *gs, int days, int *seen_flags)
{
    int64_t t = gs->tick * LLV_TICK_SECONDS;
    for (int d = 0; d < days; d++)
        for (int h = 0; h < 144; h++) {
            t += LLV_TICK_SECONDS;
            engine_advance(gs, t);
            for (int sp = 1; sp < SP_COUNT; sp++)
                if (garden_count_sp(gs, (uint8_t)sp, ST_ANY))
                    seen_flags[sp] = 1;
        }
    return 0;
}

static void test_seasons(void)
{
    int winter[SP_COUNT] = {0}, summer[SP_COUNT] = {0}, autumn[SP_COUNT] = {0};
    GameState gs;
    state_init(&gs, 14, time_from_civil(2026, 1, 5, 6, 0, 0));
    simulate_days(&gs, 40, winter);
    state_init(&gs, 14, time_from_civil(2026, 7, 5, 6, 0, 0));
    simulate_days(&gs, 40, summer);
    state_init(&gs, 14, time_from_civil(2026, 10, 5, 6, 0, 0));
    simulate_days(&gs, 40, autumn);
    CHECK(winter[SP_NIEVE]);       /* nieva en invierno... */
    CHECK(!summer[SP_NIEVE]);      /* ...y nunca en verano */
    CHECK(!autumn[SP_NIEVE] || garden_season(&gs) == SEASON_WINTER);
    CHECK(autumn[SP_HOJAS]);       /* hojas secas en otoño */
    CHECK(!summer[SP_HOJAS]);
    CHECK(summer[SP_ABEJA] || summer[SP_FLOR]);
}

static void test_collection(void)
{
    GameState gs;
    state_init(&gs, 1, t_autumn());
    CHECK_EQ(state_collect(&gs, COLL_RANA), 1);
    CHECK_EQ(state_collect(&gs, COLL_RANA), 0);
    CHECK_EQ(count_ev(&gs, EV_DISCOVER), 1);
    CHECK_EQ(gs.collection, 1u << COLL_RANA);
    CHECK_EQ(state_collect(&gs, COLL_PEZ5), 1); /* gancho para la fase 5 */
    CHECK_EQ(state_collect(&gs, COLL_COUNT), 0);
    CHECK_STR(state_coll_name(COLL_RANA), "Rana");
    /* Nacer algo nuevo lo descubre; el jardín inicial no. */
    ent_clear(&gs);
    gs.collection = 0;
    garden_place(&gs, SP_HONGO, 0, -1, false);
    CHECK(gs.collection & (1u << COLL_HONGO));
}

/* ---------- mascota ---------- */

static void test_pet_choose_and_ficha(void)
{
    GameState gs;
    state_init(&gs, 2, t_autumn());
    CHECK(!pet_choose(&gs, PET_NONE));
    CHECK(!pet_choose(&gs, PET_COUNT));
    CHECK(pet_choose(&gs, PET_HONGO));
    CHECK(!pet_choose(&gs, PET_PIEDRA)); /* ya hay una */
    CHECK_EQ(gs.pet.kind, PET_HONGO);
    CHECK_EQ(gs.pet.gen, 1);
    /* El hongo del ejemplo del spec: día 43, 7 px, "hm.". */
    gs.pet.age = 42 * LLV_TICKS_PER_DAY + 5;
    gs.pet.thought = 0;
    char lines[10][24];
    int n = pet_ficha(&gs.pet, lines, 10);
    CHECK(n >= 8);
    CHECK_STR(lines[0], "HONGO");
    CHECK_STR(lines[1], "Gen 1 · Día 43");
    CHECK_STR(lines[2], "Altura: 7 px");
    CHECK_STR(lines[3], "(+0,1 hoy)");
    CHECK_STR(lines[4], "Humor:");
    CHECK_STR(lines[n - 1], "\"hm.\"");
    for (int i = 0; i < n; i++)
        CHECK(gfx_text_width(lines[i]) <= 104);
    /* Rarezas por mascota. */
    Pet p = gs.pet;
    p.kind = PET_PIEDRA;
    n = pet_ficha(&p, lines, 10);
    CHECK_STR(lines[2], "Peso: 120 g");
    CHECK_STR(lines[3], "(estable)");
    CHECK_STR(pet_thought(&p), ""); /* nunca dice nada */
    p.kind = PET_COMPOSTERA;
    n = pet_ficha(&p, lines, 10);
    CHECK_STR(lines[3], "Productividad:");
    CHECK_STR(lines[4], "+0,2 %");
    CHECK_EQ(pet_worms(&p), 3 + 42 * 144 * 9 / LLV_PET_CYCLE_TICKS);
    for (int k = 1; k < PET_COUNT; k++) {
        p.kind = (uint8_t)k;
        p.age = 0;
        CHECK_EQ(pet_stage(&p), 0);
        p.age = LLV_PET_CYCLE_TICKS - 1;
        CHECK_EQ(pet_stage(&p), pet_stage_count((PetKind)k) - 1);
        CHECK(strlen(pet_name((PetKind)k)) > 0 && strlen(pet_blurb((PetKind)k)) > 0);
    }
}

static void test_pet_never_dies_and_generations(void)
{
    for (int kind = PET_HONGO; kind < PET_COUNT; kind++) {
        GameState gs;
        state_init(&gs, 50 + (uint64_t)kind, t_autumn());
        pet_choose(&gs, (PetKind)kind);
        int64_t t = t_autumn();
        int min_happy = 255;
        EvCount ev = {0};
        /* 3,3 ciclos sin cuidarla jamás. */
        for (int d = 0; d < 335; d++) {
            for (int h = 0; h < 144; h++) {
                t += LLV_TICK_SECONDS;
                engine_advance(&gs, t);
                if (gs.pet.happy < min_happy)
                    min_happy = gs.pet.happy;
                CHECK_EQ(gs.pet.kind, kind);
                CHECK(gs.pet.age < LLV_PET_CYCLE_TICKS);
            }
            ev_update(&ev, &gs);
            if (d == 99) /* justo al completar los 100 días */
                CHECK_EQ(gs.pet.gen, 2);
        }
        CHECK_EQ(gs.pet.gen, 4);
        CHECK(min_happy >= 30);                    /* como mucho, pensativa */
        CHECK(strlen(pet_mood(&gs.pet)) > 0);
        CHECK_EQ(ev.n[EV_PET_GEN], 3);
        /* cada ciclo completo cambia de etapa (n-1) veces, más las del ciclo en curso */
        CHECK_EQ(ev.n[EV_PET_STAGE], 3 * (pet_stage_count((PetKind)kind) - 1) + pet_stage(&gs.pet));
        CHECK_EQ(ev.n[EV_PET_CHOSEN], 1);
        CHECK(garden_ok(&gs));
    }
    /* Cronometrado: el ciclo dura exactamente LLV_PET_CYCLE_TICKS ticks. */
    GameState gs;
    state_init(&gs, 1, t_autumn());
    pet_choose(&gs, PET_PIEDRA);
    for (int k = 0; k < LLV_PET_CYCLE_TICKS - 1; k++)
        pet_tick(&gs);
    CHECK_EQ(gs.pet.gen, 1);
    pet_tick(&gs);
    CHECK_EQ(gs.pet.gen, 2);
    CHECK_EQ(gs.pet.age, 0);
}

static void test_pet_stage_events(void)
{
    GameState gs;
    state_init(&gs, 6, t_autumn());
    pet_choose(&gs, PET_HONGO);
    int64_t t = t_autumn();
    for (int d = 0; d < 99; d++) {
        t += 86400;
        engine_advance(&gs, t);
    }
    /* 6 etapas: 5 cambios en un ciclo (el diario circular puede haber pisado algunos). */
    CHECK(gs.pet.age >= 90 * 144 || gs.pet.gen > 1);
    CHECK(pet_stage(&gs.pet) == 5);
    CHECK_STR(pet_stage_name(&gs.pet), "anciano con barba");
}

static void test_pet_care_daily_limit(void)
{
    GameState gs;
    state_init(&gs, 4, t_autumn());
    char msg[96];
    CHECK_EQ(pet_care(&gs, 5, msg, sizeof(msg)), CARE_NO_PET);
    pet_choose(&gs, PET_HONGO);
    CHECK_EQ(pet_care(&gs, 5, msg, sizeof(msg)), CARE_OK); /* saludar */
    CHECK(strlen(msg) > 0);
    CHECK_EQ(gs.actions_used[5], 1);
    CHECK_EQ(pet_care(&gs, 5, msg, sizeof(msg)), CARE_NONE_LEFT);
    CHECK(has(msg, "Mañana"));
    CHECK_EQ(gs.actions_used[5], 1);
    engine_advance(&gs, t_autumn() + 86400);
    CHECK_EQ(gs.actions_used[5], 0);
    CHECK_EQ(pet_care(&gs, 5, msg, sizeof(msg)), CARE_OK);
}

static void test_pet_likes_and_offering(void)
{
    /* Al hongo le encanta la humedad (Nube); la lombriz le da igual. */
    CHECK_EQ(pet_like(PET_HONGO, ITEM_NUBE), 2);
    CHECK_EQ(pet_like(PET_HONGO, ITEM_LOMBRIZ), 0);
    CHECK_EQ(pet_like(PET_COMPOSTERA, ITEM_LOMBRIZ), 2);
    CHECK(pet_like(PET_SEMILLA, ITEM_ALPISTE) < 0);
    int hap[2];
    for (int round = 0; round < 2; round++) {
        GameState gs;
        state_init(&gs, 4, t_autumn());
        pet_choose(&gs, PET_HONGO);
        gs.pet.happy = 50;
        gs.bag_count = 0;
        memset(gs.bag, 0, sizeof(gs.bag));
        state_bag_add(&gs, ITEM_SEMILLA);
        state_bag_add(&gs, round == 0 ? ITEM_NUBE : ITEM_LOMBRIZ);
        gs.prepared = round == 0 ? ITEM_NUBE : ITEM_LOMBRIZ;
        char msg[96];
        CHECK_EQ(pet_care(&gs, 5, msg, sizeof(msg)), CARE_OK);
        /* El objeto ofrecido se gasta y el preparado se limpia. */
        CHECK_EQ(gs.bag_count, 1);
        CHECK_EQ(gs.bag[0], ITEM_SEMILLA);
        CHECK_EQ(gs.prepared, ITEM_NONE);
        CHECK(has(msg, round == 0 ? "encanta" : "igual"));
        hap[round] = gs.pet.happy;
    }
    CHECK(hap[0] > hap[1]);
    /* Un preparado que ya no está en la bolsa no se inventa: se saluda sin gastar nada. */
    GameState gs;
    state_init(&gs, 4, t_autumn());
    pet_choose(&gs, PET_PIEDRA);
    gs.prepared = ITEM_DESEO;
    int bag = gs.bag_count;
    char msg[96];
    CHECK_EQ(pet_care(&gs, 5, msg, sizeof(msg)), CARE_OK);
    CHECK_EQ(gs.bag_count, bag);
    CHECK_EQ(gs.prepared, ITEM_NONE);
}

static void test_pet_rarities(void)
{
    /* Hongo: una palabra a la semana, ni una más. */
    GameState gs;
    state_init(&gs, 8, t_autumn());
    pet_choose(&gs, PET_HONGO);
    int64_t t = t_autumn();
    EvCount ev = {0};
    for (int d = 0; d < 28; d++) {
        t += 86400;
        engine_advance(&gs, t);
        ev_update(&ev, &gs);
    }
    CHECK_EQ(ev.n[EV_PET_WORD], 4);
    /* Sus esporas hacen nacer hongos en el Jardín cuando es adulto (conexión entre mundos). */
    state_init(&gs, 9, t_autumn());
    pet_choose(&gs, PET_HONGO);
    gs.pet.age = 5 * LLV_PET_CYCLE_TICKS / 6 + 10; /* anciano */
    ent_clear(&gs);
    t = t_autumn();
    EvCount sp = {0};
    for (int d = 0; d < 20; d++) {
        t += 86400;
        engine_advance(&gs, t);
        ev_update(&sp, &gs);
    }
    CHECK(sp.n[EV_PET_SPORES] >= 2);
    CHECK(gs.collection & (1u << COLL_HONGO));
    /* Un hongo pequeño todavía no esporula. */
    state_init(&gs, 9, t_autumn());
    pet_choose(&gs, PET_HONGO);
    t = t_autumn();
    EvCount young = {0};
    for (int d = 0; d < 10; d++) {
        t += 86400;
        engine_advance(&gs, t);
        ev_update(&young, &gs);
    }
    CHECK_EQ(young.n[EV_PET_SPORES], 0);
    /* La semilla regala Semillas a veces (35 %). */
    int gifts = 0;
    for (int i = 0; i < 300; i++) {
        state_init(&gs, 1000 + (uint64_t)i, t_autumn());
        pet_choose(&gs, PET_SEMILLA);
        gs.bag_count = 0;
        memset(gs.bag, 0, sizeof(gs.bag));
        char msg[96];
        pet_care(&gs, 5, msg, sizeof(msg));
        gifts += gs.bag_count;
    }
    CHECK(gifts >= 70 && gifts <= 140);
    /* La compostera se come las manzanas podridas y da una lombriz para la bolsa. */
    state_init(&gs, 12, t_autumn());
    pet_choose(&gs, PET_COMPOSTERA);
    ent_clear(&gs);
    gs.bag_count = 0;
    memset(gs.bag, 0, sizeof(gs.bag));
    gs.pet.task_left = 3;
    ent_put(&gs, SP_MANZANA, 1, 12, 5000);
    for (int k = 0; k < 6; k++)
        pet_tick(&gs);
    CHECK_EQ(garden_count_sp(&gs, SP_MANZANA, ST_ANY), 0);
    CHECK_EQ(gs.bag_count, 1);
    CHECK_EQ(gs.bag[0], ITEM_LOMBRIZ);
    CHECK_EQ(count_ev(&gs, EV_PET_COMPOST), 1);
}

/* ---------- guardado de la fase 2 ---------- */

static void test_save_roundtrip_phase2(void)
{
    GameState gs, back;
    state_init(&gs, 0xC0FFEE, t_autumn());
    pet_choose(&gs, PET_SEMILLA);
    Rng r;
    rng_seed(&r, 9, 9);
    int64_t t = t_autumn();
    for (int d = 0; d < 9; d++) {
        fill_bag(&gs, &r);
        gs.actions_used[1] = 0;
        garden_act(&gs, 1, NULL, NULL, NULL);
        t += 86400 / 2;
        engine_advance(&gs, t);
    }
    gs.prepared = gs.bag_count ? gs.bag[0] : ITEM_NONE;
    gs.weather = W_RAIN;
    gs.weather_left = 33;
    gs.sky = SKY_STAR;
    gs.sky_left = 4;
    gs.collection = 0x8A55;
    gs.pet.happy = 201;
    gs.pet.thought = 3;
    gs.pet.word_left = 777;
    gs.pet.task_left = 66;
    CHECK(garden_count(&gs) > 4);
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = ser(&gs, buf);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK(same_state(&gs, &back));
    CHECK(memcmp(gs.ents, back.ents, sizeof(gs.ents)) == 0);
    CHECK(back.weather == W_RAIN && back.weather_left == 33 && back.sky == SKY_STAR && back.sky_left == 4);
    CHECK_EQ(back.collection, 0x8A55);
    CHECK(back.pet.kind == PET_SEMILLA && back.pet.happy == 201 && back.pet.thought == 3);
    CHECK(back.pet.word_left == 777 && back.pet.task_left == 66 && back.pet.age == gs.pet.age);
    CHECK_EQ(back.diary_total, gs.diary_total);
    CHECK_EQ(back.prepared, gs.prepared);
    /* El cargado sigue la misma vida que el original. */
    engine_advance(&gs, t + 5 * 86400);
    engine_advance(&back, t + 5 * 86400);
    CHECK(same_state(&gs, &back));
}

static void put_le(uint8_t *buf, size_t *pos, uint64_t v, int n)
{
    for (int i = 0; i < n; i++)
        buf[(*pos)++] = (uint8_t)(v >> (8 * i));
}

/* Guardado v1, tal y como lo escribía la fase 0 (sin jardín, colección ni mascota). */
static size_t build_v1_save(uint8_t *buf)
{
    size_t p = 0;
    put_le(buf, &p, 0x31564C4Cu, 4);
    put_le(buf, &p, 1, 2);
    put_le(buf, &p, 0, 2);
    put_le(buf, &p, 1234, 8);  /* seed */
    put_le(buf, &p, 55, 8);    /* rng.state */
    put_le(buf, &p, 77, 8);    /* rng.inc */
    put_le(buf, &p, t_autumn() / 600, 8);
    put_le(buf, &p, 1000, 8);  /* day_key */
    put_le(buf, &p, t_autumn(), 8);
    put_le(buf, &p, 2, 1);     /* bag_count */
    put_le(buf, &p, 0, 1);     /* prepared */
    put_le(buf, &p, 30, 2);    /* refill_left */
    put_le(buf, &p, 0, 1);     /* current_world */
    put_le(buf, &p, 0, 1);     /* reloj_style */
    put_le(buf, &p, 1, 4);
    put_le(buf, &p, 1, 4);
    put_le(buf, &p, 0, 1);     /* diary_head */
    put_le(buf, &p, 0, 1);     /* diary_count */
    for (int i = 0; i < 5; i++)
        put_le(buf, &p, i < 2 ? i + 1 : 0, 1);
    for (int i = 0; i < 6; i++)
        put_le(buf, &p, 0, 1);
    for (int i = 0; i < 64; i++) {
        put_le(buf, &p, 0, 8);
        put_le(buf, &p, 0, 2);
    }
    size_t payload = p - 8;
    buf[6] = (uint8_t)payload;
    buf[7] = (uint8_t)(payload >> 8);
    put_le(buf, &p, state_crc32(buf, p), 4);
    return p;
}

static void test_save_rejects_old_version(void)
{
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = build_v1_save(buf);
    CHECK(n > 700 && n < LLV_SAVE_MAX);
    GameState out;
    state_init(&out, 5, t_autumn());
    out.looks_total = 4242;
    CHECK(state_deserialize(&out, buf, n) != 0); /* se rechaza sin romperse... */
    CHECK_EQ(out.looks_total, 4242);             /* ...y no toca el destino */
    /* Un v2 con la versión cambiada a 1 tampoco entra. */
    GameState gs;
    state_init(&gs, 5, t_autumn());
    uint8_t cur[LLV_SAVE_MAX];
    size_t m = ser(&gs, cur);
    CHECK_EQ(state_deserialize(&out, cur, m), 0);
    cur[4] = 1;
    fix_crc(cur, m);
    CHECK(state_deserialize(&out, cur, m) != 0);
    /* Tras rechazar el viejo, el arranque normal crea un universo nuevo y funciona. */
    static Engine e;
    GameState loaded;
    if (state_deserialize(&loaded, buf, n) != 0)
        engine_init(&e, 9, t_autumn(), 1000);
    CHECK(!e.asleep && garden_ok(&e.gs));
}

static void test_save_rejects_bad_garden(void)
{
    GameState gs, out;
    state_init(&gs, 5, t_autumn());
    uint8_t buf[LLV_SAVE_MAX];
    size_t n;
    /* Dos entidades en la misma casilla. */
    GameState evil = gs;
    ent_clear(&evil);
    ent_put(&evil, SP_FLOR, 1, 5, 10);
    ent_put(&evil, SP_FLOR, 1, 5, 10);
    n = ser(&evil, buf);
    CHECK(state_deserialize(&out, buf, n) != 0);
    /* Especie, etapa y casilla fuera de rango. */
    evil = gs;
    evil.ents[0].sp = SP_COUNT;
    n = ser(&evil, buf);
    CHECK(state_deserialize(&out, buf, n) != 0);
    evil = gs;
    evil.ents[0].st = GARDEN_STAGES;
    n = ser(&evil, buf);
    CHECK(state_deserialize(&out, buf, n) != 0);
    evil = gs;
    evil.ents[0].cell = LLV_GARDEN_CELLS;
    n = ser(&evil, buf);
    CHECK(state_deserialize(&out, buf, n) != 0);
    /* Mascota inválida. */
    evil = gs;
    evil.pet.kind = PET_COUNT;
    n = ser(&evil, buf);
    CHECK(state_deserialize(&out, buf, n) != 0);
    evil = gs;
    evil.weather = 9;
    n = ser(&evil, buf);
    CHECK(state_deserialize(&out, buf, n) != 0);
    evil = gs;
    evil.prepared = ITEM_COUNT;
    n = ser(&evil, buf);
    CHECK(state_deserialize(&out, buf, n) != 0);
}

/* ---------- el Cobertizo y el Jardín por gestos ---------- */

static void test_engine_cobertizo_flow(void)
{
    static Engine e;
    uint32_t ms = 1000;
    cobertizo_ui_reset(); /* otros tests pueden haber dejado abierto el menú de ajustes */
    int64_t now = time_from_civil(2026, 10, 22, 12, 0, 0);
    engine_init(&e, 4, now, ms);
    RawInput none = {0, false};
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = 5;
    CHECK_EQ(e.gs.pet.kind, PET_NONE);
    /* Primera vez: toque cicla, mantener confirma. */
    gesture(&e, BTN_A, now, &ms, true, 80);
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK_EQ(e.gs.pet.kind, PET_NONE);
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK_EQ(e.gs.pet.kind, PET_PIEDRA); /* hongo -> semilla -> piedra */
    CHECK(has(e.toast, "PIEDRA"));
    CHECK(cobertizo_on_pet(&e.gs));
    /* Cuidarla: una vez al día. */
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK_EQ(e.gs.actions_used[5], 1);
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK(has(e.toast, "Mañana"));
    CHECK_EQ(e.gs.actions_used[5], 1);
    /* Respirar juntos: no hay límite y no toca el estado del universo. */
    GameState before = e.gs;
    gesture(&e, BTN_A, now, &ms, true, 80); /* empieza */
    CHECK(cobertizo_breathing());
    for (int k = 0; k < 6; k++) {
        gesture(&e, BTN_A, now, &ms, true, 3000); /* inhala (una pulsación larga) */
        CHECK(cobertizo_breathing());
    }
    CHECK(same_state(&before, &e.gs));
    gesture(&e, BTN_A, now, &ms, true, 80); /* sale */
    CHECK(!cobertizo_breathing());
    CHECK(!cobertizo_on_pet(&e.gs));
    /* Preparar el objeto bajo el cursor (bolsa) y que el Jardín lo use. */
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    state_bag_add(&e.gs, ITEM_NUBE);
    state_bag_add(&e.gs, ITEM_ALPISTE);
    state_bag_add(&e.gs, ITEM_SEMILLA);
    cobertizo_ui_reset();
    gesture(&e, BTN_A, now, &ms, true, 80); /* cursor al Alpiste */
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK_EQ(e.gs.prepared, ITEM_ALPISTE);
    CHECK(has(e.toast, "Alpiste"));
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100); /* otra vez: lo quita */
    CHECK_EQ(e.gs.prepared, ITEM_NONE);
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK_EQ(e.gs.prepared, ITEM_ALPISTE);
    gesture(&e, BTN_B, now, &ms, true, 80); /* a las 6 en punto: Reloj */
    CHECK_EQ(e.gs.current_world, 0);
    gesture(&e, BTN_B, now, &ms, true, 80);
    CHECK_EQ(e.gs.current_world, 1);
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK(has(e.toast, "Preparado: Alpiste"));
    CHECK_EQ(e.gs.prepared, ITEM_NONE);
    CHECK_EQ(e.gs.bag_count, 2);
    CHECK_EQ(garden_count_sp(&e.gs, SP_PAJARO, 1), 1);
    CHECK(garden_ok(&e.gs));
}

static void test_engine_lupa(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = time_from_civil(2026, 10, 22, 12, 0, 0);
    jardin_ui_reset();
    engine_init(&e, 4, now, ms);
    RawInput none = {0, false};
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = 1;
    ent_clear(&e.gs);
    ent_put(&e.gs, SP_MANZANA, 1, 8, 30);
    ent_put(&e.gs, SP_RANA, 0, 20, 30);
    GameState before = e.gs;
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100); /* lupa: sin coste */
    CHECK_EQ(e.gs.actions_used[1], 0);
    CHECK_EQ(e.gs.bag_count, before.bag_count);
    CHECK(memcmp(e.gs.ents, before.ents, sizeof(before.ents)) == 0);
    CHECK(e.gs.collection & (1u << COLL_MANZANA)); /* mirar de cerca descubre */
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK(e.gs.collection & (1u << COLL_RANA));
    /* Jardín vacío: lo dice con gracia. */
    ent_clear(&e.gs);
    ms += 6000;
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK(has(e.toast, "nada que mirar"));
}

static void test_render_all_worlds(void)
{
    /* Todos los mundos se pintan sin salirse del framebuffer en cualquier estado, estación
     * y hora (el AddressSanitizer vigila los accesos). */
    static Engine e;
    uint32_t ms = 1000;
    RawInput none = {0, false};
    for (int season = 0; season < 4; season++)
        for (int hour = 0; hour < 24; hour += 3) {
            int64_t now = time_from_civil(2026, 1 + season * 3, 20, hour, 15, 0);
            engine_init(&e, 40 + (uint64_t)season, now, ms);
            for (int pk = PET_NONE; pk < PET_COUNT; pk++) {
                if (pk != PET_NONE && e.gs.pet.kind == PET_NONE)
                    pet_choose(&e.gs, (PetKind)pk);
                else if (pk != PET_NONE) {
                    e.gs.pet.kind = (uint8_t)pk;
                    e.gs.pet.age = (uint16_t)(pk * 3000);
                }
                for (int w = 0; w < LLV_WORLD_COUNT; w++) {
                    e.gs.current_world = (uint8_t)w;
                    e.gs.weather = (uint8_t)(w % 4);
                    e.gs.sky = (uint8_t)(w % 3);
                    FrameOut o = engine_frame(&e, none, now, ms += 100);
                    CHECK(o.screen_on);
                }
            }
        }
}


/* ======================= fase 3: la Sala, el Director y los hábitos ======================= */

static PhraseEnv penv(int64_t now)
{
    PhraseEnv e = {now, false, 0, false};
    return e;
}

/* 08:00 del 22 de octubre de 2026 (jueves), con el universo al día. */
static int64_t t_morning(void) { return t_autumn() + 3 * 3600; }

static int cur_kind(const GameState *gs)
{
    PhraseRec r;
    return gs->sala.phrase && phrase_get(gs->sala.phrase - 1, &r) ? r.kind : -1;
}

static int cur_has_tag(const GameState *gs, int kind, int arg)
{
    PhraseRec r;
    if (!gs->sala.phrase || !phrase_get(gs->sala.phrase - 1, &r))
        return 0;
    for (int i = 0; i < r.ntags; i++)
        if (phrase_tag_kind(&r, i) == kind && (arg < 0 || phrase_tag_arg(&r, i) == arg))
            return 1;
    return 0;
}

static int cur_picaro(const GameState *gs)
{
    PhraseRec r;
    return gs->sala.phrase && phrase_get(gs->sala.phrase - 1, &r) && r.picaro;
}

/* Termina lo que haya pendiente (pregunta, escena) sin gastar conversaciones. */
static void settle(GameState *gs, const PhraseEnv *env, bool yes)
{
    for (int g = 0; g < 10; g++) {
        int used = gs->actions_used[WORLD_SALA];
        if (gs->sala.stage == DLG_ASK) {
            if (yes)
                talk_tap(gs, env);
            else
                talk_hold(gs, env);
        } else if (gs->sala.stage == DLG_SAY && gs->sala.scene > 0) {
            talk_tap(gs, env);
        } else {
            break;
        }
        CHECK_EQ(gs->actions_used[WORLD_SALA], used); /* responder y seguir una escena es gratis */
    }
}

static int drawable(uint32_t cp)
{
    return cp == '?' || font_glyph(cp) != font_glyph('?');
}

static void test_phrase_bank_valid(void)
{
    CHECK_EQ(phrase_bank_check(), 0);
    int n = phrase_count();
    CHECK(n >= 500);
    int per[SPK_COUNT] = {0}, picaro = 0;
    static GameState gs;
    state_init(&gs, 1, t_morning());
    engine_advance(&gs, t_morning());
    gs.gap_min = 45 * 1440;       /* el peor caso de {ausencia}: "45 días" */
    gs.looks_today = 255;
    gs.streak = 255;
    gs.hab.steps_today = 65535;   /* "65.535" */
    gs.pet.kind = PET_COMPOSTERA; /* "compostera" */
    int max_lines = 0;
    for (int i = 0; i < n; i++) {
        PhraseRec r;
        CHECK(phrase_get(i, &r));
        CHECK(r.speaker > SPK_NONE && r.speaker < SPK_COUNT && r.kind < PK_COUNT);
        per[r.speaker]++;
        picaro += r.picaro;
        char raw[256];
        phrase_text(&r, raw, sizeof(raw));
        for (const char *p = raw; *p;) {
            uint32_t cp = font_utf8_next(&p);
            if (!drawable(cp))
                printf("    carácter que la fuente no dibuja (U+%04X) en: %s\n", cp, raw);
            CHECK(drawable(cp));
        }
        /* etiquetas: tipos conocidos y argumentos dentro de rango */
        int event_tag = -1;
        for (int t = 0; t < r.ntags; t++) {
            int k = phrase_tag_kind(&r, t), a = phrase_tag_arg(&r, t);
            CHECK(k > 0 && k < PT_COUNT);
            switch (k) {
            case PT_TOD: CHECK(a <= TOD_NOCHE); break;
            case PT_WDAY: CHECK(a <= 6); break;
            case PT_SEASON: CHECK(a <= 3); break;
            case PT_DATE: CHECK(a >= DATE_HALLOWEEN && a <= DATE_CUMPLE); break;
            case PT_EVENT: CHECK(a < PE_COUNT); event_tag = a; break;
            case PT_RESP: CHECK((a & 255) < Q_COUNT && (a >> 8) >= 1 && (a >> 8) <= 2); break;
            case PT_HABIT: CHECK(a < HAB_COUNT); break;
            case PT_PET: CHECK(a <= PET_COMPOSTERA); break;
            case PT_WEATHER: CHECK(a <= W_SNOW); break;
            default: break;
            }
        }
        if (r.kind == PK_COMENTARIO)
            CHECK(event_tag >= 0);
        if (r.kind == PK_PREGUNTA || r.kind == PK_SI || r.kind == PK_NO || r.kind == PK_RETOMA)
            CHECK(r.karg < Q_COUNT);
        /* huecos: se rellenan, no queda ninguno y la frase cabe en la caja de texto */
        int too_long = 0;
        for (int variant = 0; variant < 3; variant++)
            for (int arg = 0; arg < 16; arg++) {
                PhraseFill f = {{(uint16_t)variant, (uint16_t)arg, 23 * 60 + 59}};
                char out[256];
                phrase_render(&gs, i, &f, out, sizeof(out));
                CHECK(out[0] != '\0' && !strchr(out, '{') && !strchr(out, '}'));
                int lines = gfx_text_wrap_n(NULL, 0, 0, PHRASE_BOX_W, out, C_DARK, -1, NULL);
                if (lines > max_lines)
                    max_lines = lines;
                int limit = r.kind == PK_PREGUNTA ? PHRASE_ASK_LINES : PHRASE_BOX_LINES;
                if (lines > limit && !too_long) {
                    printf("    no cabe en la caja (%d líneas): %s\n", lines, out);
                    too_long = 1;
                }
            }
        CHECK(!too_long);
    }
    /* contenido mínimo de esta fase (la meta final llega escribiendo más) */
    CHECK(per[SPK_ABUELA] >= 60 && per[SPK_ROBOT] >= 60);
    CHECK(per[SPK_GOTICA] >= 15 && per[SPK_GAMER] >= 15 && per[SPK_BOOMER] >= 15 && per[SPK_GATO] >= 15);
    CHECK(per[SPK_SISTEMA] >= 10);
    CHECK(picaro * 100 / n >= 15 && picaro * 100 / n <= 40); /* ~70 % blanco, ~30 % pícaro (spec §5.3) */
    printf("    (%d frases: Abuela %d, Robot %d, Gótica %d, Gamer %d, Boomer %d, Gato %d, Sistema %d; %d %% pícaras; máx. %d líneas)\n",
           n, per[SPK_ABUELA], per[SPK_ROBOT], per[SPK_GOTICA], per[SPK_GAMER], per[SPK_BOOMER], per[SPK_GATO],
           per[SPK_SISTEMA], picaro * 100 / n, max_lines);
    /* El cumpleaños es un gancho para la fase 7: ninguna frase lo usa todavía. */
    for (int i = 0; i < n; i++) {
        PhraseRec r;
        phrase_get(i, &r);
        for (int t = 0; t < r.ntags; t++)
            CHECK(!(phrase_tag_kind(&r, t) == PT_DATE && phrase_tag_arg(&r, t) == DATE_CUMPLE));
    }
}

static void test_phrase_questions_complete(void)
{
    /* Cada pregunta tiene su pregunta, y quien pregunta tiene su sí y su no; y todo eso con
     * versión blanca para el modo apto-abuela. */
    int n = phrase_count();
    for (int q = 0; q < Q_COUNT; q++) {
        int asked = 0;
        for (int i = 0; i < n; i++) {
            PhraseRec r;
            phrase_get(i, &r);
            if (r.kind != PK_PREGUNTA || r.karg != q)
                continue;
            asked++;
            for (int k = 0; k < 2; k++) {
                int have = 0, white = 0;
                for (int j = 0; j < n; j++) {
                    PhraseRec o;
                    phrase_get(j, &o);
                    if (o.kind == (k ? PK_NO : PK_SI) && o.karg == q && o.speaker == r.speaker) {
                        have++;
                        white += !o.picaro;
                    }
                }
                CHECK(have > 0 && white > 0);
            }
        }
        CHECK(asked > 0);
    }
    /* Los grupos que el motor necesita siempre tienen alguna frase blanca. */
    static const int KINDS[] = {PK_RELLENO, PK_SINACCIONES, PK_TIMBRE, PK_INVASION, PK_REBELDIA, PK_GLITCH,
                                PK_GLITCH_OK, PK_CARTEL, PK_MONEDA};
    for (unsigned k = 0; k < sizeof(KINDS) / sizeof(KINDS[0]); k++) {
        int white = 0;
        for (int i = 0; i < n; i++) {
            PhraseRec r;
            phrase_get(i, &r);
            white += r.kind == KINDS[k] && !r.picaro;
        }
        CHECK(white > 0);
    }
    /* Las escenas: líneas 1..N seguidas, dichas por personajes distintos entre líneas contiguas. */
    for (int sc = 1; sc < 40; sc++) {
        int lines = 0;
        while (phrase_scene_find(sc, lines + 1) >= 0)
            lines++;
        if (lines == 0)
            continue;
        CHECK(lines >= 2);
        CHECK(phrase_scene_find(sc, lines + 1) < 0);
        for (int l = 1; l < lines; l++) {
            PhraseRec a, b;
            phrase_get(phrase_scene_find(sc, l), &a);
            phrase_get(phrase_scene_find(sc, l + 1), &b);
            CHECK(a.speaker != b.speaker);
        }
    }
}

/* Un oráculo independiente de las condiciones (escrito aparte de phrases.c). */
static int oracle_ok(const PhraseRec *r, const PhraseCtx *c)
{
    for (int i = 0; i < r->ntags; i++) {
        int k = phrase_tag_kind(r, i), a = phrase_tag_arg(r, i), ok = 0;
        int h = c->hour;
        switch (k) {
        case PT_TOD:
            ok = a == TOD_MADRUGADA ? h < 5 : a == TOD_MANANA ? (h >= 5 && h < 12) : a == TOD_MEDIODIA ? (h >= 12 && h < 15)
               : a == TOD_TARDE ? (h >= 15 && h < 21) : (h >= 21 || h < 5);
            break;
        case PT_WDAY: ok = c->wday == a; break;
        case PT_WEEKEND: ok = c->wday == 0 || c->wday == 6; break;
        case PT_SEASON: ok = c->season == a; break;
        case PT_MOON: ok = c->moon == a; break;
        case PT_DATE: ok = c->date == a; break;
        case PT_ABS_GT: ok = c->gap_min > a; break;
        case PT_ABS_LT: ok = c->gap_min < a; break;
        case PT_LOOKS_GT: ok = c->looks > a; break;
        case PT_LOOKS_LT: ok = c->looks < a; break;
        case PT_STREAK_GT: ok = c->streak > a; break;
        case PT_AFF: ok = a ? c->aff[r->speaker] >= LLV_AFF_HIGH : c->aff[r->speaker] <= LLV_AFF_LOW; break;
        case PT_RESP: ok = c->resp[a & 255] == (a >> 8); break;
        case PT_SHAKEN: ok = c->shaken; break;
        case PT_BATT: ok = c->batt_low; break;
        case PT_CHARGING: ok = c->charging; break;
        case PT_EVENT: ok = (c->fresh >> phrase_event_kind(a)) & 1; break;
        case PT_WEATHER: ok = c->weather == a; break;
        case PT_GARDEN: ok = a ? c->garden_n >= 20 : c->garden_n <= 4; break;
        case PT_PET: ok = c->pet == a; break;
        case PT_COLL_GT: ok = c->coll_n > a; break;
        case PT_BAG: ok = a ? c->bag_n >= LLV_BAG_SIZE : c->bag_n == 0; break;
        case PT_HABIT: ok = (c->habits >> a) & 1; break;
        default: ok = 0; break;
        }
        if (!ok)
            return 0;
    }
    return 1;
}

static void test_phrase_selection_context(void)
{
    static GameState gs;
    state_init(&gs, 77, t_morning());
    Rng r;
    rng_seed(&r, 1234, 5);
    int picked = 0, by_kind[PK_COUNT] = {0};
    static const uint32_t QUERIES[] = {
        PK_BIT(PK_CHARLA), PK_BIT(PK_COMENTARIO), PK_BIT(PK_RELLENO) | PK_BIT(PK_SINACCIONES),
        PK_BIT(PK_CUARTA_PARED), PK_BIT(PK_TIMBRE), PK_BIT(PK_REBELDIA), PK_BIT(PK_ESCENA),
    };
    for (int iter = 0; iter < 4000; iter++) {
        PhraseCtx c;
        memset(&c, 0, sizeof(c));
        c.hour = (int)rng_below(&r, 24);
        c.minute = (int)rng_below(&r, 60);
        c.wday = (int)rng_below(&r, 7);
        c.season = (int)rng_below(&r, 4);
        c.moon = (int)rng_below(&r, 8);
        c.date = rng_below(&r, 4) == 0 ? (uint8_t)(1 + rng_below(&r, 4)) : 0;
        static const int GAPS[] = {0, 5, 30, 200, 1500, 6000, 70000};
        c.gap_min = GAPS[rng_below(&r, 7)];
        c.looks = (int)rng_below(&r, 30);
        c.streak = (int)rng_below(&r, 12);
        for (int i = 0; i < SPK_COUNT; i++)
            c.aff[i] = (uint8_t)rng_below(&r, 256);
        c.shaken = rng_below(&r, 2);
        c.batt_low = rng_below(&r, 3) == 0;
        c.charging = rng_below(&r, 4) == 0;
        c.weather = (uint8_t)rng_below(&r, 4);
        c.pet = (uint8_t)rng_below(&r, PET_COUNT);
        c.garden_n = (int)rng_below(&r, 33);
        c.coll_n = (int)rng_below(&r, 17);
        c.bag_n = (int)rng_below(&r, 6);
        c.habits = (uint8_t)rng_below(&r, 128);
        for (int i = 0; i < Q_COUNT; i++)
            c.resp[i] = (uint8_t)rng_below(&r, 3);
        c.fresh = ((uint64_t)rng_next(&r) << 32) | rng_next(&r);
        c.present = SPK_BIT(SPK_ABUELA) | SPK_BIT(SPK_ROBOT);
        if (rng_below(&r, 2))
            c.present |= SPK_BIT(SPK_GOTICA + (int)rng_below(&r, 4));
        c.grandma = rng_below(&r, 3) == 0;
        for (unsigned q = 0; q < sizeof(QUERIES) / sizeof(QUERIES[0]); q++) {
            PhraseQuery pq;
            memset(&pq, 0, sizeof(pq));
            pq.kinds = QUERIES[q];
            pq.speakers = c.present;
            pq.karg = -1;
            pq.remember = rng_below(&r, 2);
            if (pq.kinds == PK_BIT(PK_ESCENA)) {
                pq.scene_line = 1;
                pq.scene_start = true;
            }
            int idx = phrase_pick(&c, &gs, &pq);
            if (idx < 0)
                continue;
            PhraseRec rec;
            CHECK(phrase_get(idx, &rec));
            picked++;
            by_kind[rec.kind]++;
            CHECK(oracle_ok(&rec, &c));                       /* ninguna frase fuera de su contexto */
            CHECK(pq.kinds & PK_BIT(rec.kind));
            CHECK(c.present & SPK_BIT(rec.speaker));          /* habla quien está en la sala */
            CHECK(!(c.grandma && rec.picaro));                /* el modo abuela no deja pasar la picardía */
            if (rec.kind == PK_ESCENA)
                CHECK(!(phrase_scene_speakers(rec.karg) & ~c.present));
        }
    }
    CHECK(picked > 20000);
    CHECK(by_kind[PK_CHARLA] > 3000 && by_kind[PK_COMENTARIO] > 500 && by_kind[PK_ESCENA] > 1000);
}

static void test_phrase_antirepeat(void)
{
    static GameState gs;
    state_init(&gs, 5, t_morning());
    PhraseEnv env = penv(t_morning());
    PhraseCtx c;
    phrase_ctx_make(&c, &gs, &env);
    PhraseQuery q;
    memset(&q, 0, sizeof(q));
    q.kinds = PK_BIT(PK_CHARLA);
    q.speakers = c.present;
    q.karg = -1;
    q.remember = true;
    int pool = phrase_eligible(&c, &q);
    CHECK(pool > LLV_RECENT + 20); /* con banco de sobra... */
    int last[LLV_RECENT];
    for (int i = 0; i < LLV_RECENT; i++)
        last[i] = -1;
    int repeats = 0;
    for (int n = 0; n < 900; n++) {
        int idx = phrase_pick(&c, &gs, &q);
        CHECK(idx >= 0);
        for (int i = 0; i < LLV_RECENT; i++)
            repeats += last[i] == idx;
        last[n % LLV_RECENT] = idx;
    }
    CHECK_EQ(repeats, 0); /* ...nada se repite dentro de las últimas 30 */

    /* Con un banco corto no se bloquea: sale la que más tiempo lleva sin salir. */
    memset(&q, 0, sizeof(q));
    q.kinds = PK_BIT(PK_REBELDIA);
    q.speakers = SPK_BIT(SPK_ABUELA);
    q.karg = -1;
    q.remember = true;
    int prev = -1, pprev = -1;
    for (int n = 0; n < 8; n++) {
        int idx = phrase_pick(&c, &gs, &q);
        CHECK(idx >= 0);
        CHECK(idx != prev || phrase_eligible(&c, &q) == 1);
        if (n >= 2)
            CHECK(idx == pprev || phrase_eligible(&c, &q) != 2); /* con dos frases se alternan */
        pprev = prev;
        prev = idx;
    }
    /* El buffer es fijo: 30 hashes. */
    CHECK_EQ((int)(sizeof(gs.sala.recent) / sizeof(gs.sala.recent[0])), LLV_RECENT);
    CHECK(gs.sala.recent_head < LLV_RECENT);
}

static int find_phrase(const char *prefix, int speaker)
{
    int n = phrase_count();
    size_t l = strlen(prefix);
    for (int i = 0; i < n; i++) {
        PhraseRec r;
        phrase_get(i, &r);
        if (r.speaker == speaker && r.len >= l && memcmp(r.text, prefix, l) == 0)
            return i;
    }
    return -1;
}

static void test_phrase_fill_grammar(void)
{
    static GameState gs;
    state_init(&gs, 5, t_morning());
    engine_advance(&gs, t_morning());
    PhraseEnv env = penv(t_morning());
    PhraseCtx c;

    /* {ausencia}: "4 días sin verme" */
    gs.gap_min = 4 * 1440 + 60;
    int i = find_phrase("{ausencia} sin verme. He reflexionado", SPK_ROBOT);
    CHECK(i >= 0);
    PhraseFill f;
    phrase_ctx_make(&c, &gs, &env);
    phrase_fill_prepare(&gs, &c, i, &f, &gs.rng);
    char out[256];
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK_STR(out, "4 días sin verme. He reflexionado sobre la soledad. Conclusión: 404.");
    gs.gap_min = 3 * 60;
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK(has(out, "3 horas sin verme"));
    gs.gap_min = 1;
    i = find_phrase("Me miraste hace {ausencia}. No pasó nada", SPK_ROBOT);
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK(has(out, "hace 1 minuto."));

    /* {evento}: el suceso del diario, con mayúscula al principio y minúscula a media frase */
    state_log(&gs, EV_APPLE_ROT, 0);
    phrase_ctx_make(&c, &gs, &env);
    i = find_phrase("{evento}. Para el compost", SPK_ABUELA);
    CHECK(i >= 0);
    f = (PhraseFill){{0, 0, 600}};
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK_STR(out, "Se pudrió una manzana. Para el compost, como todo en la vida.");
    i = find_phrase("Ay, {evento}. No la tires", SPK_ABUELA);
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK(has(out, "Ay, se pudrió una manzana."));

    /* {hora}: la de la mirada, fija mientras se muestra */
    i = find_phrase("Son las {hora}. ¿Todo bien?", SPK_ROBOT);
    CHECK(i >= 0);
    f = (PhraseFill){{0, 0, 3 * 60 + 47}};
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK(has(out, "Son las 3:47."));

    /* {pasos}: con punto de millar */
    i = find_phrase("Caminaste {pasos} pasos", SPK_ROBOT);
    gs.hab.steps_today = 6000;
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK_STR(out, "Caminaste 6.000 pasos. Yo no tengo piernas. Estoy orgulloso y un poco celoso.");
    /* {miradas} y {racha} */
    gs.looks_today = 15;
    i = find_phrase("Me has mirado {miradas} veces hoy. Esto ya es obsesión. Pero", SPK_ROBOT);
    phrase_render(&gs, i, &f, out, sizeof(out));
    CHECK(has(out, "Me has mirado 15 veces hoy."));
    /* La frase "En mis tiempos" del Boomer y la del Gamer con la racha: están. */
    CHECK(find_phrase("En mis tiempos el agua salía del grifo", SPK_BOOMER) >= 0);
    CHECK(find_phrase("Racha de {racha} días. Ni en mis relaciones duro tanto.", SPK_GAMER) >= 0);
    /* Frases de la spec §5.2 con las etiquetas exactas. */
    i = find_phrase("A estas horas solo trabajan las lechuzas y los hongos.", SPK_ABUELA);
    PhraseRec rec;
    CHECK(i >= 0 && phrase_get(i, &rec) && rec.ntags == 1 && phrase_tag_kind(&rec, 0) == PT_TOD &&
          phrase_tag_arg(&rec, 0) == TOD_NOCHE);
    i = find_phrase("¿Puedes dejar de agitarme? Me mareo.", SPK_ROBOT);
    CHECK(i >= 0 && phrase_get(i, &rec) && phrase_tag_kind(&rec, 0) == PT_SHAKEN);
    i = find_phrase("Esta noche no me esperes despierta.", SPK_GOTICA);
    CHECK(i >= 0 && phrase_get(i, &rec) && phrase_tag_kind(&rec, 0) == PT_MOON && phrase_tag_arg(&rec, 0) == 4);
}

static void test_phrase_loader_robust(void)
{
    /* Un banco corrupto no rompe nada: se rechaza o se ignora. */
    uint8_t junk[64] = {'L', 'L', 'V', 'P', 1, 0, 200, 0};
    phrase_bank_set(junk, sizeof(junk));
    CHECK_EQ(phrase_count(), 0);
    CHECK(phrase_bank_check() != 0);
    PhraseRec r;
    CHECK(!phrase_get(0, &r));
    uint8_t one[64];
    memset(one, 0, sizeof(one));
    memcpy(one, "LLVP", 4);
    one[4] = 1;
    one[6] = 1;                /* una frase */
    one[8] = 12;               /* empieza en el byte 12 */
    one[12] = SPK_ABUELA;
    one[13] = PK_CHARLA;
    one[18] = 9;               /* longitud 9 */
    memcpy(one + 19, "Hola mundo", 9);
    phrase_bank_set(one, 28);
    CHECK_EQ(phrase_count(), 1);
    CHECK_EQ(phrase_bank_check(), 0);
    CHECK(phrase_get(0, &r) && r.len == 9);
    phrase_bank_set(one, 24); /* truncado: el texto no cabe */
    CHECK(!phrase_get(0, &r) && phrase_bank_check() != 0);
    phrase_bank_set(NULL, 0);
    CHECK_EQ(phrase_bank_check(), 0);
    CHECK(phrase_count() > 400);
}

static void test_sala_daily_limit(void)
{
    static GameState gs;
    state_init(&gs, 31, t_autumn());
    int64_t now = t_morning();
    engine_advance(&gs, now);
    PhraseEnv env = penv(now);
    CHECK_EQ(talk_tap(&gs, &env), TALK_STARTED);
    CHECK_EQ(gs.actions_used[WORLD_SALA], 1);
    settle(&gs, &env, true);
    CHECK_EQ(talk_tap(&gs, &env), TALK_STARTED);
    CHECK_EQ(gs.actions_used[WORLD_SALA], 2);
    settle(&gs, &env, false);
    /* Sin conversaciones: relleno con gracia, sin bloquearse y sin gastar nada. */
    for (int k = 0; k < 6; k++) {
        TalkResult t = (k & 1) ? talk_hold(&gs, &env) : talk_tap(&gs, &env);
        CHECK_EQ(t, TALK_FILLER);
        CHECK_EQ(gs.actions_used[WORLD_SALA], 2);
        CHECK(cur_kind(&gs) == PK_RELLENO || cur_kind(&gs) == PK_SINACCIONES);
        char txt[256];
        CHECK(talk_text(&gs, txt, sizeof(txt)) && txt[0]);
    }
    CHECK(gs.sala.visitor == 0 || gs.sala.visitor_left > 0);
    /* Reinicio a las 05:00, no a medianoche. */
    int64_t next = t_autumn() + 86400;
    engine_advance(&gs, next - 60);
    CHECK_EQ(gs.actions_used[WORLD_SALA], 2);
    now = next - 60;
    env = penv(now);
    CHECK_EQ(talk_tap(&gs, &env), TALK_FILLER);
    engine_advance(&gs, next);
    CHECK_EQ(gs.actions_used[WORLD_SALA], 0);
    env = penv(next + 3600);
    CHECK_EQ(talk_tap(&gs, &env), TALK_STARTED);
    CHECK_EQ(gs.actions_used[WORLD_SALA], 1);
    /* El timbre también cuenta como conversación y entra otro visitante. */
    settle(&gs, &env, true);
    int before = gs.sala.visitor;
    CHECK_EQ(talk_hold(&gs, &env), TALK_BELL);
    CHECK_EQ(gs.actions_used[WORLD_SALA], 2);
    CHECK(gs.sala.visitor >= SPK_GOTICA && gs.sala.visitor <= SPK_BOOMER && gs.sala.visitor != before);
    CHECK_EQ(cur_kind(&gs), PK_TIMBRE);
    CHECK_EQ(count_events(&gs, EV_VISITOR) > 0, 1);
    CHECK_EQ(talk_hold(&gs, &env), TALK_FILLER);
}

static void test_sala_questions_memory(void)
{
    static GameState gs;
    state_init(&gs, 8, t_autumn());
    int64_t now = t_morning();
    engine_advance(&gs, now);
    PhraseEnv env = penv(now);
    /* La pregunta de las plantas (spec §4.3): toque = sí, mantener = no. */
    CHECK(talk_say(&gs, &env, PK_BIT(PK_PREGUNTA), SPK_BIT(SPK_ABUELA), Q_PLANTAS));
    CHECK_EQ(gs.sala.stage, DLG_ASK);
    CHECK_EQ(gs.sala.q, Q_PLANTAS + 1);
    CHECK_EQ(gs.sala.who, SPK_ABUELA);
    int used = gs.actions_used[WORLD_SALA];
    CHECK_EQ(talk_hold(&gs, &env), TALK_ANSWERED); /* no */
    CHECK_EQ(gs.actions_used[WORLD_SALA], used);
    CHECK_EQ(gs.sala.memo[Q_PLANTAS] & 3, 2);
    CHECK_EQ(cur_kind(&gs), PK_NO);
    CHECK_EQ(gs.sala.who, SPK_ABUELA);
    CHECK_EQ(gs.sala.stage, DLG_SAY);
    CHECK_EQ(gs.sala.q, 0);
    CHECK_EQ(gs.sala.memo_day[Q_PLANTAS], (uint16_t)gs.day_key);
    /* La respuesta sobrevive al guardado. */
    uint8_t buf[LLV_SAVE_MAX];
    GameState back;
    size_t n = ser(&gs, buf);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK_EQ(back.sala.memo[Q_PLANTAS] & 3, 2);
    /* Tres días después las plantas vuelven a salir, con el recuerdo de lo que dijiste. */
    int64_t later = now + 3 * 86400;
    engine_advance(&gs, later);
    gs.sala.stage = DLG_IDLE;
    gs.sala.phrase = 0;
    env = penv(later);
    int retoma = 0, no_branch = 0;
    for (int k = 0; k < 40 && !retoma; k++) {
        GameState g2 = back;
        engine_advance(&g2, later);
        g2.rng.state += (uint64_t)k * 7919; /* otra tirada */
        CHECK(talk_tap(&g2, &env) == TALK_STARTED);
        if (cur_kind(&g2) == PK_RETOMA) {
            retoma = 1;
            CHECK_EQ(g2.sala.who, SPK_ABUELA);
            CHECK(g2.sala.memo[Q_PLANTAS] & 4); /* ya retomada */
            no_branch = cur_has_tag(&g2, PT_RESP, Q_PLANTAS | 2 << 8) || !cur_has_tag(&g2, PT_RESP, -1);
            CHECK(!cur_has_tag(&g2, PT_RESP, Q_PLANTAS | 1 << 8)); /* nada de "tan bien regadas" */
            /* y no se repite: la siguiente conversación ya es otra cosa */
            settle(&g2, &env, true);
            CHECK(talk_tap(&g2, &env) != TALK_NONE);
            CHECK(cur_kind(&g2) != PK_RETOMA || g2.sala.q == 0);
        }
    }
    CHECK(retoma);
    CHECK(no_branch);
    /* Antes de los tres días no se retoma. */
    GameState early = back;
    engine_advance(&early, now + 2 * 86400 + 3600);
    env = penv(now + 2 * 86400 + 3600);
    for (int k = 0; k < 20; k++) {
        GameState g3 = early;
        g3.rng.state += (uint64_t)k * 31;
        g3.actions_used[WORLD_SALA] = 0;
        g3.sala.stage = DLG_IDLE;
        talk_tap(&g3, &env);
        CHECK(cur_kind(&g3) != PK_RETOMA);
    }
}

static void test_sala_question_rules_60_days(void)
{
    /* 60 días de 2 conversaciones diarias, respondiendo lo que sea: un tope de hábitos al día,
     * nunca la misma pregunta dos veces el mismo día y las de siempre no se repiten en 20 días. */
    static GameState gs;
    state_init(&gs, 123, t_autumn());
    Rng r;
    rng_seed(&r, 9, 9);
    int64_t last_asked[Q_COUNT];
    for (int q = 0; q < Q_COUNT; q++)
        last_asked[q] = -10000;
    int total_q = 0, habit_q = 0, per_day_max = 0;
    int asked_count[Q_COUNT] = {0};
    for (int day = 0; day < 60; day++) {
        int habits_today = 0;
        for (int conv = 0; conv < 2; conv++) {
            int64_t now = t_autumn() + (int64_t)day * 86400 + (conv ? 15 : 7) * 3600 + (int64_t)rng_below(&r, 3600);
            engine_advance(&gs, now);
            PhraseEnv env = penv(now);
            gs.sala.stage = DLG_IDLE;
            talk_tap(&gs, &env);
            for (int g = 0; g < 10; g++) {
                if (gs.sala.stage == DLG_ASK) {
                    int q = gs.sala.q - 1;
                    asked_count[q]++;
                    total_q++;
                    if (q < HAB_WITH_QUESTION) {
                        habits_today++;
                        habit_q++;
                        CHECK(habit_enabled(&gs, q));
                        CHECK(day * 1 > last_asked[q] || last_asked[q] < 0 || (int64_t)day - last_asked[q] >= 1);
                        if (q == Q_LLAMAR)
                            CHECK((int64_t)day - last_asked[q] >= 7);
                    } else {
                        CHECK((int64_t)day - last_asked[q] >= 20); /* la misma pregunta, no antes de 20 días */
                    }
                    CHECK(last_asked[q] != day);
                    last_asked[q] = day;
                    if (rng_below(&r, 2))
                        talk_tap(&gs, &env);
                    else
                        talk_hold(&gs, &env);
                } else if (gs.sala.stage == DLG_SAY && gs.sala.scene > 0) {
                    talk_tap(&gs, &env);
                } else {
                    break;
                }
            }
            CHECK(gs.hab.asked_count <= LLV_HABIT_MAX_PER_DAY);
        }
        if (habits_today > per_day_max)
            per_day_max = habits_today;
        CHECK(habits_today <= LLV_HABIT_MAX_PER_DAY);
    }
    CHECK(habit_q >= 15 && total_q >= 30);
    CHECK_EQ(per_day_max, LLV_HABIT_MAX_PER_DAY);
    printf("    (60 días: %d preguntas, %d de hábitos; máx. %d hábitos en un día)\n", total_q, habit_q, per_day_max);
}

static void test_sala_scene_two_voices(void)
{
    static GameState gs;
    state_init(&gs, 4, t_autumn());
    int64_t now = t_morning();
    engine_advance(&gs, now);
    PhraseEnv env = penv(now);
    int found = 0;
    for (int k = 0; k < 400 && found < 3; k++) {
        gs.sala.stage = DLG_IDLE;
        gs.actions_used[WORLD_SALA] = 0;
        gs.rng.state += 104729u * (uint64_t)k;
        talk_tap(&gs, &env);
        if (cur_kind(&gs) != PK_ESCENA)
            continue;
        found++;
        int sc = gs.sala.scene_line, lines = 1, spk = gs.sala.who, used = gs.actions_used[WORLD_SALA];
        CHECK_EQ(sc, 1);
        while (gs.sala.scene > 0) { /* seguir la escena: gratis y cambiando de voz */
            CHECK_EQ(talk_tap(&gs, &env), TALK_ADVANCED);
            CHECK_EQ(gs.actions_used[WORLD_SALA], used);
            CHECK(gs.sala.who != spk);
            spk = gs.sala.who;
            lines++;
            CHECK_EQ(gs.sala.scene_line, lines);
            CHECK(lines <= 6);
        }
        CHECK(lines >= 2);
        CHECK(cur_kind(&gs) == PK_ESCENA);
        CHECK(!(phrase_scene_speakers(gs.sala.scene_line ? 1 : 1) == 0));
    }
    CHECK_EQ(found, 3);
}

static void test_sala_universe_comments(void)
{
    static GameState gs;
    state_init(&gs, 6, t_autumn());
    int64_t now = t_morning();
    engine_advance(&gs, now);
    PhraseEnv env = penv(now);
    /* Sin sucesos nuevos no hay comentario. */
    gs.sala.fresh_a = gs.sala.fresh_b = 0;
    CHECK(!talk_comment(&gs, &env));
    /* La manzana podrida, como en la spec: la Abuela o el Robot lo comentan, una vez. */
    state_log(&gs, EV_APPLE_ROT, 0);
    CHECK(state_fresh(&gs) >> EV_APPLE_ROT & 1);
    CHECK(gs.news & (1u << WORLD_JARDIN)); /* y el Jardín se marca con ❗ */
    int said = 0;
    for (int k = 0; k < 30 && !said; k++) {
        GameState g2 = gs;
        g2.rng.state += 977u * (uint64_t)k;
        if (talk_comment(&g2, &env) && cur_has_tag(&g2, PT_EVENT, PE_MANZANA_PODRIDA)) {
            said = 1;
            char txt[256];
            talk_text(&g2, txt, sizeof(txt));
            CHECK(has(txt, "manzana"));
            CHECK(!(state_fresh(&g2) >> EV_APPLE_ROT & 1)); /* ya no es noticia */
            CHECK(!talk_comment(&g2, &env));
        }
    }
    CHECK(said);
    /* Los sucesos viven hoy y mañana: a los dos días se olvidan. */
    engine_advance(&gs, now + 86400);
    CHECK(state_fresh(&gs) >> EV_APPLE_ROT & 1);
    engine_advance(&gs, now + 2 * 86400);
    CHECK(!(state_fresh(&gs) >> EV_APPLE_ROT & 1));
    /* Lluvia, rana, hongo, bolsa llena y demás tienen quién los comente. */
    static const int EVS[] = {EV_RAIN, EV_FROG, EV_MUSHROOM, EV_BAG_FULL_LOST, EV_RAINBOW, EV_STAR, EV_PET_STAGE, EV_VISITOR};
    for (unsigned e = 0; e < sizeof(EVS) / sizeof(EVS[0]); e++) {
        GameState g3;
        state_init(&g3, 6, t_autumn());
        engine_advance(&g3, now);
        g3.sala.fresh_a = g3.sala.fresh_b = 0;
        state_log(&g3, (EventKind)EVS[e], 0);
        CHECK(talk_comment(&g3, &env));
    }
}

static void test_sala_secret_cat_frequency(void)
{
    /* El Gato sale ~1 de cada 300 miradas con la tirada del Director. */
    static GameState gs;
    state_init(&gs, 11, t_autumn());
    int64_t now = t_autumn();
    int cats = 0, looks = 0;
    for (int i = 0; i < 60000; i++) {
        now += 3 * 3600;
        engine_advance(&gs, now);
        gs.sala.fresh_a = ~0ull;
        PhraseEnv env = penv(now);
        director_wake(&gs, &env, 3 * 3600, false);
        looks++;
        cats += gs.dir.cur == DS_GATO;
    }
    CHECK(cats * 600 > looks && cats * 150 < looks); /* entre 1/600 y 1/150 */
    printf("    (gato: %d de %d miradas = 1 de cada %d)\n", cats, looks, cats ? looks / cats : 0);
}

static void test_director_rules(void)
{
    static GameState gs;
    state_init(&gs, 21, t_autumn());
    int64_t now = t_autumn();
    int counts[DK_COUNT] = {0}, prev = DK_NORMAL, rolls = 0, worst_gap = 0, since = 0;
    int64_t last_surprise_day = gs.day_key;
    for (int i = 0; i < 40000; i++) {
        now += 600 + (int64_t)(i % 7) * 1800; /* de 10 min a 3 h entre miradas */
        engine_advance(&gs, now);
        gs.sala.fresh_a = ~0ull; /* siempre hay algo que comentar */
        int before = gs.dir.rolls;
        PhraseEnv env = penv(now);
        director_wake(&gs, &env, 600 + (int64_t)(i % 7) * 1800, false);
        CHECK_EQ(gs.dir.rolls, before + 1);
        rolls++;
        int k = gs.dir.last;
        counts[k]++;
        CHECK(!(prev >= DK_SURPRISE && k >= DK_SURPRISE)); /* nunca dos sorpresas seguidas */
        CHECK((k >= DK_SURPRISE) == (gs.dir.cur != DS_NONE));
        if (k >= DK_SURPRISE) {
            int gap = (int)(gs.day_key - last_surprise_day);
            if (gap > worst_gap)
                worst_gap = gap;
            last_surprise_day = gs.day_key;
            since = 0;
        } else {
            since++;
        }
        prev = k;
    }
    (void)since;
    /* Probabilidades 🧪: ~80 % normal, ~15 % comentario, ~4 % sorpresa, ~1 % muy raro. */
    CHECK(counts[DK_NORMAL] * 100 / rolls >= 74 && counts[DK_NORMAL] * 100 / rolls <= 86);
    CHECK(counts[DK_COMMENT] * 100 / rolls >= 11 && counts[DK_COMMENT] * 100 / rolls <= 18);
    CHECK(counts[DK_SURPRISE] * 100 / rolls >= 2 && counts[DK_SURPRISE] * 100 / rolls <= 6);
    CHECK(counts[DK_RARE] > 100 && counts[DK_RARE] * 100 / rolls <= 2);
    printf("    (%d tiradas: normal %d, comentario %d, sorpresa %d, rara %d)\n", rolls, counts[DK_NORMAL],
           counts[DK_COMMENT], counts[DK_SURPRISE], counts[DK_RARE]);
    CHECK(worst_gap <= 3);

    /* Seguro de pena: con 3 miradas al día (casi sin azar de sorpresa), nunca pasan más de 2 días
     * sin sorpresa, y no hay dos seguidas. */
    state_init(&gs, 77, t_autumn());
    now = t_autumn();
    prev = DK_NORMAL;
    last_surprise_day = gs.day_key;
    worst_gap = 0;
    int surprises = 0, days = 3000;
    for (int d = 0; d < days; d++)
        for (int l = 0; l < 3; l++) {
            now = t_autumn() + (int64_t)d * 86400 + (7 + 5 * l) * 3600;
            engine_advance(&gs, now);
            PhraseEnv env = penv(now);
            director_wake(&gs, &env, 5 * 3600, false);
            int k = gs.dir.last;
            CHECK(!(prev >= DK_SURPRISE && k >= DK_SURPRISE));
            if (k >= DK_SURPRISE) {
                surprises++;
                int gap = (int)(gs.day_key - last_surprise_day);
                if (gap > worst_gap)
                    worst_gap = gap;
                last_surprise_day = gs.day_key;
            }
            prev = k;
        }
    CHECK(worst_gap <= LLV_DIR_PENA_DAYS);
    CHECK(surprises > days / 3); /* al menos una cada ~2 días */
    printf("    (3 miradas al día, %d días: %d sorpresas, máx. %d días sin ninguna)\n", days, surprises, worst_gap);

    /* Las miradas seguidas (menos de 10 min) no tiran el dado ni dan nada nuevo. */
    GameState q = gs;
    int rolls_before = q.dir.rolls;
    PhraseEnv env = penv(now + 100);
    director_wake(&q, &env, 120, false);
    CHECK_EQ(q.dir.rolls, rolls_before);
    CHECK_EQ(q.dir.cur, DS_NONE);
    /* Y la primera mirada de la vida tampoco trae sorpresas. */
    GameState f;
    state_init(&f, 5, t_autumn());
    f.dir.last_surprise_day = (uint16_t)(f.day_key - 5);
    env = penv(t_autumn());
    director_wake(&f, &env, 0, true);
    CHECK_EQ(f.dir.cur, DS_NONE);
    CHECK_EQ(f.dir.rolls, 0);
}

static void test_director_opens_world_with_news(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_morning();
    engine_init(&e, 14, now, ms);
    pet_choose(&e.gs, PET_HONGO);
    CHECK_EQ(e.gs.current_world, WORLD_RELOJ); /* sin novedades se queda donde estaba */
    engine_sleep(&e, now);
    e.gs.dir.last = DK_NORMAL;
    e.gs.dir.last_surprise_day = (uint16_t)e.gs.day_key;
    e.gs.news = 0;
    RawInput shake = {0, true};
    /* Una novedad en el Cobertizo: al despertar abre el Cobertizo y marca los demás con ❗. */
    e.gs.news = (uint8_t)(1u << WORLD_COBERTIZO | 1u << WORLD_JARDIN);
    engine_frame(&e, shake, now + 4 * 3600, ms + 100000);
    int w = e.gs.current_world;
    CHECK(w == WORLD_COBERTIZO || w == WORLD_SALA || w == WORLD_JARDIN); /* el que tenga novedades (o una sorpresa) */
    CHECK_EQ(e.gs.news & (1u << w), 0);
    /* Una mirada rápida no cambia de mundo. */
    engine_sleep(&e, now + 4 * 3600 + 30);
    e.gs.current_world = WORLD_MUSICA;
    e.gs.news = 1u << WORLD_JARDIN;
    engine_frame(&e, shake, now + 4 * 3600 + 90, ms + 200000);
    CHECK_EQ(e.gs.current_world, WORLD_MUSICA);
    /* El orden de preferencia: sucesos raros del Jardín, Sala, Cobertizo, Jardín. */
    GameState g;
    state_init(&g, 1, t_autumn());
    g.news = 1u << WORLD_JARDIN | 1u << WORLD_COBERTIZO;
    CHECK_EQ(director_pick_world(&g), WORLD_COBERTIZO);
    g.news |= 1u << WORLD_SALA;
    CHECK_EQ(director_pick_world(&g), WORLD_SALA);
    g.rare_unseen = 1;
    CHECK_EQ(director_pick_world(&g), WORLD_JARDIN);
    g.news = 1u << WORLD_JARDIN;
    g.rare_unseen = 0;
    CHECK_EQ(director_pick_world(&g), WORLD_JARDIN);
    g.news = 0;
    CHECK_EQ(director_pick_world(&g), -1);
    /* El ❗ del HUD: un mundo con novedades pinta rojo (o naranja) sobre su marcador. */
    static Engine h;
    engine_init(&h, 3, t_morning(), 1000);
    RawInput none = {0, false};
    for (int i = 0; i < 40; i++)
        engine_frame(&h, none, t_morning(), 1000 + 33u * (uint32_t)i);
    h.gs.news = 1u << WORLD_PESCA;
    engine_frame(&h, none, t_morning(), 5000);
    int red = 0;
    for (int y = 2; y < 7; y++)
        for (int x = 30 + 4 * 5; x < 30 + 5 * 5; x++) {
            Color c = gfx_get(&h.fb, x, y);
            red += c == C_RED || c == C_ORANGE;
        }
    CHECK(red >= 4);
    h.gs.news = 0;
    engine_frame(&h, none, t_morning(), 5100);
    red = 0;
    for (int y = 2; y < 7; y++)
        for (int x = 30 + 4 * 5; x < 30 + 5 * 5; x++)
            red += gfx_get(&h.fb, x, y) == C_RED;
    CHECK_EQ(red, 0);
}

static void test_surprises_engine(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_morning();
    RawInput none = {0, false}, shake = {0, true}, a = {BTN_A, false}, b = {BTN_B, false};
    sala_ui_reset();
    engine_init(&e, 33, now, ms);
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    PhraseEnv env = penv(now);

    /* Cartucho sucio: los botones no hacen nada hasta agitar; agitar lo arregla. */
    director_force(&e.gs, &env, DS_GLITCH, 0);
    engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.dir.cur, DS_GLITCH);
    int world = e.gs.current_world;
    gesture(&e, BTN_B, now, &ms, true, 80);
    CHECK_EQ(e.gs.current_world, world);
    CHECK_EQ(e.gs.dir.cur, DS_GLITCH);
    engine_frame(&e, shake, now, ms += 33);
    CHECK_EQ(e.gs.dir.cur, DS_NONE);
    CHECK(has(e.toast, "") && e.toast[0]);
    gesture(&e, BTN_B, now, &ms, true, 80);
    CHECK(e.gs.current_world != world);
    /* ...y si nadie agita, se arregla solo a los 9 segundos. */
    director_force(&e.gs, &env, DS_GLITCH, 0);
    engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.dir.cur, DS_GLITCH);
    for (uint32_t t = 0; t < LLV_GLITCH_MS + 200; t += 100)
        engine_frame(&e, none, now, ms += 100);
    CHECK_EQ(e.gs.dir.cur, DS_NONE);

    /* Mundo ausente: el cartel solo está en ese mundo y se pinta sin salirse. */
    director_force(&e.gs, &env, DS_AUSENCIA, WORLD_SALA);
    CHECK_EQ(e.gs.current_world, WORLD_SALA);
    engine_frame(&e, none, now, ms += 33);
    int sign = 0;
    for (int y = 34; y < 60; y++)
        for (int x = 14; x < 106; x++)
            sign += gfx_get(&e.fb, x, y) == C_WOOD_L;
    CHECK(sign > 600);
    CHECK_EQ(e.gs.actions_used[WORLD_SALA], 0);
    gesture(&e, BTN_A, now, &ms, true, 80); /* sin nadie en casa, no se gasta ninguna conversación */
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK_EQ(e.gs.actions_used[WORLD_SALA], 0);
    CHECK_EQ(e.gs.sala.stage, DLG_IDLE);
    gesture(&e, BTN_B, now, &ms, true, 80);
    engine_frame(&e, none, now, ms += 33);
    sign = 0;
    for (int y = 34; y < 60; y++)
        for (int x = 14; x < 106; x++)
            sign += gfx_get(&e.fb, x, y) == C_WOOD_L;
    CHECK(sign < 100); /* en el siguiente mundo ya no hay cartel */

    /* Rebeldía: el primer B se queda donde está con "No. Me gusta aquí."; el segundo ya funciona. */
    director_force(&e.gs, &env, DS_REBELDIA, SPK_ABUELA);
    CHECK_EQ(e.gs.current_world, WORLD_SALA);
    CHECK(e.gs.dir.armed);
    gesture(&e, BTN_B, now, &ms, true, 80);
    CHECK_EQ(e.gs.current_world, WORLD_SALA);
    CHECK(!e.gs.dir.armed);
    CHECK_EQ(cur_kind(&e.gs), PK_REBELDIA);
    gesture(&e, BTN_B, now, &ms, true, 80);
    CHECK(e.gs.current_world != WORLD_SALA);

    /* INSERT COIN: A da un Deseo. */
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    director_force(&e.gs, &env, DS_MONEDA, 0);
    engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.dir.cur, DS_MONEDA);
    engine_frame(&e, b, now, ms += 33);
    engine_frame(&e, none, now, ms += 33);
    CHECK_EQ(e.gs.dir.cur, DS_MONEDA); /* B no hace nada */
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK_EQ(e.gs.dir.cur, DS_NONE);
    CHECK(e.gs.bag_count == 1 && e.gs.bag[0] == ITEM_DESEO);

    /* Arranque falso: la consola "arranca" ~1,8 s y luego sigue lo de siempre. */
    director_force(&e.gs, &env, DS_ARRANQUE, 0);
    uint32_t t0 = ms;
    engine_frame(&e, none, now, ms += 33);
    int dark = 0;
    for (int i = 0; i < LLV_W * LLV_H; i++)
        dark += e.fb.px[i] == C_DARK;
    CHECK(dark > LLV_W * LLV_H / 2);
    while (ms - t0 < 2000)
        engine_frame(&e, none, now, ms += 33);
    gesture(&e, BTN_B, now, &ms, true, 80); /* ya vuelve a hacer caso */
    (void)a;

    /* Invasión y colapso: se pintan sin romper nada, en cualquier mundo. */
    director_force(&e.gs, &env, DS_INVASION, WORLD_JARDIN << 4 | SPK_GAMER);
    CHECK_EQ(e.gs.current_world, WORLD_JARDIN);
    engine_frame(&e, none, now, ms += 33);
    director_force(&e.gs, &env, DS_COLAPSO, 0);
    for (int i = 0; i < 60; i++)
        engine_frame(&e, none, now, ms += 33);
    engine_sleep(&e, now);
    CHECK_EQ(e.gs.dir.cur, DS_NONE);
    CHECK(!e.gs.dir.armed);
}

static void test_secret_code(void)
{
    static Engine e;
    const int starts[3] = {WORLD_RELOJ, WORLD_COBERTIZO, WORLD_MUSICA};
    static const char GOOD[] = "BBAABABA";
    for (int s = 0; s < 3; s++) {
        uint32_t ms = 1000;
        int64_t now = t_morning();
        RawInput none = {0, false};
        sala_ui_reset();
        cobertizo_ui_reset();
        engine_init(&e, 5, now, ms);
        for (int i = 0; i < 40; i++)
            engine_frame(&e, none, now, ms += 33);
        pet_choose(&e.gs, PET_HONGO);
        e.gs.current_world = (uint8_t)starts[s];
        GameState before = e.gs;
        for (int i = 0; GOOD[i]; i++)
            gesture(&e, GOOD[i] == 'A' ? BTN_A : BTN_B, now, &ms, true, 80);
        /* El gato está en la Sala, dice algo y no se ha gastado nada por el camino. */
        CHECK_EQ(e.gs.sala.visitor, SPK_GATO);
        CHECK_EQ(e.gs.current_world, WORLD_SALA);
        CHECK_EQ(e.gs.sala.stage, DLG_SAY);
        CHECK_EQ(e.gs.sala.who, SPK_GATO);
        CHECK_EQ(e.gs.bag_count, before.bag_count);
        CHECK(memcmp(e.gs.bag, before.bag, sizeof(before.bag)) == 0);
        CHECK(memcmp(e.gs.actions_used, before.actions_used, sizeof(before.actions_used)) == 0);
        CHECK_EQ(e.gs.prepared, before.prepared);
        CHECK(garden_ok(&e.gs));
        CHECK(garden_count(&e.gs) == garden_count(&before));
    }
    /* Variantes que no son el código: ni gato ni nada raro. */
    static const char *const BAD[] = {"BBAABABB", "BBAAABAB", "BABAABAB", "BBBBBBBB", "ABAABABA"};
    for (unsigned k = 0; k < sizeof(BAD) / sizeof(BAD[0]); k++) {
        uint32_t ms = 1000;
        int64_t now = t_morning();
        RawInput none = {0, false};
        engine_init(&e, 5, now, ms);
        for (int i = 0; i < 40; i++)
            engine_frame(&e, none, now, ms += 33);
        for (int i = 0; BAD[k][i]; i++)
            gesture(&e, BAD[k][i] == 'A' ? BTN_A : BTN_B, now, &ms, true, 80);
        CHECK(e.gs.sala.visitor != SPK_GATO);
    }
    /* Demasiado despacio (más de 1,5 s entre toques) tampoco vale; una pulsación larga rompe la serie. */
    for (int mode = 0; mode < 2; mode++) {
        uint32_t ms = 1000;
        int64_t now = t_morning();
        RawInput none = {0, false};
        engine_init(&e, 5, now, ms);
        for (int i = 0; i < 40; i++)
            engine_frame(&e, none, now, ms += 33);
        for (int i = 0; GOOD[i]; i++) {
            if (i == 4 && mode == 0)
                ms += 3000;
            if (i == 4 && mode == 1)
                gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
            gesture(&e, GOOD[i] == 'A' ? BTN_A : BTN_B, now, &ms, true, 80);
        }
        CHECK(e.gs.sala.visitor != SPK_GATO);
    }
    /* El código no cambia la gramática: B sigue cambiando de mundo y A actuando. */
    uint32_t ms = 1000;
    int64_t now = t_morning();
    RawInput none = {0, false};
    engine_init(&e, 5, now, ms);
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    gesture(&e, BTN_B, now, &ms, true, 80);
    CHECK_EQ(e.gs.current_world, WORLD_JARDIN);
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK_EQ(e.gs.actions_used[WORLD_JARDIN], 1);
}

static void test_habits(void)
{
    static GameState gs;
    state_init(&gs, 90, t_autumn());
    int64_t now = t_morning();
    engine_advance(&gs, now);
    gs.hab.enabled = (1u << HAB_COUNT) - 1;
    pet_choose(&gs, PET_HONGO);
    /* Franjas del día: el sueño se pregunta por la mañana y las pantallas de noche. */
    CHECK(habit_question_ok(&gs, HAB_DORMIR, now));
    CHECK(!habit_question_ok(&gs, HAB_DORMIR, t_autumn() + 10 * 3600 + 5 * 3600));
    CHECK(!habit_question_ok(&gs, HAB_PANTALLA, now));
    CHECK(habit_question_ok(&gs, HAB_PANTALLA, t_autumn() + 15 * 3600 + 3600));
    CHECK(!habit_question_ok(&gs, HAB_MOVERSE, now)); /* lo mide el podómetro: no pregunta */

    /* Beber agua: un sí hace llover un poco, sin gastar objetos ni acciones. */
    int bag = gs.bag_count, acts = gs.actions_used[WORLD_JARDIN];
    gs.weather = W_CLEAR;
    gs.weather_left = 0;
    CHECK_EQ(habit_answer(&gs, HAB_AGUA, true), HA_EFFECT);
    CHECK(gs.weather == W_RAIN || gs.weather == W_SNOW);
    CHECK(gs.weather_left >= LLV_HABIT_RAIN_TICKS);
    CHECK_EQ(gs.bag_count, bag);
    CHECK_EQ(gs.actions_used[WORLD_JARDIN], acts);
    CHECK(count_events(&gs, EV_HABIT) == 1);
    /* una vez al día: el segundo sí no repite el efecto */
    gs.weather = W_CLEAR;
    CHECK_EQ(habit_answer(&gs, HAB_AGUA, true), 0);
    CHECK_EQ(gs.weather, W_CLEAR);
    /* no cumplirlo no castiga nada */
    GameState g2;
    state_init(&g2, 90, t_autumn());
    engine_advance(&g2, now);
    pet_choose(&g2, PET_HONGO);
    GameState snap = g2;
    CHECK_EQ(habit_answer(&g2, HAB_AGUA, false), 0);
    CHECK_EQ(habit_answer(&g2, HAB_SOL, false), 0);
    CHECK(same_state(&snap, &g2));
    /* los demás efectos */
    GameState g3 = gs;
    g3.hab.done_today = 0;
    int pa = g3.pet.happy;
    CHECK(habit_breath(&g3, 3) && g3.pet.happy > pa);
    CHECK(!habit_breath(&g3, 3)); /* una vez al día */
    CHECK(!habit_breath(&g3, 2));
    int flowers = garden_count_sp(&g3, SP_FLOR, ST_ANY), birds = garden_count_sp(&g3, SP_PAJARO, ST_ANY);
    ent_clear(&g3);
    CHECK_EQ(habit_answer(&g3, HAB_SOL, true), HA_EFFECT);
    CHECK_EQ(garden_count_sp(&g3, SP_FLOR, 0), 2);
    CHECK_EQ(habit_answer(&g3, HAB_PANTALLA, true), HA_EFFECT);
    CHECK_EQ(garden_count_sp(&g3, SP_PAJARO, ST_ANY), 1);
    (void)flowers;
    (void)birds;
    g3.bag_count = 0;
    memset(g3.bag, 0, sizeof(g3.bag));
    CHECK_EQ(habit_answer(&g3, HAB_LLAMAR, true), HA_EFFECT);
    CHECK(g3.bag_count == 1 && g3.bag[0] == ITEM_SEMILLA);

    /* Dormir: racha de noches buenas; un sí de alguien al que se vio despierto de madrugada es
     * "una mentira detectada" y corta la racha (sin castigo). */
    GameState g4;
    state_init(&g4, 90, t_autumn());
    engine_advance(&g4, now);
    CHECK_EQ(habit_answer(&g4, HAB_DORMIR, true), HA_EFFECT);
    CHECK_EQ(g4.hab.sleep_streak, 1);
    habits_new_day(&g4);
    CHECK_EQ(habit_answer(&g4, HAB_DORMIR, true), HA_EFFECT);
    CHECK_EQ(g4.hab.sleep_streak, 2);
    habits_new_day(&g4);
    habits_on_look(&g4, t_autumn() - 2 * 3600); /* una mirada a las 03:00 */
    CHECK(g4.hab.night_look);
    habits_new_day(&g4);
    CHECK(g4.hab.night_look_prev && !g4.hab.night_look);
    CHECK_EQ(habit_answer(&g4, HAB_DORMIR, true), HA_LIE);
    CHECK_EQ(g4.hab.sleep_streak, 0);
    CHECK_EQ(habit_answer(&g4, HAB_DORMIR, false), 0);
    CHECK_EQ(g4.hab.sleep_streak, 0);

    /* Cada hábito se puede desactivar: ni se pregunta ni se menciona. */
    GameState g5;
    state_init(&g5, 91, t_autumn());
    habit_set_enabled(&g5, HAB_AGUA, false);
    habit_set_enabled(&g5, HAB_SOL, false);
    CHECK(!habit_enabled(&g5, HAB_AGUA) && habit_enabled(&g5, HAB_PAUSA));
    Rng r;
    rng_seed(&r, 4, 4);
    for (int day = 0; day < 40; day++)
        for (int conv = 0; conv < 2; conv++) {
            int64_t t = t_autumn() + (int64_t)day * 86400 + (conv ? 16 : 8) * 3600;
            engine_advance(&g5, t);
            PhraseEnv env = penv(t);
            g5.sala.stage = DLG_IDLE;
            talk_tap(&g5, &env);
            for (int g = 0; g < 8 && (g5.sala.stage == DLG_ASK || g5.sala.scene > 0 || g == 0); g++) {
                CHECK(!cur_has_tag(&g5, PT_HABIT, HAB_AGUA) && !cur_has_tag(&g5, PT_HABIT, HAB_SOL));
                CHECK(!(g5.sala.stage == DLG_ASK && (g5.sala.q - 1 == Q_AGUA || g5.sala.q - 1 == Q_SOL)));
                if (g5.sala.stage == DLG_ASK)
                    talk_tap(&g5, &env);
                else if (g5.sala.scene > 0)
                    talk_tap(&g5, &env);
                else
                    break;
            }
        }
    CHECK_EQ(g5.hab.done_today & (1u << HAB_AGUA), 0);
    /* Tope de preguntas de hábitos al día. */
    GameState g6;
    state_init(&g6, 92, t_autumn());
    engine_advance(&g6, t_autumn() + 12 * 3600);
    int ok = 0;
    for (int q = 0; q < HAB_WITH_QUESTION; q++)
        if (habit_question_ok(&g6, q, t_autumn() + 12 * 3600)) {
            habit_note_asked(&g6, q, t_autumn() + 12 * 3600);
            ok++;
        }
    CHECK_EQ(ok, LLV_HABIT_MAX_PER_DAY);
    for (int q = 0; q < HAB_WITH_QUESTION; q++)
        CHECK(!habit_question_ok(&g6, q, t_autumn() + 12 * 3600));
}

static void test_habit_steps_pedometer(void)
{
    /* "Un paseo largo suma un objeto extra (máx. 1 al día)": con pasos inyectados. */
    static GameState gs;
    state_init(&gs, 12, t_autumn());
    engine_advance(&gs, t_morning());
    pet_choose(&gs, PET_COMPOSTERA);
    gs.bag_count = 0;
    memset(gs.bag, 0, sizeof(gs.bag));
    habit_steps(&gs, 123456);           /* la primera lectura es solo la referencia: sin salto */
    CHECK_EQ(gs.bag_count, 0);
    CHECK_EQ(gs.hab.steps_today, 0);
    habit_steps(&gs, 123456 + 3000);
    CHECK_EQ(gs.hab.steps_today, 3000);
    habit_steps(&gs, 123456 + LLV_WALK_STEPS - 1);
    CHECK_EQ(gs.bag_count, 0);          /* 5.999: aún no */
    int hp = gs.pet.happy;
    habit_steps(&gs, 123456 + LLV_WALK_STEPS);
    CHECK_EQ(gs.bag_count, 1);          /* 6.000: objeto extra */
    CHECK(gs.hab.walk_rewarded);
    CHECK(gs.pet.happy > hp);           /* y alimenta la compostera */
    CHECK_EQ(count_events(&gs, EV_WALK), 1);
    habit_steps(&gs, 123456 + 3 * LLV_WALK_STEPS);
    CHECK_EQ(gs.bag_count, 1);          /* máx. 1 al día */
    /* al día siguiente, otra vez */
    engine_advance(&gs, t_morning() + 86400);
    CHECK_EQ(gs.hab.steps_today, 0);
    gs.bag_count = 0;
    memset(gs.bag, 0, sizeof(gs.bag));
    habit_steps(&gs, 123456 + 3 * LLV_WALK_STEPS + 2 * LLV_WALK_STEPS);
    CHECK_EQ(gs.bag_count, 1);
    /* un contador que se reinicia (el chip arrancó otra vez) no inventa pasos */
    int bag = gs.bag_count;
    habit_steps(&gs, 50);
    CHECK_EQ(gs.hab.steps_last, 50);
    habit_steps(&gs, 60);
    CHECK_EQ(gs.bag_count, bag);
    /* con la bolsa llena se espera a que haya sitio; desactivado, no da nada */
    GameState g2;
    state_init(&g2, 13, t_autumn());
    habit_steps(&g2, 0);
    fill_bag(&g2, &(Rng){1, 3});
    habit_steps(&g2, 8000);
    CHECK(!g2.hab.walk_rewarded && g2.bag_count == LLV_BAG_SIZE);
    /* esperar con la bolsa llena no gasta azar: esto se llama en cada fotograma y el resultado no
     * puede depender de cuántos haya habido (un bug que encontró el test de determinismo) */
    Rng before = g2.rng;
    for (int k = 0; k < 200; k++)
        habit_steps(&g2, 8000);
    CHECK(g2.rng.state == before.state && g2.rng.inc == before.inc);
    g2.bag_count--;
    g2.bag[LLV_BAG_SIZE - 1] = ITEM_NONE;
    habit_steps(&g2, 8001);
    CHECK(g2.hab.walk_rewarded && g2.bag_count == LLV_BAG_SIZE);
    GameState g3;
    state_init(&g3, 14, t_autumn());
    habit_set_enabled(&g3, HAB_MOVERSE, false);
    habit_steps(&g3, 0);
    habit_steps(&g3, 20000);
    CHECK(!g3.hab.walk_rewarded && g3.bag_count == 2);
    /* y por el motor: el HAL pasa el podómetro con engine_set_body */
    static Engine e;
    uint32_t ms = 1000;
    engine_init(&e, 6, t_morning(), ms);
    RawInput none = {0, false};
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    engine_set_body(&e, 80, false, 1000);
    engine_frame(&e, none, t_morning(), ms += 33);
    engine_set_body(&e, 80, false, 1000 + LLV_WALK_STEPS + 10);
    engine_frame(&e, none, t_morning(), ms += 33);
    CHECK_EQ(e.gs.bag_count, 1);
    CHECK(e.gs.hab.walk_rewarded);
}

static void test_grandma_mode(void)
{
    /* Con el modo apto para la abuela, nada pícaro: ni charlas, ni preguntas, ni sorpresas. */
    static Engine e;
    static GameState gs;
    state_init(&gs, 303, t_autumn());
    gs.flags |= SF_GRANDMA;
    Rng r;
    rng_seed(&r, 3, 3);
    int shown = 0;
    for (int day = 0; day < 120; day++)
        for (int conv = 0; conv < 4; conv++) {
            int64_t t = t_autumn() + (int64_t)day * 86400 + (7 + conv * 4) * 3600 + (int64_t)rng_below(&r, 1800);
            engine_advance(&gs, t);
            PhraseEnv env = penv(t);
            gs.sala.fresh_a |= rng_next(&r);
            if (conv == 0)
                director_wake(&gs, &env, 5 * 3600, false);
            gs.sala.stage = conv == 0 ? gs.sala.stage : DLG_IDLE;
            for (int k = 0; k < 8; k++) {
                if (k == 0 && conv != 0)
                    talk_tap(&gs, &env);
                else if (gs.sala.stage == DLG_ASK)
                    (rng_below(&r, 2) ? talk_tap : talk_hold)(&gs, &env);
                else if (gs.sala.scene > 0)
                    talk_tap(&gs, &env);
                else if (k > 0)
                    break;
                if (gs.sala.phrase) {
                    shown++;
                    CHECK(!cur_picaro(&gs));
                }
            }
            if (rng_below(&r, 6) == 0) {
                talk_hold(&gs, &env);
                CHECK(!cur_picaro(&gs));
            }
            talk_shake(&gs, &env);
            CHECK(!cur_picaro(&gs));
            talk_gato(&gs, &env);
            CHECK(!cur_picaro(&gs));
            /* los textos fijos de las sorpresas */
            for (int kind = 0; kind < 3; kind++) {
                static const uint32_t K[3] = {PK_BIT(PK_CARTEL), PK_BIT(PK_GLITCH), PK_BIT(PK_REBELDIA)};
                for (int spk = 0; spk < SPK_COUNT; spk++) {
                    int idx = director_phrase(&gs, K[kind], spk);
                    PhraseRec pr;
                    if (idx >= 0 && phrase_get(idx, &pr))
                        CHECK(!pr.picaro);
                }
            }
        }
    printf("    (modo abuela: %d frases revisadas)\n", shown);
    CHECK(shown > 300);
    /* Con el modo apagado, en cambio, sí sale picardía. */
    gs.flags &= (uint8_t)~SF_GRANDMA;
    int spicy = 0;
    for (int k = 0; k < 300; k++) {
        PhraseEnv env = penv(t_autumn() + 400 * 86400 + k * 3600);
        engine_advance(&gs, env.now);
        gs.sala.stage = DLG_IDLE;
        gs.actions_used[WORLD_SALA] = 0;
        talk_tap(&gs, &env);
        spicy += cur_picaro(&gs);
    }
    CHECK(spicy > 15);
    (void)e;
}

static void test_affinity_without_guilt(void)
{
    /* Se mira a menudo: más cariñoso. Se ignora: pasivo-agresivo. Nunca cambia lo que pasa en el universo. */
    static Engine a, b;
    uint32_t msa = 1000, msb = 1000;
    int64_t now = t_morning();
    engine_init(&a, 7, now, msa);
    engine_init(&b, 7, now, msb);
    RawInput shake = {0, true};
    for (int d = 0; d < 10; d++) { /* A: 4 miradas al día */
        for (int l = 0; l < 4; l++) {
            int64_t t = now + (int64_t)d * 86400 + l * 4 * 3600;
            engine_frame(&a, shake, t, msa += 100000);
            engine_sleep(&a, t + 5);
        }
    }
    engine_frame(&b, shake, now + 10 * 86400, msb += 100000); /* B: una mirada tras 10 días */
    CHECK(a.gs.sala.affinity[SPK_ABUELA] >= LLV_AFF_HIGH);
    CHECK(b.gs.sala.affinity[SPK_ABUELA] <= LLV_AFF_LOW);
    PhraseCtx c;
    PhraseEnv env = penv(now);
    phrase_ctx_make(&c, &a.gs, &env);
    PhraseQuery q;
    memset(&q, 0, sizeof(q));
    q.kinds = PK_BIT(PK_CHARLA);
    q.speakers = SPK_BIT(SPK_ABUELA);
    q.karg = -1;
    q.need_tag = PT_AFF;
    int hi = phrase_eligible(&c, &q);
    phrase_ctx_make(&c, &b.gs, &env);
    int lo = phrase_eligible(&c, &q);
    CHECK(hi > 0 && lo > 0);
    /* y nunca se acaba nada: la afinidad no toca el jardín ni la bolsa */
    GameState x = b.gs, y = b.gs;
    x.sala.affinity[SPK_ABUELA] = 255;
    y.sala.affinity[SPK_ABUELA] = 0;
    engine_advance(&x, now + 12 * 86400);
    engine_advance(&y, now + 12 * 86400);
    CHECK(memcmp(x.ents, y.ents, sizeof(x.ents)) == 0 && x.bag_count == y.bag_count && x.pet.age == y.pet.age);
}

static void test_calendar_dates(void)
{
    CHECK_EQ(phrase_special_date(time_from_civil(2026, 10, 31, 12, 0, 0)), DATE_HALLOWEEN);
    CHECK_EQ(phrase_special_date(time_from_civil(2027, 8, 13, 12, 0, 0)), DATE_VIERNES13); /* viernes */
    CHECK_EQ(phrase_special_date(time_from_civil(2026, 8, 13, 12, 0, 0)), DATE_NONE);      /* jueves 13 */
    CHECK_EQ(phrase_special_date(time_from_civil(2027, 1, 1, 9, 0, 0)), DATE_ANIO_NUEVO);
    CHECK_EQ(phrase_special_date(time_from_civil(2026, 12, 31, 20, 0, 0)), DATE_ANIO_NUEVO);
    CHECK_EQ(phrase_special_date(time_from_civil(2026, 12, 31, 10, 0, 0)), DATE_NONE);
    CHECK_EQ(phrase_special_date(time_from_civil(2026, 12, 25, 10, 0, 0)), DATE_NAVIDAD);
    CHECK_EQ(phrase_special_date(t_morning()), DATE_NONE);
    /* El cumpleaños es un gancho para la fase 7: ninguna fecha lo activa todavía. */
    for (int d = 0; d < 366; d++)
        CHECK(phrase_special_date(time_from_civil(2026, 1, 1, 12, 0, 0) + (int64_t)d * 86400) != DATE_CUMPLE);
    /* El día de Halloween llega la Gótica a media tarde, sin tirar dados. */
    GameState gs;
    state_init(&gs, 5, time_from_civil(2026, 10, 31, 6, 0, 0));
    engine_advance(&gs, time_from_civil(2026, 10, 31, 15, 50, 0));
    CHECK(gs.sala.visitor != SPK_GOTICA || gs.sala.visitor_left > 0);
    engine_advance(&gs, time_from_civil(2026, 10, 31, 17, 0, 0));
    CHECK_EQ(gs.sala.visitor, SPK_GOTICA);
    engine_advance(&gs, time_from_civil(2026, 11, 1, 9, 0, 0)); /* se fue */
    CHECK(gs.sala.visitor != SPK_GOTICA || gs.sala.visitor_left > 0);
    /* Y el viernes 13, el gato. La sorpresa de calendario solo existe en fechas especiales. */
    state_init(&gs, 5, time_from_civil(2027, 8, 13, 6, 0, 0));
    engine_advance(&gs, time_from_civil(2027, 8, 13, 17, 0, 0));
    CHECK_EQ(gs.sala.visitor, SPK_GATO);
    PhraseEnv env = penv(time_from_civil(2027, 8, 13, 17, 0, 0));
    director_force(&gs, &env, DS_CALENDARIO, 0);
    CHECK_EQ(gs.dir.cur, DS_CALENDARIO);
    env = penv(t_morning());
    director_force(&gs, &env, DS_CALENDARIO, 0);
    CHECK_EQ(gs.dir.cur, DS_NONE);
}

static void test_patience_bonus(void)
{
    /* Cuanto más se espera (hasta 12 h), más probable un suceso raro; esperar más no suma; y
     * mirar a cada rato no da nada. */
    int hits[5] = {0}, trials = 600;
    static const int HOURS[5] = {1, 4, 8, 12, 48};
    for (int h = 0; h < 5; h++)
        for (int s = 0; s < trials; s++) {
            GameState gs;
            state_init(&gs, 1000 + (uint64_t)s, t_autumn());
            gs.flags |= SF_ASLEEP;
            engine_advance(&gs, t_morning());
            int before = count_events(&gs, EV_PATIENCE);
            engine_advance(&gs, t_morning() + (int64_t)HOURS[h] * 3600);
            hits[h] += count_events(&gs, EV_PATIENCE) > before;
        }
    printf("    (bono de paciencia, %% con suceso raro: 1 h %d, 4 h %d, 8 h %d, 12 h %d, 48 h %d)\n",
           hits[0] * 100 / trials, hits[1] * 100 / trials, hits[2] * 100 / trials, hits[3] * 100 / trials,
           hits[4] * 100 / trials);
    CHECK(hits[0] < hits[1] && hits[1] < hits[2] && hits[2] < hits[3]);
    CHECK(hits[0] * 100 / trials <= 4);
    CHECK(hits[3] * 100 / trials >= 20);
    CHECK(hits[4] <= hits[3] + trials / 10); /* el tope: pasadas las 12 h no sube */
    /* Despierto no hay bono: ni siquiera dejando pasar días. */
    int awake = 0;
    for (int s = 0; s < 100; s++) {
        GameState gs;
        state_init(&gs, 5000 + (uint64_t)s, t_autumn());
        engine_advance(&gs, t_morning() + 3 * 86400);
        awake += count_events(&gs, EV_PATIENCE);
    }
    CHECK_EQ(awake, 0);
    /* Mirar cada 15 minutos durante un día no da casi nada comparado con esperar medio día. */
    static Engine e;
    int frequent = 0, total_trials = 120;
    for (int s = 0; s < total_trials; s++) {
        uint32_t ms = 1000;
        engine_init(&e, 7000 + (uint64_t)s, t_morning(), ms);
        RawInput shake = {0, true};
        for (int k = 0; k < 48; k++) {
            int64_t t = t_morning() + (int64_t)k * 900;
            engine_sleep(&e, t);
            engine_frame(&e, shake, t + 900, ms += 100000);
        }
        frequent += count_events(&e.gs, EV_PATIENCE);
    }
    CHECK(frequent * 100 / total_trials < hits[3] * 100 / trials / 2);
}

static void test_led_warning(void)
{
    /* El LED de aviso: pasó algo raro mientras no mirabas. Dormido parpadea; al mirar se apaga. */
    static Engine e;
    uint32_t ms = 1000;
    int found = 0;
    for (int seed = 0; seed < 200 && !found; seed++) {
        engine_init(&e, 40 + (uint64_t)seed, t_morning(), ms);
        pet_choose(&e.gs, PET_HONGO);
        engine_sleep(&e, t_morning());
        RawInput none = {0, false}, shake = {0, true};
        FrameOut o = engine_frame(&e, none, t_morning() + 12 * 3600, 5000); /* ms % 5000 == 0: ventana del parpadeo */
        if (e.gs.rare_unseen == 0) {
            CHECK(!o.led);
            continue;
        }
        found = 1;
        CHECK(o.led);
        CHECK(!o.screen_on);
        o = engine_frame(&e, none, t_morning() + 12 * 3600, 5000 + 2000); /* fuera del parpadeo: apagado */
        CHECK(!o.led);
        o = engine_frame(&e, none, t_morning() + 12 * 3600, 10000 + 100); /* otra vez dentro */
        CHECK(o.led);
        o = engine_frame(&e, shake, t_morning() + 12 * 3600 + 1, 20000);
        CHECK(o.screen_on && !o.led);
        CHECK_EQ(e.gs.rare_unseen, 0);
        e.gs.dir.cur = DS_NONE;
        o = engine_frame(&e, none, t_morning() + 12 * 3600 + 2, 20100);
        CHECK(!o.led);
    }
    CHECK(found);
}

static void test_p3_determinism_live_vs_catchup(void)
{
    /* Con todo el estado nuevo (visitantes, sucesos frescos, bono de paciencia, ❗ y LED): lo simulado
     * mientras duerme, segundo a segundo, coincide con el catch-up de golpe, byte a byte. */
    static const int64_t spans[] = {10 * 3600, 36 * 3600, 5 * 86400, 7 * 86400};
    int gifts = 0, visits = 0;
    for (int seed = 1; seed <= 4; seed++)
        for (unsigned k = 0; k < sizeof(spans) / sizeof(spans[0]); k++) {
            GameState live, batch;
            state_init(&live, 500 + (uint64_t)seed, t_autumn());
            state_init(&batch, 500 + (uint64_t)seed, t_autumn());
            pet_choose(&live, PET_COMPOSTERA);
            pet_choose(&batch, PET_COMPOSTERA);
            live.flags |= SF_ASLEEP;
            batch.flags |= SF_ASLEEP;
            int64_t end = t_autumn() + spans[k];
            for (int64_t t = t_autumn(); t <= end; t += 7)
                engine_advance(&live, t);
            engine_advance(&live, end);
            engine_advance(&batch, end);
            CHECK(same_state(&live, &batch));
            gifts += live.rare_unseen > 0;
            visits += count_events(&live, EV_VISITOR);
        }
    CHECK(gifts > 0 && visits > 0); /* la prueba no es vacía */
}

/* Un guion con la Sala, hábitos, sorpresas, el código y el cuerpo del llavero. Los fotogramas son
 * densos o justos; esperas largas para que no importe cuándo se pinta cada letra. */
#define SALA_STEPS 70
static uint8_t ssnaps[2][SALA_STEPS][LLV_SAVE_MAX];
static size_t ssnap_len[2][SALA_STEPS];

static void wait_ms(Engine *e, int64_t now, uint32_t *ms, uint32_t dur, bool dense)
{
    RawInput none = {0, false};
    if (dense) {
        for (uint32_t t = 33; t <= dur; t += 33)
            engine_frame(e, none, now, *ms + t);
    }
    *ms += dur;
}

static void run_sala_script(Engine *e, bool dense, int slot)
{
    uint32_t ms = 1000;
    sala_ui_reset();
    jardin_ui_reset();
    cobertizo_ui_reset();
    engine_init(e, 2468, t_autumn(), ms);
    RawInput none = {0, false}, shake = {0, true};
    int64_t now = t_autumn();
    Rng r;
    rng_seed(&r, 77, 7);
    uint32_t steps = 0;
    for (int step = 0; step < SALA_STEPS; step++) {
        /* una ausencia de minutos a un día */
        uint32_t pick = rng_below(&r, 10);
        now += pick < 2 ? 120 + (int64_t)rng_below(&r, 300) : 1800 + (int64_t)rng_below(&r, pick == 9 ? 24 * 3600 : 6 * 3600);
        ms += 100000;
        steps += rng_below(&r, 3500);
        engine_set_body(e, (uint8_t)(rng_below(&r, 3) ? 70 : 12), rng_below(&r, 8) == 0, steps);
        engine_frame(e, shake, now, ms);
        /* tras despertar, hasta que se acabe de escribir lo que el Director haya puesto: un toque a
         * mitad de una frase solo la completa, y eso depende de los milisegundos exactos del toque */
        wait_ms(e, now, &ms, 4200, dense);
        if (e->gs.dir.cur == DS_GLITCH) {
            engine_frame(e, shake, now, ms += 33);
            wait_ms(e, now, &ms, 600, dense);
        } else if (e->gs.dir.cur == DS_MONEDA) {
            gesture(e, BTN_A, now, &ms, dense, 80);
        }
        int what = (int)rng_below(&r, 6);
        for (int i = 0; i < 6 && e->gs.current_world != WORLD_SALA && what < 4; i++)
            gesture(e, BTN_B, now, &ms, dense, 80);
        if (what < 4 && e->gs.current_world == WORLD_SALA) {
            int taps = 1 + (int)rng_below(&r, 5);
            for (int t = 0; t < taps; t++) {
                bool hold = rng_below(&r, 5) == 0;
                gesture(e, BTN_A, now, &ms, dense, hold ? LLV_HOLD_MS + 100 : 80);
                wait_ms(e, now, &ms, 4200, dense);
            }
            if (rng_below(&r, 4) == 0) {
                engine_frame(e, shake, now, ms += 33);
                wait_ms(e, now, &ms, 4200, dense);
            }
        } else if (what == 4) {
            for (int i = 0; i < 6 && e->gs.current_world != WORLD_COBERTIZO; i++)
                gesture(e, BTN_B, now, &ms, dense, 80);
            if (e->gs.pet.kind == PET_NONE)
                gesture(e, BTN_A, now, &ms, dense, LLV_HOLD_MS + 100);
            for (int t = 0; t < 4; t++)
                gesture(e, BTN_A, now, &ms, dense, rng_below(&r, 2) ? 80 : LLV_HOLD_MS + 100);
        } else {
            for (int i = 0; i < 6 && e->gs.current_world != WORLD_JARDIN; i++)
                gesture(e, BTN_B, now, &ms, dense, 80);
            gesture(e, BTN_A, now, &ms, dense, 80);
        }
        if (step % 17 == 5) { /* el código secreto, tecleado desde donde esté */
            static const char CODE[] = "BBAABABA";
            for (int i = 0; CODE[i]; i++)
                gesture(e, CODE[i] == 'A' ? BTN_A : BTN_B, now, &ms, dense, 80);
        }
        engine_frame(e, none, now, ms += 33);
        engine_sleep(e, now + 20);
        now += 20;
        ssnap_len[slot][step] = state_serialize(&e->gs, ssnaps[slot][step], LLV_SAVE_MAX);
    }
}

static void test_p3_determinism_interactions(void)
{
    /* Dos motores reciben los mismos gestos, sacudidas, sueños y pasos a las mismas horas; uno pinta un
     * fotograma cada 33 ms y el otro solo los justos. Con las tiradas del Director, la elección de
     * frases (gs->rng), las preguntas y los hábitos, el estado es idéntico tras cada paso. */
    static Engine a, b;
    run_sala_script(&a, true, 0);
    run_sala_script(&b, false, 1);
    for (int step = 0; step < SALA_STEPS; step++) {
        CHECK(ssnap_len[0][step] > 0 && ssnap_len[0][step] == ssnap_len[1][step]);
        int same = memcmp(ssnaps[0][step], ssnaps[1][step], ssnap_len[0][step]) == 0;
        if (!same && step < SALA_STEPS) {
            static GameState x, y;
            state_deserialize(&x, ssnaps[0][step], ssnap_len[0][step]);
            state_deserialize(&y, ssnaps[1][step], ssnap_len[1][step]);
            printf("    primer paso distinto: %d (tick %lld/%lld, rng %d, sala %d, dir %d, hab %d, mundo %d/%d, looks %u/%u)\n", step,
                   (long long)x.tick, (long long)y.tick, memcmp(&x.rng, &y.rng, sizeof(x.rng)) != 0,
                   memcmp(&x.sala, &y.sala, sizeof(x.sala)) != 0, memcmp(&x.dir, &y.dir, sizeof(x.dir)) != 0,
                   memcmp(&x.hab, &y.hab, sizeof(x.hab)) != 0, x.current_world, y.current_world,
                   (unsigned)x.looks_total, (unsigned)y.looks_total);
            CHECK(same);
            break;
        }
        CHECK(same);
    }
    CHECK(garden_ok(&a.gs));
    CHECK(a.gs.dir.rolls > 10);
    CHECK(a.gs.actions_used[WORLD_SALA] > 0 || a.gs.diary_total > 100);
    CHECK(a.gs.sala.memo[0] || a.gs.sala.memo[1] || a.gs.sala.memo[2] || a.gs.sala.memo[3] ||
          a.gs.sala.memo[4] || a.gs.sala.memo[5] || a.gs.sala.memo[6] || a.gs.sala.memo[7] ||
          a.gs.sala.memo[8] || a.gs.sala.memo[9] || a.gs.sala.memo[10] || a.gs.sala.memo[11] ||
          a.gs.sala.memo[12] || a.gs.sala.memo[13]);
    /* Y un guardado a mitad de camino sigue la misma vida que el original. */
    static GameState back;
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = ser(&a.gs, buf);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK(same_state(&a.gs, &back));
    engine_advance(&back, t_autumn() + 12 * 86400);
    engine_advance(&a.gs, t_autumn() + 12 * 86400);
    CHECK(same_state(&a.gs, &back));
}

static void test_save_phase3(void)
{
    static GameState gs, back;
    state_init(&gs, 0xFA5E3, t_autumn());
    pet_choose(&gs, PET_PIEDRA);
    Rng r;
    rng_seed(&r, 3, 3);
    for (int d = 0; d < 12; d++) {
        int64_t t = t_autumn() + (int64_t)d * 86400 + 9 * 3600;
        engine_advance(&gs, t);
        PhraseEnv env = penv(t);
        gs.sala.stage = DLG_IDLE;
        for (int k = 0; k < 2; k++) {
            talk_tap(&gs, &env);
            settle(&gs, &env, rng_below(&r, 2));
        }
        director_wake(&gs, &env, 3 * 3600, false);
    }
    gs.flags = SF_GRANDMA | SF_ASLEEP;
    gs.news = 0x2A;
    gs.rare_unseen = 3;
    gs.unseen_ticks = 40;
    gs.gap_min = 4321;
    gs.streak = 9;
    gs.hab.enabled = 0x5B;
    gs.hab.sleep_streak = 5;
    gs.hab.steps_last = 123456;
    gs.hab.steps_today = 4321;
    gs.hab.night_look_prev = 1;
    gs.sala.visitor = SPK_BOOMER;
    gs.sala.visitor_left = 17;
    gs.dir.cur = DS_AUSENCIA;
    gs.dir.arg = WORLD_JARDIN;
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = ser(&gs, buf);
    CHECK(n > 0 && n < 4096);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK(same_state(&gs, &back));
    CHECK(memcmp(gs.sala.recent, back.sala.recent, sizeof(gs.sala.recent)) == 0);
    CHECK(memcmp(gs.sala.memo, back.sala.memo, sizeof(gs.sala.memo)) == 0);
    CHECK(memcmp(gs.sala.memo_day, back.sala.memo_day, sizeof(gs.sala.memo_day)) == 0);
    CHECK(back.hab.steps_last == 123456 && back.hab.enabled == 0x5B && back.streak == 9 && back.gap_min == 4321);
    CHECK(back.sala.fresh_a == gs.sala.fresh_a && back.sala.fresh_b == gs.sala.fresh_b);
    engine_advance(&gs, t_autumn() + 30 * 86400);
    engine_advance(&back, t_autumn() + 30 * 86400);
    CHECK(same_state(&gs, &back));

    /* Un guardado de la fase 2 (versión 2) se rechaza con limpieza y no toca el destino. */
    uint8_t old[LLV_SAVE_MAX];
    memcpy(old, buf, n);
    old[4] = 2;
    fix_crc(old, n);
    GameState out;
    state_init(&out, 1, t_autumn());
    out.looks_total = 777;
    CHECK(state_deserialize(&out, old, n) != 0);
    CHECK_EQ(out.looks_total, 777);
    /* Y un v2 de verdad (más corto, sin los campos nuevos) tampoco entra. */
    memcpy(old, buf, n - 4 - 200);
    old[4] = 2;
    old[6] = (uint8_t)(n - 200 - 12);
    old[7] = (uint8_t)((n - 200 - 12) >> 8);
    fix_crc(old, n - 200);
    CHECK(state_deserialize(&out, old, n - 200) != 0);
    /* Valores absurdos con CRC bueno no entran. */
    struct { const char *name; } dummy = {0};
    (void)dummy;
    GameState evil;
    uint8_t bad[LLV_SAVE_MAX];
    size_t m;
#define EVIL(stmt)                                    \
    do {                                              \
        evil = gs;                                    \
        stmt;                                         \
        m = ser(&evil, bad);                          \
        CHECK(state_deserialize(&out, bad, m) != 0);  \
    } while (0)
    EVIL(evil.sala.stage = 9);
    EVIL(evil.sala.visitor = 99);
    EVIL(evil.sala.q = Q_COUNT + 3);
    EVIL(evil.sala.recent_head = LLV_RECENT);
    EVIL(evil.sala.memo[2] = 3);
    EVIL(evil.sala.memo[Q_COUNT] = 1);
    EVIL(evil.dir.cur = DS_COUNT);
    EVIL(evil.dir.last = DK_COUNT);
    EVIL(evil.hab.enabled = 0xFF);
    EVIL(evil.hab.asked_count = 9);
    EVIL(evil.news = 0xC0);
    EVIL(evil.flags = 0x40);
    EVIL(evil.sala.phrase = 60000; evil.sala.stage = DLG_SAY);
#undef EVIL
    CHECK_EQ(state_deserialize(&out, buf, n), 0);
}

static void test_render_sala(void)
{
    /* La Sala se pinta sin salirse en cualquier estado: visitantes, fechas, pijama, ausencia, y con
     * cada una de las frases del banco en la caja (el AddressSanitizer vigila los accesos). */
    static Engine e;
    uint32_t ms = 1000;
    RawInput none = {0, false};
    static const int64_t DATES[] = {
        0, 1, 2, 3, 4,
    };
    (void)DATES;
    int64_t when[6] = {
        time_from_civil(2026, 10, 22, 17, 0, 0), time_from_civil(2026, 10, 23, 3, 0, 0),
        time_from_civil(2026, 10, 31, 18, 0, 0), time_from_civil(2027, 8, 13, 18, 0, 0),
        time_from_civil(2027, 1, 1, 12, 0, 0), time_from_civil(2026, 12, 25, 12, 0, 0),
    };
    for (int w = 0; w < 6; w++) {
        sala_ui_reset();
        engine_init(&e, 40 + (uint64_t)w, when[w], ms);
        e.gs.current_world = WORLD_SALA;
        for (int v = 0; v < SPK_COUNT; v++)
            for (int absent = 0; absent < 2; absent++) {
                e.gs.sala.visitor = (uint8_t)v;
                e.gs.sala.visitor_left = 20;
                e.gs.dir.cur = absent ? DS_AUSENCIA : DS_NONE;
                e.gs.dir.arg = WORLD_SALA;
                for (int i = 0; i < 6; i++) {
                    e.last_activity_ms = ms;
                    FrameOut o = engine_frame(&e, none, when[w], ms += 700);
                    CHECK(o.screen_on);
                }
            }
        e.gs.dir.cur = DS_NONE;
        e.seen_cur = DS_NONE;
    }
    /* cada frase del banco en la caja, ya escrita del todo y a medias */
    sala_ui_reset();
    engine_init(&e, 5, when[0], ms);
    e.gs.current_world = WORLD_SALA;
    int n = phrase_count();
    for (int i = 0; i < n; i++) {
        PhraseRec r;
        phrase_get(i, &r);
        e.gs.sala.phrase = (uint16_t)(i + 1);
        e.gs.sala.who = r.speaker;
        e.gs.sala.stage = r.kind == PK_PREGUNTA ? DLG_ASK : DLG_SAY;
        e.gs.sala.fill[0] = (uint16_t)(i % 3);
        e.gs.sala.fill[1] = (uint16_t)(i % 16);
        e.gs.sala.fill[2] = 23 * 60 + 59;
        e.gs.sala.visitor = (uint8_t)(r.speaker >= SPK_GOTICA && r.speaker <= SPK_GATO ? r.speaker : 0);
        e.gs.sala.visitor_left = 20;
        for (int k = 0; k < 3; k++) {
            e.last_activity_ms = ms;
            engine_frame(&e, none, when[0], ms += (k ? 5000 : 40));
        }
    }
    CHECK(cast_selfcheck() == 0);
    CHECK(art_selfcheck() == 0);
}

static void test_typewriter_voices(void)
{
    /* Texto letra a letra con un pitido por letra, de un tono distinto por personaje, por FrameOut.tones;
     * un toque durante la escritura la completa sin gastar conversación. */
    static Engine e;
    RawInput none = {0, false};
    int first_tone[SPK_COUNT], mean_tone[SPK_COUNT];
    for (int spk = SPK_ABUELA; spk <= SPK_GATO; spk++) {
        uint32_t ms = 1000;
        int64_t now = t_morning();
        sala_ui_reset();
        engine_init(&e, 9, now, ms);
        for (int i = 0; i < 40; i++)
            engine_frame(&e, none, now, ms += 33);
        e.gs.current_world = WORLD_SALA;
        e.gs.sala.visitor = (uint8_t)(spk >= SPK_GOTICA ? spk : 0);
        e.gs.sala.visitor_left = 20;
        PhraseEnv env = penv(now);
        e.gs.dir.cur = DS_NONE;
        CHECK(talk_say(&e.gs, &env, PK_BIT(PK_CHARLA), SPK_BIT(spk), -1));
        int tones = 0, letters_seen = 0, sum = 0;
        first_tone[spk] = 0;
        char txt[256];
        talk_text(&e.gs, txt, sizeof(txt));
        int total;
        gfx_text_wrap_n(NULL, 0, 0, PHRASE_BOX_W, txt, C_DARK, -1, &total);
        for (int t = 0; t < 140; t++) {
            FrameOut o = engine_frame(&e, none, now, ms += 33);
            for (int k = 0; k < o.tone_count && k < ENGINE_MAX_TONES; k++) {
                tones++;
                sum += o.tones[k].freq;
                if (!first_tone[spk])
                    first_tone[spk] = o.tones[k].freq;
            }
            letters_seen += o.tone_count > 0;
        }
        CHECK(tones > total / 2 && tones <= total + 4); /* ~un pitido por letra (los espacios no) */
        CHECK(first_tone[spk] > 100 && first_tone[spk] < 2000);
        mean_tone[spk] = tones ? sum / tones : 0;
        /* ya escrita, calla */
        FrameOut o = engine_frame(&e, none, now, ms += 33);
        CHECK_EQ(o.tone_count, 0);
        (void)letters_seen;
    }
    for (int a = SPK_ABUELA; a <= SPK_GATO; a++)
        for (int b = a + 1; b <= SPK_GATO; b++)
            CHECK(mean_tone[a] - mean_tone[b] >= 25 || mean_tone[b] - mean_tone[a] >= 25); /* tonos distintos */
    /* un toque mientras escribe: completa la frase y no gasta nada */
    uint32_t ms = 1000;
    int64_t now = t_morning();
    sala_ui_reset();
    engine_init(&e, 9, now, ms);
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = WORLD_SALA;
    gesture(&e, BTN_A, now, &ms, true, 80);
    CHECK_EQ(e.gs.actions_used[WORLD_SALA], 1);
    int phrase = e.gs.sala.phrase;
    CHECK(phrase > 0);
    /* el toque llega mientras aún se escribe (la gesture suma 480 ms, de sobra para <14 letras, pero no para 60) */
    ms = ms - 400;
    gesture(&e, BTN_A, now, &ms, true, 80);
    if (e.gs.sala.stage == DLG_SAY && e.gs.sala.scene == 0) {
        char txt[256];
        talk_text(&e.gs, txt, sizeof(txt));
        int total;
        gfx_text_wrap_n(NULL, 0, 0, PHRASE_BOX_W, txt, C_DARK, -1, &total);
        if (total > 30) {
            CHECK_EQ(e.gs.actions_used[WORLD_SALA], 1);
            CHECK_EQ(e.gs.sala.phrase, phrase);
        }
    }
}

static void test_cobertizo_habit_menu(void)
{
    /* Ajustes de hábitos desde el Cobertizo: mantener A sobre la colección abre el menú. */
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_morning();
    RawInput none = {0, false};
    cobertizo_ui_reset();
    engine_init(&e, 4, now, ms);
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    pet_choose(&e.gs, PET_HONGO);
    e.gs.current_world = WORLD_COBERTIZO;
    e.gs.bag_count = 1;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    e.gs.bag[0] = ITEM_NUBE;
    CHECK(!cobertizo_menu_open());
    gesture(&e, BTN_A, now, &ms, true, 80); /* del objeto a la colección */
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK(cobertizo_menu_open());
    CHECK(habit_enabled(&e.gs, HAB_AGUA));
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100); /* fila 0: desactiva el agua */
    CHECK(!habit_enabled(&e.gs, HAB_AGUA));
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100); /* y lo vuelve a activar */
    CHECK(habit_enabled(&e.gs, HAB_AGUA));
    for (int i = 0; i < HAB_COUNT; i++)
        gesture(&e, BTN_A, now, &ms, true, 80); /* baja hasta "Apto abuela" */
    CHECK(!(e.gs.flags & SF_GRANDMA));
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100);
    CHECK(e.gs.flags & SF_GRANDMA);
    gesture(&e, BTN_A, now, &ms, true, 80);
    gesture(&e, BTN_A, now, &ms, true, LLV_HOLD_MS + 100); /* Salir */
    CHECK(!cobertizo_menu_open());
    /* Respirar con la mascota hace el hábito: con 3 respiraciones, ella se alegra (y solo una vez al día). */
    cobertizo_ui_reset();
    e.gs.hab.done_today = 0;
    e.gs.pet.happy = 60;
    gesture(&e, BTN_A, now, &ms, true, 80); /* de la bolsa a la colección */
    gesture(&e, BTN_A, now, &ms, true, 80); /* a la mascota */
    CHECK(cobertizo_on_pet(&e.gs));
    gesture(&e, BTN_A, now, &ms, true, 80); /* empieza */
    CHECK(cobertizo_breathing());
    for (int k = 0; k < 4; k++) {
        RawInput hold = {BTN_A, false};
        for (uint32_t t = 0; t < 3700; t += 33)
            engine_frame(&e, hold, now, ms += 33);
        for (uint32_t t = 0; t < 3700; t += 33)
            engine_frame(&e, none, now, ms += 33);
    }
    gesture(&e, BTN_A, now, &ms, true, 80); /* sale */
    CHECK(!cobertizo_breathing());
    CHECK(e.gs.pet.happy > 60);
    CHECK(e.gs.hab.done_today >> HAB_PAUSA & 1);
}

static void test_sunrise_streak(void)
{
    /* Una buena racha de sueño da un amanecer especial en el Reloj: se nota en los píxeles. */
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = time_from_civil(2026, 10, 22, 7, 20, 0);
    RawInput none = {0, false};
    engine_init(&e, 3, now, ms);
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = WORLD_RELOJ;
    e.gs.reloj_style = 0;
    e.gs.hab.sleep_streak = 0;
    engine_frame(&e, none, now, ms += 33);
    Framebuffer normal = e.fb;
    e.gs.hab.sleep_streak = LLV_SLEEP_STREAK_GOOD;
    engine_frame(&e, none, now, ms += 33);
    int diff = 0;
    for (int i = 0; i < LLV_W * LLV_H; i++)
        diff += normal.px[i] != e.fb.px[i];
    CHECK(diff > 400);
    /* Fuera de la franja del amanecer no cambia nada. */
    int64_t noon = time_from_civil(2026, 10, 22, 13, 0, 0);
    engine_sleep(&e, now);
    RawInput shake = {0, true};
    engine_frame(&e, shake, noon, ms += 100000);
    e.gs.current_world = WORLD_RELOJ;
    e.gs.dir.cur = DS_NONE;
    e.gs.hab.sleep_streak = 0;
    for (int i = 0; i < 60; i++)
        engine_frame(&e, none, noon, ms += 33);
    normal = e.fb;
    e.gs.hab.sleep_streak = 9;
    engine_frame(&e, none, noon, ms += 33);
    diff = 0;
    for (int i = 0; i < LLV_W * LLV_H; i++)
        diff += normal.px[i] != e.fb.px[i];
    CHECK(diff < 40); /* solo el parpadeo del reloj */
}

static void test_input_body_defaults(void)
{
    /* Un RawInput de la fase 0 sigue valiendo; el cuerpo entra por engine_set_body. */
    RawInput in = {BTN_A, true};
    CHECK(in.buttons == BTN_A && in.shake);
    static Engine e;
    engine_init(&e, 1, t_morning(), 1000);
    CHECK_EQ(e.battery, 0);
    engine_set_body(&e, 15, true, 99);
    CHECK(e.battery == 15 && e.charging && e.steps == 99);
    /* la frase de batería baja solo se elige con batería baja */
    PhraseEnv env = {t_morning(), false, 15, false};
    PhraseCtx c;
    phrase_ctx_make(&c, &e.gs, &env);
    CHECK(c.batt_low);
    env.battery = 90;
    phrase_ctx_make(&c, &e.gs, &env);
    CHECK(!c.batt_low);
    env.battery = 0; /* no se sabe */
    phrase_ctx_make(&c, &e.gs, &env);
    CHECK(!c.batt_low);
}


int main(void)
{
    struct { const char *name; TestFn fn; } tests[] = {
        {"determinismo: en vivo vs catch-up", test_determinism_live_vs_catchup},
        {"determinismo: pasos irregulares", test_determinism_irregular_steps},
        {"determinismo: dormir/despertar", test_determinism_sleep_wake},
        {"catch-up: tope de 7 dias", test_catchup_cap},
        {"despertar anota las horas", test_wake_logs_hours},
        {"crc32 vector estandar", test_crc32_vector},
        {"guardado: ida y vuelta", test_save_roundtrip},
        {"guardado: rechaza corruptos", test_save_rejects_bad},
        {"timeutil: calendario civil", test_time_civil},
        {"timeutil: dia del llavero (05:00)", test_time_day_key},
        {"timeutil: estaciones", test_time_seasons},
        {"timeutil: fases lunares", test_time_moon},
        {"input: gestos", test_input},
        {"temporizadores: ms_pending y vuelta del contador", test_ms_pending},
        {"rng: PCG32", test_rng},
        {"bolsa: recarga, tope y perdidas", test_bag},
        {"bolsa: objeto preparado", test_bag_prepared},
        {"diario: buffer circular", test_diary_circular},
        {"reloj: hora en palabras", test_reloj_words},
        {"motor: gestos y sueno", test_engine_gestures},
        /* fase 2 */
        {"jardin: tablas de datos coherentes", test_tables},
        {"estado: tamano fijo < 4 KB", test_state_size},
        {"jardin: estado inicial", test_garden_init},
        {"jardin: determinismo vivo vs catch-up", test_garden_determinism_live_vs_catchup},
        {"jardin: determinismo con acciones intercaladas", test_garden_determinism_with_actions},
        {"motor: determinismo con gestos y suenos", test_engine_determinism_gestures},
        {"jardin: nunca pasa de 32 (1200 dias)", test_garden_never_overflows},
        {"jardin: uso normal, 400 dias, esta vivo", test_garden_realistic_week_is_alive},
        {"jardin: lleno no nace nada", test_garden_full_spawns_nothing},
        {"jardin: topes por especie", test_species_caps},
        {"jardin: 2 acciones al dia y reinicio a las 05:00", test_garden_action_limit},
        {"jardin: mensajes del motor (descansa, vacia)", test_engine_garden_messages},
        {"bolsa: objeto preparado se usa y se consume", test_prepared_item},
        {"jardin: efecto de cada objeto", test_item_effects},
        {"jardin: manzana podrida -> brote/hongo/nada", test_rotten_apple_chain_statistics},
        {"jardin: tiempos de la cadena de la manzana", test_apple_chain_timing},
        {"jardin: el viento tira la fruta", test_wind_drops_fruit},
        {"jardin: pajaro come manzana y deja semilla", test_bird_eats_apple_and_leaves_seed},
        {"jardin: lluvia acelera, charco -> rana", test_rain_speeds_growth},
        {"jardin: estaciones", test_seasons},
        {"coleccion: descubrimientos", test_collection},
        {"mascota: elegir y ficha solemne", test_pet_choose_and_ficha},
        {"mascota: nunca muere, ciclo y generaciones", test_pet_never_dies_and_generations},
        {"mascota: etapas", test_pet_stage_events},
        {"mascota: un cuidado al dia", test_pet_care_daily_limit},
        {"mascota: gustos y objeto ofrecido", test_pet_likes_and_offering},
        {"mascota: rarezas de cada una", test_pet_rarities},
        {"guardado v2: ida y vuelta con todo", test_save_roundtrip_phase2},
        {"guardado: rechaza el de la fase 0", test_save_rejects_old_version},
        {"guardado: rechaza jardin corrupto", test_save_rejects_bad_garden},
        {"cobertizo: elegir, cuidar, respirar, preparar", test_engine_cobertizo_flow},
        {"jardin: lupa sin coste", test_engine_lupa},
        {"render: todos los mundos y estados", test_render_all_worlds},
        /* fase 3 */
        {"frases: banco valido (tags, huecos, fuente, caja)", test_phrase_bank_valid},
        {"frases: preguntas, reacciones y escenas completas", test_phrase_questions_complete},
        {"frases: cargador robusto ante bancos rotos", test_phrase_loader_robust},
        {"frases: ninguna fuera de su contexto (oraculo)", test_phrase_selection_context},
        {"frases: antirrepeticion de las ultimas 30", test_phrase_antirepeat},
        {"frases: huecos y frases de la spec", test_phrase_fill_grammar},
        {"sala: 2 conversaciones al dia y reinicio a las 05:00", test_sala_daily_limit},
        {"sala: memoria de respuestas y retomada dias despues", test_sala_questions_memory},
        {"sala: preguntas, topes y no insistir (60 dias)", test_sala_question_rules_60_days},
        {"sala: escenas a dos voces", test_sala_scene_two_voices},
        {"sala: comentarios del universo", test_sala_universe_comments},
        {"sala: texto letra a letra y un tono por personaje", test_typewriter_voices},
        {"sala: visitantes por calendario", test_calendar_dates},
        {"sala: modo apto para la abuela", test_grandma_mode},
        {"sala: afinidad sin culpa", test_affinity_without_guilt},
        {"director: nunca dos sorpresas, seguro de pena", test_director_rules},
        {"director: abre el mundo con novedades y marca con !", test_director_opens_world_with_news},
        {"director: el gato sale ~1 de cada 300", test_sala_secret_cat_frequency},
        {"sorpresas: glitch, salí, rebeldia, moneda, arranque", test_surprises_engine},
        {"codigo secreto B B A A B A B A", test_secret_code},
        {"habitos: efectos, tope diario, desactivar, mentiras", test_habits},
        {"habitos: podometro con pasos inyectados", test_habit_steps_pedometer},
        {"habitos: ajustes en el Cobertizo y respirar", test_cobertizo_habit_menu},
        {"reloj: amanecer especial con buena racha", test_sunrise_streak},
        {"paciencia: mas espera, mas probable (tope 12 h)", test_patience_bonus},
        {"LED de aviso: algo raro mientras no mirabas", test_led_warning},
        {"cuerpo del llavero: bateria, pasos y compatibilidad", test_input_body_defaults},
        {"determinismo fase 3: vivo vs catch-up", test_p3_determinism_live_vs_catchup},
        {"determinismo fase 3: gestos, dados y frases", test_p3_determinism_interactions},
        {"guardado v3: ida y vuelta, rechaza v2 y absurdos", test_save_phase3},
        {"render: la Sala con cada frase, visitante y fecha", test_render_sala},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0])), ok = 0;
    for (int i = 0; i < n; i++)
        ok += run_test(tests[i].name, tests[i].fn);
    printf("\n%d/%d tests OK, %d comprobaciones, %d fallos\n", ok, n, checks, failures);
    return failures ? 1 : 0;
}
