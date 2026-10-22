/* Tests del motor, sin framework externo. Salida != 0 si algo falla. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/engine.h"
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
        {"rng: PCG32", test_rng},
        {"bolsa: recarga, tope y perdidas", test_bag},
        {"bolsa: objeto preparado", test_bag_prepared},
        {"diario: buffer circular", test_diary_circular},
        {"reloj: hora en palabras", test_reloj_words},
        {"motor: gestos y sueno", test_engine_gestures},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0])), ok = 0;
    for (int i = 0; i < n; i++)
        ok += run_test(tests[i].name, tests[i].fn);
    printf("\n%d/%d tests OK, %d comprobaciones, %d fallos\n", ok, n, checks, failures);
    return failures ? 1 : 0;
}
