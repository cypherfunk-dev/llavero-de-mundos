/* Tests del motor, sin framework externo. Salida != 0 si algo falla. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/art.h"
#include "../src/core/engine.h"
#include "../src/core/garden.h"
#include "../src/core/pet.h"
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
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0])), ok = 0;
    for (int i = 0; i < n; i++)
        ok += run_test(tests[i].name, tests[i].fn);
    printf("\n%d/%d tests OK, %d comprobaciones, %d fallos\n", ok, n, checks, failures);
    return failures ? 1 : 0;
}
