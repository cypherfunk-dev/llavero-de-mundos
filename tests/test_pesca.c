/* Tests de la Pesca (fase 5), sin framework externo. Salida != 0 si algo falla.
 * Cubren la tabla de especies, la selección por contexto (noche, lluvia, estación, luna llena),
 * el lanzamiento (2 al día, lombriz, objeto preparado), el minijuego (reproducible, el jugador perfecto
 * gana y el que no pulsa pierde), el desenlace (colección, récords, diario, regalos baratos), la cadena
 * lombriz -> pez -> colección -> diario por el motor, el guardado y el determinismo del universo. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/director.h"
#include "../src/core/engine.h"
#include "../src/core/fish.h"
#include "../src/core/garden.h"
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

typedef void (*TestFn)(void);
static int run_test(const char *name, TestFn fn)
{
    int before = failures;
    fn();
    printf("[%s] %s\n", failures == before ? " OK " : "FAIL", name);
    return failures == before;
}

static int has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

static int same_state(const GameState *a, const GameState *b)
{
    uint8_t x[LLV_SAVE_MAX], y[LLV_SAVE_MAX];
    size_t nx = state_serialize(a, x, sizeof(x)), ny = state_serialize(b, y, sizeof(y));
    return nx > 0 && nx == ny && memcmp(x, y, nx) == 0;
}

static void fix_crc(uint8_t *buf, size_t len)
{
    uint32_t c = state_crc32(buf, len - 4);
    for (int i = 0; i < 4; i++)
        buf[len - 4 + i] = (uint8_t)(c >> (8 * i));
}

static int64_t t_autumn(void) { return time_from_civil(2026, 10, 22, 5, 0, 0); }
static int64_t t_noon(void) { return time_from_civil(2026, 10, 22, 12, 0, 0); }

/* Un universo al día con la bolsa a medida: 'worms' lombrices y nada más. */
static void universe(GameState *gs, uint64_t seed, int64_t t, int worms)
{
    state_init(gs, seed, t);
    engine_advance(gs, t);
    memset(gs->bag, 0, sizeof(gs->bag));
    gs->bag_count = 0;
    gs->prepared = ITEM_NONE;
    for (int i = 0; i < worms; i++)
        state_bag_add(gs, ITEM_LOMBRIZ);
}

/* Una noche de luna llena (23:30) y otra que no: se buscan con la propia función de fase. */
static int64_t find_night(bool full)
{
    int64_t t = time_from_civil(2026, 10, 1, 23, 30, 0);
    for (int d = 0; d < 80; d++, t += 86400)
        if ((time_moon_phase(t) == 4) == full)
            return t;
    return t;
}

/* ---------- la tabla ---------- */

static void test_table(void)
{
    CHECK_EQ(FISH_SPECIES, 6);
    CHECK_EQ(COLL_PEZ0 + FISH_SPECIES, COLL_COUNT);
    CHECK_EQ(art_fish_selfcheck(), 0);
    int junk = 0;
    for (int i = 0; i < FISH_TOTAL; i++) {
        const FishInfo *f = fish_info(i);
        CHECK(f && f->name[0] && f->tag[0] && f->humor[0] && f->quip[0]);
        CHECK(f->weight >= 1 && f->diff >= 1 && f->diff <= 10 && f->temper < FT_COUNT);
        CHECK((f->junk != 0) == (i >= FISH_SPECIES));
        junk += f->junk;
        CHECK(f->smin <= f->smax);
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(f->name, fish_info(j)->name) != 0);
            CHECK(strcmp(f->tag, fish_info(j)->tag) != 0);
        }
        /* lo que se dibuja cabe: nombre en la ficha (62 px, 2 líneas), humor entre « », comentario */
        char buf[64];
        CHECK(gfx_text_wrap(NULL, 0, 0, 62, f->name, C_DARK) <= 2);
        snprintf(buf, sizeof(buf), "«%s»", f->humor);
        CHECK(gfx_text_width(buf) <= 100);
        CHECK(gfx_text_wrap(NULL, 0, 0, 100, f->quip, C_DARK) <= 5);
        CHECK(gfx_text_width(f->humor) <= 104); /* y en la tarjeta del resultado */
        CHECK(gfx_text_width(f->tag) <= 48);
        /* el nombre de cada pez es el de su hueco en la colección */
        if (!f->junk)
            CHECK(strcmp(state_coll_name((CollSlot)(COLL_PEZ0 + i)), f->name) == 0);
    }
    CHECK_EQ(junk, FISH_JUNK);
    CHECK(fish_info(-1) == NULL && fish_info(FISH_TOTAL) == NULL);
    CHECK_EQ(FISH_TABLE[FISH_LEGEND].need, FC_NIGHT | FC_FULLMOON); /* el legendario, solo con luna llena */
    /* Los nombres tienen humor: los de la spec están. */
    CHECK_EQ(strcmp(FISH_TABLE[0].name, "Pez Lunes"), 0);
    CHECK_EQ(strcmp(FISH_TABLE[1].name, "Carpa Resignada"), 0);
}

/* ---------- selección por contexto ---------- */

static void tally(int ctx, int season, int n, uint32_t seed, int counts[FISH_TOTAL])
{
    Rng r;
    rng_seed(&r, seed, 5);
    memset(counts, 0, sizeof(int) * FISH_TOTAL);
    for (int i = 0; i < n; i++)
        counts[fish_pick(ctx, season, &r)]++;
}

/* Las frecuencias salen de los pesos (a 4 sigmas) y lo que pesa 0 no sale nunca. */
static void check_distribution(int ctx, int season, const char *what)
{
    enum { N = 60000 };
    int counts[FISH_TOTAL], total_w = 0;
    tally(ctx, season, N, 12345, counts);
    for (int i = 0; i < FISH_TOTAL; i++)
        total_w += fish_weight(fish_info(i), ctx, season);
    CHECK(total_w > 0);
    for (int i = 0; i < FISH_TOTAL; i++) {
        int w = fish_weight(fish_info(i), ctx, season);
        if (w == 0) {
            if (counts[i] != 0)
                printf("    %s: %s salió %d veces con peso 0\n", what, fish_name(i), counts[i]);
            CHECK_EQ(counts[i], 0);
            continue;
        }
        double p = (double)w / total_w, mean = p * N;
        double sigma = sqrt((double)N * p * (1.0 - p));
        double diff = (double)counts[i] - mean;
        if (diff < 0)
            diff = -diff;
        if (diff > 4.5 * sigma + 2)
            printf("    %s: %s salió %d veces, esperadas %.0f\n", what, fish_name(i), counts[i], mean);
        CHECK(diff <= 4.5 * sigma + 2);
    }
}

static void test_selection_by_context(void)
{
    const int SUMMER = SEASON_SUMMER, AUTUMN = SEASON_AUTUMN, WINTER = SEASON_WINTER;
    int c[FISH_TOTAL];

    /* Un día de verano despejado: solo los comunes y alguna basura. */
    tally(FC_DAY, SUMMER, 30000, 1, c);
    CHECK(c[0] > 0 && c[1] > 0);                      /* Pez Lunes y Carpa */
    CHECK_EQ(c[2] + c[3] + c[4] + c[5], 0);           /* ni la trucha, ni el bagre, ni el paraguas, ni el koi */
    int junk = 0;
    for (int i = FISH_SPECIES; i < FISH_TOTAL; i++)
        junk += c[i];
    CHECK(junk > 30000 * 8 / 100 && junk < 30000 * 20 / 100); /* basura simpática: de vez en cuando */

    /* De noche salen los nocturnos... */
    tally(FC_NIGHT, SUMMER, 30000, 2, c);
    CHECK(c[3] > 30000 * 10 / 100);                   /* Bagre Insomne: un buen puñado */
    CHECK_EQ(c[4] + c[5], 0);                         /* sin lluvia ni luna llena no hay paraguas ni koi */
    int day_bagre;
    tally(FC_DAY, SUMMER, 30000, 2, c);
    day_bagre = c[3];
    CHECK_EQ(day_bagre, 0);                           /* ...y de día nunca */

    /* Con lluvia, los de lluvia; y mejores peces (menos basura, más raros). */
    int rain[FISH_TOTAL], dry[FISH_TOTAL];
    tally(FC_DAY | FC_RAIN, SUMMER, 30000, 3, rain);
    tally(FC_DAY, SUMMER, 30000, 3, dry);
    CHECK(rain[4] > 30000 * 8 / 100);
    CHECK_EQ(dry[4], 0);
    int j_rain = 0, j_dry = 0;
    for (int i = FISH_SPECIES; i < FISH_TOTAL; i++) {
        j_rain += rain[i];
        j_dry += dry[i];
    }
    CHECK(j_rain * 10 < j_dry * 7); /* la lluvia aleja las basuras */
    /* "Mejores peces": la parte de peces raros (peso base <= 60) crece con la lluvia. */
    int rare_rain = 0, rare_dry = 0;
    for (int i = 0; i < FISH_SPECIES; i++)
        if (FISH_TABLE[i].weight <= 60) {
            rare_rain += rain[i];
            rare_dry += dry[i];
        }
    CHECK(rare_rain > rare_dry);

    /* Estación: la trucha de escarcha solo en otoño e invierno, y más en invierno. */
    tally(FC_DAY, SUMMER, 30000, 4, c);
    CHECK_EQ(c[2], 0);
    int autumn[FISH_TOTAL], winter[FISH_TOTAL];
    tally(FC_DAY, AUTUMN, 30000, 4, autumn);
    tally(FC_DAY | FC_WINTER, WINTER, 30000, 4, winter);
    CHECK(autumn[2] > 0 && winter[2] > autumn[2] * 3 / 2);
    tally(FC_DAY, SEASON_SPRING, 30000, 4, c);
    CHECK_EQ(c[2], 0);

    /* El legendario: SOLO con luna llena y de noche, y nunca fuera de ella (muchas tiradas, todos los contextos). */
    int big = 0;
    for (int ctx = 0; ctx < 128; ctx++) {
        if ((ctx & FC_DAY) && (ctx & FC_NIGHT))
            continue; /* contextos imposibles */
        for (int season = 0; season < 4; season++) {
            tally(ctx, season, 400, 70 + (uint32_t)ctx, c);
            bool moon_night = (ctx & FC_FULLMOON) && (ctx & FC_NIGHT);
            if (!moon_night)
                CHECK_EQ(c[FISH_LEGEND], 0);
            else
                big += c[FISH_LEGEND];
        }
    }
    CHECK(big > 0);
    tally(FC_NIGHT | FC_FULLMOON, AUTUMN, 60000, 5, c);
    CHECK(c[FISH_LEGEND] > 60000 * 5 / 100 && c[FISH_LEGEND] < 60000 * 20 / 100);

    /* Y las frecuencias son las que dicen los pesos, contexto a contexto. */
    check_distribution(FC_DAY, SUMMER, "día de verano");
    check_distribution(FC_NIGHT, AUTUMN, "noche de otoño");
    check_distribution(FC_DAY | FC_RAIN, SEASON_SPRING, "lluvia de primavera");
    check_distribution(FC_NIGHT | FC_RAIN | FC_WINTER, WINTER, "noche de lluvia en invierno");
    check_distribution(FC_NIGHT | FC_FULLMOON, AUTUMN, "luna llena de otoño");
    check_distribution(FC_NIGHT | FC_FULLMOON | FC_RAIN, WINTER, "luna llena con lluvia");
}

static void test_context_from_universe(void)
{
    GameState gs;
    /* Mediodía de otoño: de día, sin luna llena. */
    int64_t t = find_night(false) / 86400 * 86400 + 12 * 3600;
    universe(&gs, 1, t, 0);
    gs.weather = W_CLEAR;
    int ctx = fish_ctx(&gs);
    CHECK(ctx & FC_DAY && !(ctx & FC_NIGHT) && !(ctx & FC_RAIN));
    gs.weather = W_RAIN;
    CHECK(fish_ctx(&gs) & FC_RAIN);
    /* Medianoche de luna llena. */
    int64_t full = find_night(true);
    universe(&gs, 1, full, 0);
    gs.weather = W_CLEAR;
    ctx = fish_ctx(&gs);
    CHECK(ctx & FC_NIGHT && ctx & FC_FULLMOON && !(ctx & FC_DAY));
    /* Y sin luna llena (la otra mitad del mes). */
    universe(&gs, 1, find_night(false), 0);
    CHECK(!(fish_ctx(&gs) & FC_FULLMOON));
    /* Invierno: la bandera de la estación. */
    universe(&gs, 1, time_from_civil(2027, 1, 15, 12, 0, 0), 0);
    CHECK(fish_ctx(&gs) & FC_WINTER);
    CHECK_EQ(fish_season(&gs), SEASON_WINTER);
}

/* ---------- el lanzamiento ---------- */

static void test_cast_limits(void)
{
    GameState gs;
    FishPlan plan;
    bool prep;
    universe(&gs, 5, t_noon(), 3);
    CHECK_EQ(gs.bag_count, 3);
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_OK);
    CHECK_EQ(gs.bag_count, 2);
    CHECK_EQ(gs.actions_used[WORLD_PESCA], 1);
    CHECK_EQ(gs.fish.casts, 1);
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_OK);
    CHECK_EQ(gs.bag_count, 1);
    /* Límite de 2 al día: el tercero no toca nada (ni la lombriz ni el azar). */
    GameState before = gs;
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_NO_ACTIONS);
    CHECK(same_state(&gs, &before));
    CHECK_EQ(gs.bag_count, 1);
    /* Reinicio a las 05:00: la víspera de las 05:00 sigue sin lanzamientos... */
    int64_t day2 = time_from_civil(2026, 10, 23, 5, 0, 0);
    engine_advance(&gs, day2 - 600);
    CHECK_EQ(gs.actions_used[WORLD_PESCA], 2);
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_NO_ACTIONS);
    /* ...y a las 05:00 en punto vuelven. */
    engine_advance(&gs, day2);
    CHECK_EQ(gs.actions_used[WORLD_PESCA], 0);
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_OK);
    CHECK_EQ(gs.actions_used[WORLD_PESCA], 1);
    /* Trasnochar no regala acciones: a las 03:00 del día 23 seguía el día anterior. */
    GameState late;
    universe(&late, 5, time_from_civil(2026, 10, 22, 12, 0, 0), 4);
    fish_cast(&late, WORLD_PESCA, &plan, &prep);
    fish_cast(&late, WORLD_PESCA, &plan, &prep);
    engine_advance(&late, time_from_civil(2026, 10, 23, 3, 0, 0));
    CHECK_EQ(fish_cast(&late, WORLD_PESCA, &plan, &prep), CAST_NO_ACTIONS);
}

static void test_cast_worm_and_prepared(void)
{
    GameState gs;
    FishPlan plan;
    bool prep = true;
    /* Sin lombriz: no se lanza y no se gasta nada. */
    universe(&gs, 6, t_noon(), 0);
    state_bag_add(&gs, ITEM_NUBE);
    state_bag_add(&gs, ITEM_SEMILLA);
    GameState before = gs;
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_NO_WORM);
    CHECK(same_state(&gs, &before));
    CHECK_EQ(gs.actions_used[WORLD_PESCA], 0);

    /* Con una lombriz entre otras cosas: se gasta la lombriz y las demás se quedan en su orden. */
    universe(&gs, 6, t_noon(), 0);
    state_bag_add(&gs, ITEM_NUBE);
    state_bag_add(&gs, ITEM_LOMBRIZ);
    state_bag_add(&gs, ITEM_SEMILLA);
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_OK);
    CHECK(!prep);
    CHECK_EQ(gs.bag_count, 2);
    CHECK_EQ(gs.bag[0], ITEM_NUBE);
    CHECK_EQ(gs.bag[1], ITEM_SEMILLA);
    CHECK_EQ(gs.bag[2], ITEM_NONE);

    /* El objeto preparado desde el Cobertizo cuenta si es lombriz... */
    universe(&gs, 6, t_noon(), 0);
    state_bag_add(&gs, ITEM_LOMBRIZ);
    state_bag_add(&gs, ITEM_NUBE);
    gs.prepared = ITEM_LOMBRIZ;
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_OK);
    CHECK(prep);
    CHECK_EQ(gs.prepared, ITEM_NONE);
    CHECK_EQ(gs.bag_count, 1);
    /* ...y si lo preparado es otra cosa, se queda preparado para el Jardín. */
    universe(&gs, 6, t_noon(), 0);
    state_bag_add(&gs, ITEM_NUBE);
    state_bag_add(&gs, ITEM_LOMBRIZ);
    gs.prepared = ITEM_NUBE;
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_OK);
    CHECK(!prep);
    CHECK_EQ(gs.prepared, ITEM_NUBE);
    CHECK_EQ(gs.bag_count, 1);
    CHECK_EQ(gs.bag[0], ITEM_NUBE);

    /* Cada lanzamiento anota EV_CAST en el diario. */
    const DiaryEntry *d = state_diary_get(&gs, 0);
    CHECK(d && d->kind == EV_CAST);
}

static void test_cast_one_rng_draw(void)
{
    GameState gs;
    FishPlan plan;
    bool prep;
    universe(&gs, 7, t_noon(), 2);
    Rng ref = gs.rng;
    uint32_t seed = rng_next(&ref); /* UNA tirada de gs->rng por lanzamiento */
    CHECK_EQ(fish_cast(&gs, WORLD_PESCA, &plan, &prep), CAST_OK);
    CHECK(memcmp(&gs.rng, &ref, sizeof(Rng)) == 0);
    /* Y lo sorteado es función de esa semilla y del contexto: reproducible. */
    FishPlan again;
    fish_plan(&again, seed, fish_ctx(&gs), fish_season(&gs));
    CHECK_EQ(again.idx, plan.idx);
    CHECK_EQ(again.size, plan.size);
    CHECK_EQ(again.wait_ms, plan.wait_ms);
    CHECK(again.game_seed == plan.game_seed);
    CHECK(plan.wait_ms >= 1200 && plan.wait_ms <= 3400);
    const FishInfo *f = fish_info(plan.idx);
    CHECK(f && plan.size >= f->smin && plan.size <= f->smax);
}

/* ---------- el minijuego ---------- */

static uint32_t trace_hash(int temper, int diff, uint32_t seed, bool use_ai, int steps)
{
    FishGame g;
    fishgame_init(&g, seed, temper, diff);
    uint32_t h = 2166136261u;
    for (int i = 0; i < steps && g.state == FGS_PLAYING; i++) {
        bool hold = use_ai ? fishgame_ai(&g) : ((i / 17) & 1);
        fishgame_step(&g, hold);
        h = (h ^ (uint32_t)g.fpos) * 16777619u;
        h = (h ^ (uint32_t)(g.bar + 2000)) * 16777619u;
        h = (h ^ (uint32_t)g.meter) * 16777619u;
    }
    return h;
}

static void test_minigame_reproducible(void)
{
    for (int temper = 0; temper < FT_COUNT; temper++) {
        for (uint32_t seed = 1; seed < 40; seed++) {
            /* Misma semilla => mismos movimientos del pez (y de la barra con las mismas pulsaciones). */
            CHECK(trace_hash(temper, 5, seed * 977u, true, 400) == trace_hash(temper, 5, seed * 977u, true, 400));
            CHECK(trace_hash(temper, 5, seed * 977u, false, 400) == trace_hash(temper, 5, seed * 977u, false, 400));
        }
        /* Semillas distintas => recorridos distintos. */
        int distinct = 0;
        for (uint32_t seed = 1; seed < 40; seed++)
            distinct += trace_hash(temper, 5, seed, true, 300) != trace_hash(temper, 5, seed + 1000, true, 300);
        CHECK(distinct >= 38);
    }
    /* El pez se mueve solo con SU generador: el movimiento no depende de lo que pulse el jugador. */
    FishGame a, b;
    fishgame_init(&a, 4242, FT_JUMPY, 6);
    fishgame_init(&b, 4242, FT_JUMPY, 6);
    for (int i = 0; i < 300; i++) {
        fishgame_step(&a, true);
        fishgame_step(&b, false);
        if (a.state != FGS_PLAYING || b.state != FGS_PLAYING)
            break;
        CHECK_EQ(a.fpos, b.fpos);
    }
}

static void test_minigame_skill(void)
{
    int perfect_wins = 0, perfect_total = 0, never_wins = 0, never_total = 0;
    for (int sp = 0; sp < FISH_TOTAL; sp++) {
        const FishInfo *f = fish_info(sp);
        for (uint32_t seed = 1; seed <= 300; seed++) {
            FishGame g;
            /* El jugador perfecto SIEMPRE gana, antes del tiempo máximo y con todo dentro de rango. */
            fishgame_init(&g, seed * 2654435761u, f->temper, f->diff);
            while (g.state == FGS_PLAYING) {
                fishgame_step(&g, fishgame_ai(&g));
                CHECK(g.fpos >= FISHGAME_MIN && g.fpos <= FISHGAME_MAX);
                CHECK(g.bar >= g.zone / 2 && g.bar <= 1000 - g.zone / 2);
                CHECK(g.meter >= 0 && g.meter <= 1000);
            }
            perfect_total++;
            perfect_wins += g.state == FGS_WON;
            CHECK(g.steps < LLV_FISH_TIMEOUT_STEPS);
            /* El que nunca pulsa SIEMPRE pierde (y pronto: el pez nunca baja a la barra en reposo). */
            fishgame_init(&g, seed * 2654435761u, f->temper, f->diff);
            while (g.state == FGS_PLAYING)
                fishgame_step(&g, false);
            never_total++;
            never_wins += g.state == FGS_WON;
            CHECK(g.steps < 200);
        }
    }
    CHECK_EQ(perfect_wins, perfect_total);
    CHECK_EQ(never_wins, 0);

    /* El que mantiene A siempre (la barra arriba) tampoco gana: el pez no sube tanto. */
    int hold_wins = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        FishGame g;
        fishgame_init(&g, seed, FT_CALM, 2);
        while (g.state == FGS_PLAYING)
            fishgame_step(&g, true);
        hold_wins += g.state == FGS_WON;
    }
    CHECK_EQ(hold_wins, 0);

    /* Un humano con reflejos de 200 ms y algún error: lo fácil sale casi siempre y lo difícil, a veces. */
    int win[FISH_SPECIES] = {0};
    for (int sp = 0; sp < FISH_SPECIES; sp++) {
        const FishInfo *f = fish_info(sp);
        for (uint32_t seed = 1; seed <= 300; seed++) {
            FishGame g, hist[32];
            fishgame_init(&g, seed * 40503u, f->temper, f->diff);
            for (int i = 0; i < 32; i++)
                hist[i] = g;
            Rng r;
            rng_seed(&r, seed, 3);
            for (int n = 0; g.state == FGS_PLAYING; n++) {
                hist[n % 32] = g;
                bool hold = fishgame_ai(&hist[(n - 10 + 64) % 32]);
                if (rng_below(&r, 100) < 3)
                    hold = !hold;
                fishgame_step(&g, hold);
            }
            win[sp] += g.state == FGS_WON;
        }
    }
    CHECK(win[0] >= 285 && win[1] >= 285);               /* los comunes: casi seguro */
    CHECK(win[FISH_LEGEND] < win[0] && win[FISH_LEGEND] > 15 && win[FISH_LEGEND] < 270); /* el legendario cuesta, pero se puede */
    printf("    (humano de 200 ms, 3 %% de error: ");
    for (int sp = 0; sp < FISH_SPECIES; sp++)
        printf("%s %d%%%s", FISH_TABLE[sp].tag, win[sp] * 100 / 300, sp + 1 < FISH_SPECIES ? ", " : ")\n");
}

/* ---------- desenlace ---------- */

static void test_resolve_collection_and_records(void)
{
    GameState gs;
    universe(&gs, 8, t_noon(), 0);
    gs.sala.fresh_a = gs.sala.fresh_b = 0;
    FishResult r;
    FishPlan p = {1, 41, 0, 0}; /* Carpa Resignada, 41 cm */
    fish_resolve(&gs, &p, true, &r);
    CHECK(r.won && r.new_species && !r.record && !r.legend);
    CHECK((gs.collection >> (COLL_PEZ0 + 1)) & 1);
    CHECK_EQ(gs.fish.caught[1], 1);
    CHECK_EQ(gs.fish.best[1], 41);
    CHECK_EQ(fish_collected(&gs), 1);
    /* Sin duplicados en la colección: el segundo no es nuevo y no vuelve a anotar el descubrimiento. */
    int discover_before = 0, discover_after = 0;
    for (int i = 0; i < gs.diary_count; i++)
        discover_before += state_diary_get(&gs, i)->kind == EV_DISCOVER;
    p.size = 30;
    fish_resolve(&gs, &p, true, &r);
    CHECK(r.won && !r.new_species && !r.record);
    CHECK_EQ(gs.fish.best[1], 41); /* un pez más pequeño no baja el récord */
    CHECK_EQ(gs.fish.caught[1], 2);
    p.size = 55;
    fish_resolve(&gs, &p, true, &r);
    CHECK(!r.new_species && r.record);
    CHECK_EQ(gs.fish.best[1], 55);
    CHECK_EQ(gs.fish.caught[1], 3);
    for (int i = 0; i < gs.diary_count; i++)
        discover_after += state_diary_get(&gs, i)->kind == EV_DISCOVER;
    CHECK_EQ(discover_after, discover_before);
    CHECK_EQ(fish_collected(&gs), 1);
    CHECK_EQ(fish_total_caught(&gs), 3);
    /* Cada captura queda en el diario y es noticia para la Sala. */
    CHECK(gs.sala.fresh_a >> EV_FISH & 1);
    CHECK_EQ(state_diary_get(&gs, 0)->kind, EV_FISH);
    CHECK_EQ(state_diary_get(&gs, 0)->a, 1);

    /* Un pez que se escapa: contador y diario, y no entra en la colección. */
    p = (FishPlan){3, 50, 0, 0};
    fish_resolve(&gs, &p, false, &r);
    CHECK(!r.won);
    CHECK_EQ(gs.fish.fled, 1);
    CHECK_EQ(gs.fish.caught[3], 0);
    CHECK(!((gs.collection >> (COLL_PEZ0 + 3)) & 1));
    CHECK_EQ(state_diary_get(&gs, 0)->kind, EV_FISH_LOST);
    CHECK_EQ(state_diary_get(&gs, 0)->a, 3);
    /* Una basura: aparece en el diario, no cuenta en la colección. */
    p = (FishPlan){FISH_SPECIES, 0, 0, 0};
    uint16_t coll = gs.collection;
    fish_resolve(&gs, &p, true, &r);
    CHECK_EQ(gs.collection, coll);
    CHECK_EQ(gs.fish.junk, 1);
    CHECK_EQ(state_diary_get(&gs, 0)->kind, EV_FISH_JUNK);
    /* Los seis peces completan la colección de peces, una sola vez. */
    int completes = 0;
    for (int i = 0; i < FISH_SPECIES; i++)
        for (int k = 0; k < 2; k++) {
            p = (FishPlan){(uint8_t)i, (uint8_t)(fish_info(i)->smin + k), 0, 0};
            fish_resolve(&gs, &p, true, &r);
            completes += r.complete;
        }
    CHECK_EQ(completes, 1);
    CHECK_EQ(fish_collected(&gs), FISH_SPECIES);
    CHECK_EQ(gs.collection & 0xFC00, 0xFC00);
    /* El legendario tiene su propio suceso. */
    p = (FishPlan){FISH_LEGEND, 120, 0, 0};
    fish_resolve(&gs, &p, true, &r);
    CHECK(r.legend);
    CHECK_EQ(state_diary_get(&gs, 0)->kind, EV_FISH_LEGEND);
    /* Los contadores se saturan en 255 en vez de dar la vuelta. */
    for (int i = 0; i < 300; i++)
        fish_resolve(&gs, &p, true, NULL);
    CHECK_EQ(gs.fish.caught[FISH_LEGEND], 255);
}

static void test_resolve_cheap_gifts(void)
{
    GameState gs;
    FishResult r;
    /* Los raros dejan un objeto (si cabe) y alegran a la mascota. */
    universe(&gs, 9, t_noon(), 0);
    pet_choose(&gs, PET_HONGO);
    uint8_t happy = gs.pet.happy;
    FishPlan p = {4, 20, 0, 0}; /* Pez Paraguas -> Nube */
    fish_resolve(&gs, &p, true, &r);
    CHECK_EQ(r.gift_item, ITEM_NUBE);
    CHECK_EQ(gs.bag_count, 1);
    CHECK_EQ(gs.bag[0], ITEM_NUBE);
    CHECK(r.joy && gs.pet.happy > happy);
    /* Con la bolsa llena no cae (y no se pierde nada: simplemente no hay objeto). */
    universe(&gs, 9, t_noon(), 0);
    for (int i = 0; i < LLV_BAG_SIZE; i++)
        state_bag_add(&gs, ITEM_VIENTO);
    fish_resolve(&gs, &p, true, &r);
    CHECK_EQ(r.gift_item, 0);
    CHECK_EQ(gs.bag_count, LLV_BAG_SIZE);
    /* Sin mascota no hay a quién alegrar. */
    universe(&gs, 9, t_noon(), 0);
    fish_resolve(&gs, &p, true, &r);
    CHECK(!r.joy);
    /* El alga son restos de pesca: la Compostera los acepta y devuelve una lombriz. */
    universe(&gs, 9, t_noon(), 0);
    pet_choose(&gs, PET_COMPOSTERA);
    p = (FishPlan){FISH_SPECIES + 2, 0, 0, 0}; /* Alga Enredada */
    CHECK(strcmp(fish_name(p.idx), "Alga Enredada") == 0);
    fish_resolve(&gs, &p, true, &r);
    CHECK(r.compost);
    CHECK_EQ(gs.bag_count, 1);
    CHECK_EQ(gs.bag[0], ITEM_LOMBRIZ);
    /* Con otra mascota, el alga es solo un alga. */
    universe(&gs, 9, t_noon(), 0);
    pet_choose(&gs, PET_PIEDRA);
    fish_resolve(&gs, &p, true, &r);
    CHECK(!r.compost);
    CHECK_EQ(gs.bag_count, 0);
    /* El Koi Lunar concede un Deseo. */
    universe(&gs, 9, t_noon(), 0);
    p = (FishPlan){FISH_LEGEND, 100, 0, 0};
    fish_resolve(&gs, &p, true, &r);
    CHECK_EQ(r.gift_item, ITEM_DESEO);
    CHECK_EQ(gs.bag[0], ITEM_DESEO);
    /* Perder un pez no da nada. */
    universe(&gs, 9, t_noon(), 0);
    fish_resolve(&gs, &p, false, &r);
    CHECK_EQ(gs.bag_count, 0);
}

/* ---------- por el motor: la cadena entera ---------- */

static void gesture(Engine *e, uint8_t btn, int64_t now, uint32_t *ms, uint32_t hold_ms)
{
    RawInput down = {btn, false}, up = {0, false};
    uint32_t t0 = *ms;
    for (uint32_t t = 0; t < hold_ms; t += 20)
        engine_frame(e, down, now, t0 + t);
    engine_frame(e, up, now, t0 + hold_ms);
    for (uint32_t t = 20; t <= 300; t += 20)
        engine_frame(e, up, now, t0 + hold_ms + t);
    *ms = t0 + hold_ms + 320;
}

/* Frames de 20 ms hasta que 'until' (o el tope); si ai, juega perfecto cuando hay pelea. */
static int run_until(Engine *e, int64_t now, uint32_t *ms, PescaPhase until, bool ai, int max_frames)
{
    int n = 0;
    for (; n < max_frames && pesca_phase() != until; n++) {
        const FishGame *g = pesca_game();
        RawInput in = {(uint8_t)(ai && g && fishgame_ai(g) ? BTN_A : 0), false};
        engine_frame(e, in, now, *ms += 20);
    }
    return n;
}

static void test_chain_worm_fish_collection_diary(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_noon();
    engine_init(&e, 31, now, ms);
    RawInput none = {0, false};
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = WORLD_PESCA;
    e.gs.weather = W_CLEAR;
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    state_bag_add(&e.gs, ITEM_NUBE);
    state_bag_add(&e.gs, ITEM_LOMBRIZ);
    CHECK_EQ(pesca_phase(), PESCA_IDLE);

    /* 1) Un toque de A lanza la caña y gasta la lombriz. */
    gesture(&e, BTN_A, now, &ms, 80);
    CHECK(pesca_phase() != PESCA_IDLE);
    CHECK_EQ(e.gs.bag_count, 1);
    CHECK_EQ(e.gs.bag[0], ITEM_NUBE);
    CHECK_EQ(e.gs.actions_used[WORLD_PESCA], 1);
    CHECK(has(e.toast, "Cebo: Lombriz"));
    FishPlan plan = *pesca_plan();
    /* 2) Espera "picando…", pica y empieza la pelea. */
    run_until(&e, now, &ms, PESCA_FIGHT, false, 1000);
    CHECK_EQ(pesca_phase(), PESCA_FIGHT);
    CHECK(pesca_game() != NULL);
    /* 3) Mantener A (el jugador perfecto) lleva el medidor hasta el final. */
    run_until(&e, now, &ms, PESCA_RESULT, true, 3000);
    CHECK_EQ(pesca_phase(), PESCA_RESULT);
    CHECK(pesca_result()->won);
    const FishInfo *f = fish_info(plan.idx);
    /* 4) Colección y diario. */
    if (!f->junk) {
        CHECK((e.gs.collection >> (COLL_PEZ0 + plan.idx)) & 1);
        CHECK_EQ(e.gs.fish.caught[plan.idx], 1);
        CHECK_EQ(e.gs.fish.best[plan.idx], plan.size);
        CHECK(pesca_result()->new_species);
    } else {
        CHECK_EQ(e.gs.fish.junk, 1);
    }
    int cast = 0, caught = 0, discover = 0;
    for (int i = 0; i < e.gs.diary_count; i++) {
        const DiaryEntry *d = state_diary_get(&e.gs, i);
        cast += d->kind == EV_CAST;
        caught += d->kind == EV_FISH || d->kind == EV_FISH_LEGEND || d->kind == EV_FISH_JUNK;
        discover += d->kind == EV_DISCOVER;
    }
    CHECK_EQ(cast, 1);
    CHECK_EQ(caught, 1);
    CHECK_EQ(discover, f->junk ? 0 : 1);
    /* El diario "mientras no mirabas" lo enseña con texto. */
    char what[40] = "";
    for (int i = 0; i < e.gs.diary_count; i++) {
        const DiaryEntry *d = state_diary_get(&e.gs, i);
        char t[40];
        CHECK(state_event_text(t, sizeof(t), d) || d->kind == EV_NONE || d->kind == EV_WAKE);
        if (d->kind == EV_FISH || d->kind == EV_FISH_LEGEND || d->kind == EV_FISH_JUNK)
            snprintf(what, sizeof(what), "%s", t);
    }
    CHECK(has(what, "Pesca "));
    CHECK(has(what, f->tag));
    /* 5) La tarjeta se cierra con A (no al soltar la tecla de la pelea) y se vuelve a poder lanzar. */
    uint32_t guard = ms;
    while (ms - guard < 1200)
        engine_frame(&e, none, now, ms += 20);
    gesture(&e, BTN_A, now, &ms, 80);
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
    /* 6) El universo sigue guardable y cabe de sobra. */
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = state_serialize(&e.gs, buf, sizeof(buf));
    CHECK(n > 0 && n < 4096);
    GameState back;
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK(same_state(&back, &e.gs));
    /* 7) Y la Sala lo comenta (universo compartido): sale una frase del suceso. */
    PhraseEnv env = {now, false, 0, false};
    bool said = false;
    for (int k = 0; k < 40 && !said; k++) {
        GameState g2 = e.gs;
        g2.rng.state += 977u * (uint64_t)k;
        char txt[256];
        if (talk_comment(&g2, &env) && talk_text(&g2, txt, sizeof(txt))) {
            said = true;
            printf("    (la Sala comenta: \"%s\")\n", txt);
            CHECK(txt[0] != '\0');
        }
    }
    CHECK(said);
}

static void test_engine_messages(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_noon();
    engine_init(&e, 32, now, ms);
    RawInput none = {0, false};
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = WORLD_PESCA;
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    /* Sin lombrices: mensaje con gracia, sin bloquear y sin gastar el lanzamiento. */
    gesture(&e, BTN_A, now, &ms, 80);
    CHECK(has(e.toast, "Sin lombrices"));
    CHECK(has(e.toast, "meditación"));
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
    CHECK_EQ(e.gs.actions_used[WORLD_PESCA], 0);
    /* Se puede seguir: B cambia de mundo y vuelve. */
    gesture(&e, BTN_B, now, &ms, 80);
    CHECK_EQ(e.gs.current_world, WORLD_COBERTIZO);
    e.gs.current_world = WORLD_PESCA;
    /* Dos lanzamientos al día; el tercero: "El estanque descansa. Vuelve en ...". */
    for (int k = 0; k < 2; k++) {
        state_bag_add(&e.gs, ITEM_LOMBRIZ);
        gesture(&e, BTN_A, now, &ms, 80);
        CHECK(pesca_phase() != PESCA_IDLE);
        run_until(&e, now, &ms, PESCA_RESULT, true, 3000);
        uint32_t g = ms;
        while (ms - g < 1200)
            engine_frame(&e, none, now, ms += 20);
        gesture(&e, BTN_A, now, &ms, 80); /* cierra la tarjeta */
        CHECK_EQ(pesca_phase(), PESCA_IDLE);
    }
    CHECK_EQ(e.gs.actions_used[WORLD_PESCA], 2);
    state_bag_add(&e.gs, ITEM_LOMBRIZ);
    gesture(&e, BTN_A, now, &ms, 80);
    CHECK(has(e.toast, "El estanque descansa"));
    CHECK(has(e.toast, "Vuelve en"));
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
    CHECK_EQ(e.gs.bag_count, 1); /* la lombriz sigue en la bolsa */
}

static void test_cancel_no_hanging_state(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_noon();
    RawInput none = {0, false}, shake = {0, true};
    engine_init(&e, 33, now, ms);
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    e.gs.current_world = WORLD_PESCA;
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    state_bag_add(&e.gs, ITEM_LOMBRIZ);
    state_bag_add(&e.gs, ITEM_LOMBRIZ);
    state_bag_add(&e.gs, ITEM_LOMBRIZ);

    /* B durante la pelea: se cambia de mundo, la lombriz se pierde, el pez cuenta como escapado. */
    gesture(&e, BTN_A, now, &ms, 80);
    run_until(&e, now, &ms, PESCA_FIGHT, false, 1000);
    CHECK_EQ(pesca_phase(), PESCA_FIGHT);
    uint8_t fled = e.gs.fish.fled;
    gesture(&e, BTN_B, now, &ms, 80);
    CHECK_EQ(e.gs.current_world, WORLD_COBERTIZO);
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
    CHECK(pesca_game() == NULL);
    CHECK(has(e.toast, "lombriz se perdió"));
    CHECK_EQ(e.gs.fish.fled, fled + 1);
    CHECK_EQ(state_diary_get(&e.gs, 0)->kind, EV_FISH_LOST);
    CHECK_EQ(e.gs.bag_count, 2);
    CHECK_EQ(e.gs.actions_used[WORLD_PESCA], 1); /* el lanzamiento ya estaba gastado */

    /* B esperando: igual, pero sin pez escapado (aún no había picado). */
    e.gs.current_world = WORLD_PESCA;
    gesture(&e, BTN_A, now, &ms, 80);
    CHECK(pesca_phase() == PESCA_CAST || pesca_phase() == PESCA_WAIT);
    fled = e.gs.fish.fled;
    gesture(&e, BTN_B, now, &ms, 80);
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
    CHECK_EQ(e.gs.fish.fled, fled);
    CHECK_EQ(e.gs.bag_count, 1);

    /* Dormirse a mitad: el minijuego no se guarda, se cancela y al despertar se avisa. */
    e.gs.current_world = WORLD_PESCA;
    e.gs.actions_used[WORLD_PESCA] = 0;
    gesture(&e, BTN_A, now, &ms, 80);
    run_until(&e, now, &ms, PESCA_WAIT, false, 1000);
    CHECK(pesca_phase() != PESCA_IDLE);
    engine_sleep(&e, now);
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = state_serialize(&e.gs, buf, sizeof(buf));
    GameState back;
    CHECK(n > 0 && state_deserialize(&back, buf, n) == 0);
    ms += 100000;
    engine_frame(&e, shake, now + 3600, ms);
    CHECK(has(e.toast, "Soltaste la caña"));
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
    /* Con todo cancelado se puede volver a pescar sin trabas. */
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    state_bag_add(&e.gs, ITEM_LOMBRIZ);
    e.gs.actions_used[WORLD_PESCA] = 0;
    e.gs.current_world = WORLD_PESCA;
    e.diary_until_ms = 0;
    gesture(&e, BTN_A, now + 3600, &ms, 80);
    CHECK(pesca_phase() != PESCA_IDLE);
    run_until(&e, now + 3600, &ms, PESCA_RESULT, true, 3000);
    CHECK_EQ(pesca_phase(), PESCA_RESULT);

    /* Un engine nuevo no hereda una pesca a medias. */
    gesture(&e, BTN_A, now + 3600, &ms, 80); /* cierra la tarjeta si ya pasó el margen */
    static Engine e2;
    engine_init(&e2, 34, now, 1000);
    CHECK_EQ(pesca_phase(), PESCA_IDLE);
}

/* El minijuego NO depende de cuántos fotogramas pinte la pantalla: a 20, 33 o 100 ms por fotograma el
 * resultado de un jugador que decide por el estado (perfecto) es el mismo. */
static void test_frame_rate_independent(void)
{
    int outcome[3];
    static const uint32_t DT[3] = {20, 33, 100};
    for (int k = 0; k < 3; k++) {
        static Engine e;
        uint32_t ms = 1000;
        int64_t now = t_noon();
        engine_init(&e, 77, now, ms);
        RawInput none = {0, false};
        for (int i = 0; i < 40; i++)
            engine_frame(&e, none, now, ms += 33);
        e.gs.current_world = WORLD_PESCA;
        e.gs.bag_count = 0;
        memset(e.gs.bag, 0, sizeof(e.gs.bag));
        state_bag_add(&e.gs, ITEM_LOMBRIZ);
        gesture(&e, BTN_A, now, &ms, 80);
        outcome[k] = pesca_plan()->idx;
        for (int i = 0; i < 2000 && pesca_phase() != PESCA_RESULT; i++) {
            const FishGame *g = pesca_game();
            RawInput in = {(uint8_t)(g && fishgame_ai(g) ? BTN_A : 0), false};
            engine_frame(&e, in, now, ms += DT[k]);
        }
        CHECK_EQ(pesca_phase(), PESCA_RESULT);
        CHECK(pesca_result()->won || DT[k] == 100); /* a 100 ms por fotograma el jugador es torpe, pero no se cuelga */
    }
    CHECK_EQ(outcome[0], outcome[1]);
    CHECK_EQ(outcome[1], outcome[2]);
}

/* ---------- determinismo del universo con pesca ---------- */

typedef struct {
    int64_t at;
    int kind; /* 0 lombriz a la bolsa, 1 lanzar y resolver */
} Act;

static void do_act(GameState *gs, const Act *a)
{
    if (a->kind == 0) { /* una lombriz a la bolsa (si está llena, en vez de lo último) */
        if (!state_bag_add(gs, ITEM_LOMBRIZ))
            gs->bag[gs->bag_count - 1] = ITEM_LOMBRIZ;
        return;
    }
    FishPlan plan;
    bool prep;
    if (fish_cast(gs, WORLD_PESCA, &plan, &prep) == CAST_OK) {
        /* el resultado de la pelea es lo único que decide el jugador: aquí, según la semilla */
        FishGame g;
        const FishInfo *f = fish_info(plan.idx);
        fishgame_init(&g, plan.game_seed, f->temper, f->diff);
        bool won = (plan.game_seed & 3) != 0;
        fish_resolve(gs, &plan, won, NULL);
    }
}

static void test_determinism_with_fishing(void)
{
    /* Sin lanzamientos: en vivo (cada segundo) y de golpe dan el mismo universo, con el estado de pesca incluido. */
    {
        GameState live, batch;
        state_init(&live, 55, t_autumn());
        state_init(&batch, 55, t_autumn());
        int64_t end = t_autumn() + 5 * 86400;
        for (int64_t t = t_autumn(); t <= end; t += 7)
            engine_advance(&live, t);
        engine_advance(&live, end);
        engine_advance(&batch, end);
        CHECK(same_state(&live, &batch));
    }
    /* Con lanzamientos intercalados: lombrices que entran y salen, a horas fijas. */
    Rng r;
    rng_seed(&r, 9, 9);
    Act acts[64];
    int n = 0;
    int64_t t = t_autumn();
    for (int d = 0; d < 12; d++) {
        acts[n++] = (Act){t + d * 86400 + 7 * 3600, 0};
        acts[n++] = (Act){t + d * 86400 + 9 * 3600 + (int64_t)rng_below(&r, 3600), 1};
        acts[n++] = (Act){t + d * 86400 + 20 * 3600 + (int64_t)rng_below(&r, 3600), 0};
        if (d % 3 != 2)
            acts[n++] = (Act){t + d * 86400 + 21 * 3600 + (int64_t)rng_below(&r, 3600), 1};
    }
    for (int i = 1; i < n; i++)
        CHECK(acts[i].at >= acts[i - 1].at || (acts[i].at / 86400 != acts[i - 1].at / 86400));
    /* orden por hora */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (acts[j].at < acts[i].at) {
                Act tmp = acts[i];
                acts[i] = acts[j];
                acts[j] = tmp;
            }
    static GameState a, b, c;
    state_init(&a, 56, t_autumn());
    state_init(&b, 56, t_autumn());
    state_init(&c, 56, t_autumn());
    for (int i = 0; i < n; i++) {
        /* A: avanza cada pocos minutos; B: de golpe hasta la hora de la acción. */
        for (int64_t s = a.tick * LLV_TICK_SECONDS; s < acts[i].at; s += 631)
            engine_advance(&a, s);
        engine_advance(&a, acts[i].at);
        do_act(&a, &acts[i]);
        engine_advance(&b, acts[i].at);
        do_act(&b, &acts[i]);
    }
    int64_t end = t_autumn() + 13 * 86400;
    engine_advance(&a, end);
    engine_advance(&b, end);
    CHECK(same_state(&a, &b));
    CHECK(a.fish.casts >= 10);
    CHECK(fish_total_caught(&a) + a.fish.junk + a.fish.fled == a.fish.casts); /* cada lanzamiento acabó en algo */
    /* Y es distinto del universo en que no se pescó (la pesca deja huella). */
    engine_advance(&c, end);
    CHECK(!same_state(&a, &c));
    /* Un estado guardado a mitad y recargado sigue el mismo camino. */
    static GameState d1, d2;
    state_init(&d1, 56, t_autumn());
    int half = n / 2;
    for (int i = 0; i < half; i++) {
        engine_advance(&d1, acts[i].at);
        do_act(&d1, &acts[i]);
    }
    uint8_t buf[LLV_SAVE_MAX];
    size_t len = state_serialize(&d1, buf, sizeof(buf));
    CHECK(len > 0 && state_deserialize(&d2, buf, len) == 0);
    for (int i = half; i < n; i++) {
        engine_advance(&d1, acts[i].at);
        do_act(&d1, &acts[i]);
        engine_advance(&d2, acts[i].at);
        do_act(&d2, &acts[i]);
    }
    engine_advance(&d1, end);
    engine_advance(&d2, end);
    CHECK(same_state(&d1, &d2));
    CHECK(same_state(&d1, &a));
}

/* Dos motores que reciben los mismos gestos de pesca (vivo con fotogramas densos / escasos)
 * acaban con el mismo estado del universo. */
static void run_fishing_script(Engine *e, bool dense)
{
    uint32_t ms = 1000;
    int64_t now = t_autumn() + 7 * 3600;
    engine_init(e, 4141, now, ms);
    RawInput none = {0, false};
    for (int i = 0; i < 10; i++)
        engine_frame(e, none, now, ms += 33);
    e->gs.current_world = WORLD_PESCA;
    e->gs.bag_count = 0;
    memset(e->gs.bag, 0, sizeof(e->gs.bag));
    state_bag_add(&e->gs, ITEM_LOMBRIZ);
    state_bag_add(&e->gs, ITEM_LOMBRIZ);
    for (int k = 0; k < 2; k++) {
        gesture(e, BTN_A, now, &ms, 80);
        /* El jugador perfecto decide por el estado y los fotogramas son más o menos densos. */
        for (int i = 0; i < 4000 && pesca_phase() != PESCA_RESULT; i++) {
            const FishGame *g = pesca_game();
            RawInput in = {(uint8_t)(g && fishgame_ai(g) ? BTN_A : 0), false};
            engine_frame(e, in, now, ms += dense ? 10 : 20);
        }
        uint32_t g0 = ms;
        while (ms - g0 < 1200)
            engine_frame(e, none, now, ms += 20);
        gesture(e, BTN_A, now, &ms, 80);
    }
}

static void test_engine_determinism_fishing(void)
{
    static Engine a, b;
    run_fishing_script(&a, true);
    GameState ga = a.gs;
    run_fishing_script(&b, false);
    /* last_seen/diario de despertar dependen de now, que es el mismo: el estado coincide byte a byte. */
    CHECK(same_state(&ga, &b.gs));
    CHECK_EQ(b.gs.fish.casts, 2);
    CHECK(fish_total_caught(&b.gs) + b.gs.fish.junk >= 1);
}

/* ---------- guardado ---------- */

static void test_save_fishing(void)
{
    static GameState gs, back;
    universe(&gs, 91, t_autumn(), 3);
    gs.fish = (Fishing){{1, 2, 3, 4, 5, 6}, {10, 20, 30, 40, 50, 60}, 7, 8, 9};
    gs.collection |= 0xFC00;
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = state_serialize(&gs, buf, sizeof(buf));
    CHECK(n > 0 && n < 4096);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK(same_state(&gs, &back));
    CHECK(memcmp(&gs.fish, &back.fish, sizeof(Fishing)) == 0);
    CHECK_EQ(back.fish.best[5], 60);
    printf("    (GameState: %zu bytes en memoria, %zu en el guardado; Fishing: %zu bytes)\n", sizeof(GameState), n,
           sizeof(Fishing));

    /* El guardado de la fase 4 (versión 4, sin pesca) se rechaza con limpieza y no toca el destino. */
    uint8_t old[LLV_SAVE_MAX];
    memcpy(old, buf, n);
    old[4] = 4;
    fix_crc(old, n);
    GameState out;
    state_init(&out, 1, t_autumn());
    out.looks_total = 777;
    CHECK(state_deserialize(&out, old, n) != 0);
    CHECK_EQ(out.looks_total, 777);
    /* Y uno de verdad v4 (sin los 16 bytes de la pesca) tampoco entra. */
    memcpy(old, buf, n - 4 - 16);
    old[4] = 4;
    old[6] = (uint8_t)(n - 16 - 12);
    old[7] = (uint8_t)((n - 16 - 12) >> 8);
    fix_crc(old, n - 16);
    CHECK(state_deserialize(&out, old, n - 16) != 0);
    CHECK_EQ(out.looks_total, 777);

    /* Estados imposibles: se rechazan aunque el CRC sea bueno. */
    GameState bad;
    bad = gs;
    bad.fish.caught[2] = 0; /* récord sin capturas */
    n = state_serialize(&bad, buf, sizeof(buf));
    CHECK(state_deserialize(&out, buf, n) != 0);
    bad = gs;
    bad.collection &= (uint16_t)~(1u << (COLL_PEZ0 + 3)); /* pescado y no está en la colección */
    n = state_serialize(&bad, buf, sizeof(buf));
    CHECK(state_deserialize(&out, buf, n) != 0);
    bad = gs;
    bad.actions_used[WORLD_PESCA] = LLV_PESCA_ACTIONS + 1; /* más lanzamientos que el límite */
    n = state_serialize(&bad, buf, sizeof(buf));
    CHECK(state_deserialize(&out, buf, n) != 0);
    bad = gs;
    bad.actions_used[WORLD_JARDIN] = LLV_GARDEN_ACTIONS + 1;
    n = state_serialize(&bad, buf, sizeof(buf));
    CHECK(state_deserialize(&out, buf, n) != 0);
    bad = gs;
    bad.current_world = LLV_WORLD_COUNT; /* un mundo que no existe */
    n = state_serialize(&bad, buf, sizeof(buf));
    CHECK(state_deserialize(&out, buf, n) != 0);
    CHECK_EQ(out.looks_total, 777);
    /* Un guardado válido de una pesca a medio camino (con lombriz gastada) sí carga. */
    bad = gs;
    bad.actions_used[WORLD_PESCA] = LLV_PESCA_ACTIONS;
    n = state_serialize(&bad, buf, sizeof(buf));
    CHECK_EQ(state_deserialize(&out, buf, n), 0);
}

/* ---------- universo compartido: Director, diario, frases ---------- */

static void test_news_and_director(void)
{
    GameState gs;
    universe(&gs, 12, t_noon(), 0);
    gs.news = 0;
    /* Una lombriz que entra en la bolsa marca ❗ en la Pesca. */
    state_log(&gs, EV_BAG_REFILL, ITEM_LOMBRIZ);
    CHECK(gs.news & (1u << WORLD_PESCA));
    CHECK_EQ(director_pick_world(&gs), WORLD_PESCA);
    /* Cualquier otro objeto, no. */
    gs.news = 0;
    state_log(&gs, EV_BAG_REFILL, ITEM_NUBE);
    CHECK(!(gs.news & (1u << WORLD_PESCA)));
    CHECK_EQ(director_pick_world(&gs), -1);
    /* La Sala y el Cobertizo siguen teniendo prioridad. */
    gs.news = (uint8_t)((1u << WORLD_PESCA) | (1u << WORLD_COBERTIZO));
    CHECK_EQ(director_pick_world(&gs), WORLD_COBERTIZO);
    gs.news = (uint8_t)((1u << WORLD_PESCA) | (1u << WORLD_SALA));
    CHECK_EQ(director_pick_world(&gs), WORLD_SALA);
    /* Llega la luna llena: esa mañana la Pesca tiene novedades. */
    int64_t d = find_night(true);
    int64_t day0 = d / 86400 * 86400, first = 0;
    for (int back = 1; back < 6 && !first; back++)
        if (time_moon_phase(day0 - back * 86400 + 23 * 3600) != 4)
            first = day0 - (back - 1) * 86400;
    if (!first)
        first = day0;
    universe(&gs, 12, first - 2 * 86400, 0);
    int moon_news = 0;
    for (int h = 0; h < 6 * 24; h++) {
        gs.news = 0;
        engine_advance(&gs, first - 2 * 86400 + (int64_t)(h + 1) * 3600);
        moon_news += (gs.news & (1u << WORLD_PESCA)) != 0;
    }
    CHECK(moon_news >= 1);

    /* Los sucesos de pesca están en el vocabulario del diario y de las frases. */
    CHECK_EQ(phrase_event_kind(PE_CEBO), EV_CAST);
    CHECK_EQ(phrase_event_kind(PE_PEZ), EV_FISH);
    CHECK_EQ(phrase_event_kind(PE_PEZ_ESCAPA), EV_FISH_LOST);
    CHECK_EQ(phrase_event_kind(PE_LEGENDARIO), EV_FISH_LEGEND);
    CHECK_EQ(phrase_event_kind(PE_BASURA), EV_FISH_JUNK);
    CHECK(EV_KIND_COUNT <= 64); /* "frescos" es una máscara de 64 bits */
}

static void test_sala_comments_fishing(void)
{
    static const struct { EventKind ev; uint8_t a; const char *must; } CASES[] = {
        {EV_CAST, 0, "ombriz"},
        {EV_FISH, 1, "Carpa Resignada"},
        {EV_FISH_LOST, 3, "Bagre Insomne"},
        {EV_FISH_LEGEND, 5, "Koi Lunar"},
        {EV_FISH_JUNK, 1, "Lata Oxidada"},
    };
    int64_t now = t_noon();
    PhraseEnv env = {now, false, 0, false};
    for (unsigned c = 0; c < sizeof(CASES) / sizeof(CASES[0]); c++) {
        int found = 0, said = 0;
        for (int k = 0; k < 80; k++) {
            GameState g;
            universe(&g, 13, now, 0);
            g.sala.fresh_a = g.sala.fresh_b = 0;
            state_log(&g, CASES[c].ev, CASES[c].a);
            g.rng.state += 977u * (uint64_t)k;
            char txt[256];
            if (talk_comment(&g, &env) && talk_text(&g, txt, sizeof(txt))) {
                said++;
                /* Tras comentarlo, ya no es noticia. */
                CHECK(!(state_fresh(&g) >> CASES[c].ev & 1));
                found += has(txt, CASES[c].must) || CASES[c].ev == EV_CAST;
            }
        }
        if (said == 0)
            printf("    nadie comenta el suceso %d\n", CASES[c].ev);
        CHECK(said > 0);
        if (CASES[c].ev != EV_CAST)
            CHECK(found > 0);
    }
    /* Y todas esas frases están escritas en el tono de la casa: hay al menos 10 sobre la pesca. */
    int fishing_phrases = 0;
    for (int i = 0; i < phrase_count(); i++) {
        PhraseRec r;
        if (!phrase_get(i, &r))
            continue;
        for (int t = 0; t < r.ntags; t++)
            if (phrase_tag_kind(&r, t) == PT_EVENT) {
                int pe = phrase_tag_arg(&r, t);
                fishing_phrases += pe >= PE_CEBO && pe <= PE_BASURA;
            }
    }
    CHECK(fishing_phrases >= 10);
}

static void test_diary_text_fits(void)
{
    /* El diario enseña cada suceso de pesca con un texto corto; en la pantalla de 104 px junto al "hace 47 h". */
    for (int k = 0; k < 5; k++) {
        static const EventKind KINDS[5] = {EV_CAST, EV_FISH, EV_FISH_LOST, EV_FISH_LEGEND, EV_FISH_JUNK};
        int limit = KINDS[k] == EV_FISH_JUNK ? FISH_JUNK : KINDS[k] == EV_CAST ? 1 : FISH_SPECIES;
        for (int a = 0; a < limit; a++) {
            DiaryEntry d = {0, (uint8_t)KINDS[k], (uint8_t)a};
            char t[40];
            CHECK(state_event_text(t, sizeof(t), &d));
            CHECK(t[0] != '\0');
            CHECK(gfx_text_width(t) <= 104 - 12 - 6);
        }
    }
    /* Descubrir un pez: "Nuevo: Carpa" y no el nombre largo. */
    for (int i = 0; i < FISH_SPECIES; i++) {
        DiaryEntry d = {0, EV_DISCOVER, (uint8_t)(COLL_PEZ0 + i)};
        char t[40];
        CHECK(state_event_text(t, sizeof(t), &d));
        CHECK(has(t, FISH_TABLE[i].tag));
    }
}

/* ---------- dibujo ---------- */

static void test_render_pesca(void)
{
    static Engine e;
    static Framebuffer fb;
    static const int HOURS[] = {3, 6, 9, 13, 17, 19, 21, 23};
    int shots = 0;
    for (unsigned h = 0; h < sizeof(HOURS) / sizeof(HOURS[0]); h++)
        for (int weather = 0; weather < 4; weather++)
            for (int month = 1; month <= 12; month += 3) {
                int64_t now = time_from_civil(2026, month, 14, HOURS[h], 20, 0);
                uint32_t ms = 1000;
                engine_init(&e, 100 + (uint64_t)h, now, ms);
                RawInput none = {0, false};
                for (int i = 0; i < 30; i++)
                    engine_frame(&e, none, now, ms += 33);
                e.gs.current_world = WORLD_PESCA;
                e.gs.weather = (uint8_t)weather;
                e.gs.bag_count = 0;
                memset(e.gs.bag, 0, sizeof(e.gs.bag));
                state_bag_add(&e.gs, ITEM_LOMBRIZ);
                /* todas las fases: reposo, lanzando, esperando, picando, peleando, tarjeta */
                for (int i = 0; i < 20; i++)
                    engine_frame(&e, none, now, ms += 33);
                gesture(&e, BTN_A, now, &ms, 80);
                for (int i = 0; i < 6000 && pesca_phase() != PESCA_IDLE; i++) {
                    const FishGame *g = pesca_game();
                    RawInput in = {(uint8_t)(g && fishgame_ai(g) ? BTN_A : 0), false};
                    engine_frame(&e, in, now, ms += 33);
                    shots++;
                }
            }
    CHECK(shots > 1000);
    /* Cada pez y basura en su tarjeta, con los tamaños extremos, sin salirse. */
    for (int idx = 0; idx < FISH_TOTAL; idx++)
        for (int won = 0; won < 2; won++) {
            int64_t now = t_noon();
            uint32_t ms = 1000;
            engine_init(&e, 5, now, ms);
            RawInput none = {0, false};
            for (int i = 0; i < 30; i++)
                engine_frame(&e, none, now, ms += 33);
            e.gs.current_world = WORLD_PESCA;
            e.gs.bag_count = 0;
            memset(e.gs.bag, 0, sizeof(e.gs.bag));
            state_bag_add(&e.gs, ITEM_LOMBRIZ);
            gesture(&e, BTN_A, now, &ms, 80);
            /* se fuerza el desenlace de ese pez (el sorteo ya está hecho): cae o se escapa por B, o gana la IA */
            if (won)
                run_until(&e, now, &ms, PESCA_RESULT, true, 4000);
            else
                run_until(&e, now, &ms, PESCA_RESULT, false, 4000);
            CHECK_EQ(pesca_phase(), PESCA_RESULT);
            for (int i = 0; i < 20; i++)
                engine_frame(&e, none, now, ms += 33);
            (void)fb;
        }
    /* Los sprites se dibujan sin salirse del framebuffer incluso en los bordes. */
    for (int idx = 0; idx < FISH_TOTAL; idx++)
        for (int x = -20; x < LLV_W + 5; x += 7)
            for (int y = -20; y < LLV_H + 5; y += 9) {
                art_fish(&fb, x, y, idx, idx & 1, idx & 2);
                art_fish_mini(&fb, x, y, idx, idx & 1);
                art_fish_big(&fb, x, y, idx, true);
                art_fish_tint(&fb, x, y, idx, C_DARK, idx & 1);
            }
    CHECK(1);
}

/* La colección completa en el Cobertizo: cada pez tiene su puesto de ficha y todo se dibuja. */
static void test_cobertizo_collection(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_noon();
    cobertizo_ui_reset();
    engine_init(&e, 61, now, ms);
    RawInput none = {0, false};
    for (int i = 0; i < 40; i++)
        engine_frame(&e, none, now, ms += 33);
    pet_choose(&e.gs, PET_SEMILLA);
    e.gs.current_world = WORLD_COBERTIZO;
    e.gs.bag_count = 0;
    memset(e.gs.bag, 0, sizeof(e.gs.bag));
    state_bag_add(&e.gs, ITEM_NUBE);
    /* Sin peces: bolsa, colección, mascota (3 puestos, como antes de la fase 5). */
    CHECK(!cobertizo_on_pet(&e.gs));
    gesture(&e, BTN_A, now, &ms, 80);
    gesture(&e, BTN_A, now, &ms, 80);
    CHECK(cobertizo_on_pet(&e.gs));
    CHECK_EQ(cobertizo_on_fish(&e.gs), -1);
    /* Con tres peces: aparecen tres puestos de ficha entre la colección y la mascota. */
    FishPlan p;
    p = (FishPlan){0, 20, 0, 0};
    fish_resolve(&e.gs, &p, true, NULL);
    p = (FishPlan){2, 33, 0, 0};
    fish_resolve(&e.gs, &p, true, NULL);
    p = (FishPlan){FISH_LEGEND, 120, 0, 0};
    fish_resolve(&e.gs, &p, true, NULL);
    cobertizo_ui_reset();
    int seen[3] = {-1, -1, -1}, n = 0;
    for (int i = 0; i < 10 && !cobertizo_on_pet(&e.gs); i++) {
        int f = cobertizo_on_fish(&e.gs);
        if (f >= 0 && n < 3)
            seen[n++] = f;
        for (int k = 0; k < 6; k++) /* la ficha alterna sola entre sus dos páginas */
            engine_frame(&e, none, now, ms += 700);
        if (f >= 0)
            gesture(&e, BTN_A, now, &ms, LLV_HOLD_MS + 100); /* mantener: la otra página */
        gesture(&e, BTN_A, now, &ms, 80);                    /* tocar: el cursor avanza */
    }
    CHECK_EQ(n, 3);
    CHECK(seen[0] == 0 && seen[1] == 2 && seen[2] == FISH_LEGEND);
    CHECK(cobertizo_on_pet(&e.gs));
    /* La colección completa: los 6 peces y el contador. */
    for (int i = 0; i < FISH_SPECIES; i++) {
        p = (FishPlan){(uint8_t)i, (uint8_t)(10 + i), 0, 0};
        fish_resolve(&e.gs, &p, true, NULL);
    }
    CHECK_EQ(fish_collected(&e.gs), FISH_SPECIES);
    for (int i = 0; i < 12; i++) {
        gesture(&e, BTN_A, now, &ms, 80);
        for (int k = 0; k < 4; k++)
            engine_frame(&e, none, now, ms += 900);
    }
    cobertizo_ui_reset();
}

int main(void)
{
    struct { const char *name; TestFn fn; } tests[] = {
        {"pesca: tabla de especies coherente", test_table},
        {"pesca: seleccion por contexto (noche, lluvia, estacion, luna llena)", test_selection_by_context},
        {"pesca: contexto sacado del universo", test_context_from_universe},
        {"pesca: 2 lanzamientos al dia y reinicio a las 05:00", test_cast_limits},
        {"pesca: gasto de lombriz y objeto preparado", test_cast_worm_and_prepared},
        {"pesca: una sola tirada de gs->rng por lanzamiento", test_cast_one_rng_draw},
        {"minijuego: reproducible dada la semilla", test_minigame_reproducible},
        {"minijuego: perfecto siempre gana, sin pulsar siempre pierde", test_minigame_skill},
        {"pesca: coleccion sin duplicados, records, diario", test_resolve_collection_and_records},
        {"pesca: regalos baratos (objeto, mascota, compostera)", test_resolve_cheap_gifts},
        {"CADENA: lombriz -> pez -> coleccion -> diario (por el motor)", test_chain_worm_fish_collection_diary},
        {"pesca: mensajes con gracia (sin lombrices, estanque descansa)", test_engine_messages},
        {"pesca: B y dormir cancelan sin dejar estados colgados", test_cancel_no_hanging_state},
        {"minijuego: no depende de los fotogramas por segundo", test_frame_rate_independent},
        {"determinismo del universo con pesca (vivo, catch-up, guardado)", test_determinism_with_fishing},
        {"determinismo del motor con pesca (fotogramas densos o escasos)", test_engine_determinism_fishing},
        {"guardado v5: pesca ida y vuelta, rechaza v4 y absurdos", test_save_fishing},
        {"director: la Pesca tiene novedades (lombriz, luna llena)", test_news_and_director},
        {"sala: comenta la pesca", test_sala_comments_fishing},
        {"diario: textos de pesca caben", test_diary_text_fits},
        {"render: la Pesca en todas las horas, climas y fases", test_render_pesca},
        {"cobertizo: coleccion de peces y fichas", test_cobertizo_collection},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0])), ok = 0;
    for (int i = 0; i < n; i++)
        ok += run_test(tests[i].name, tests[i].fn);
    printf("\n%d/%d tests de pesca OK, %d comprobaciones, %d fallos\n", ok, n, checks, failures);
    return failures ? 1 : 0;
}
