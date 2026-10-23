/* 🎣 Pesca (spec §4.5): el estanque del jardín con un solo botón.
 * A (toque): lanza la caña (gasta una 🪱 lombriz de la bolsa; 2 lanzamientos al día). Tras una
 *   espera ("picando…") pica el pez y empieza el minijuego: MANTÉN A para subir la barra y
 *   mantener al pez dentro de la zona; al soltar, la barra cae. El medidor sube dentro de la
 *   zona y baja fuera: si se llena, lo pescas; si se vacía, se escapa.
 * B: cambia de mundo, como siempre. Si hay una pesca a medias, se cancela: la lombriz ya gastada se
 *   pierde (se avisa y el pez que picó cuenta como escapado). Dormir hace lo mismo.
 *
 * Todo lo de aquí que no es GameState es efímero y no se guarda. La lógica (selección, minijuego a
 * paso fijo, desenlace) vive en fish.c; aquí solo hay entrada, mensajes y dibujo. El minijuego se
 * mueve en pasos de LLV_FISH_STEP_MS con una semilla que sale una sola vez de gs->rng al lanzar. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../art.h"
#include "../fish.h"
#include "../garden.h"
#include "../input.h"
#include "../timeutil.h"
#include "../world.h"

#define WATER_Y 46          /* dónde empieza el agua */
#define WATER_BOTTOM 110
#define TIP_X 46            /* punta de la caña */
#define TIP_Y 58
#define BOB_X 70            /* dónde cae el flotador */
#define BOB_Y 72
#define CAST_MS 700
#define BITE_MS 700
#define RESULT_MS 7000

/* Gauge del minijuego (a la derecha): barra del jugador, pez y medidor de captura. */
#define G_X 100
#define G_TOP 16
#define G_BOT 104
#define G_H (G_BOT - G_TOP)
#define M_X 115

static struct {
    PescaPhase phase;
    uint32_t t0;            /* ms en que empezó la fase */
    FishPlan plan;
    FishGame game;
    uint32_t last_ms;
    uint32_t acc;
    FishResult res;
    bool won;
} S;

void pesca_ui_reset(void)
{
    memset(&S, 0, sizeof(S));
}

PescaPhase pesca_phase(void) { return S.phase; }
const FishGame *pesca_game(void) { return S.phase == PESCA_FIGHT ? &S.game : NULL; }
const FishPlan *pesca_plan(void) { return &S.plan; }
const FishResult *pesca_result(void) { return &S.res; }

void pesca_debug_force(int idx, int size)
{
    if (S.phase == PESCA_IDLE || S.phase == PESCA_RESULT || !fish_info(idx))
        return;
    S.plan.idx = (uint8_t)idx;
    S.plan.size = (uint8_t)(size > 0 ? size : fish_info(idx)->smax);
}

/* ---------- textos ---------- */

static void duration_text(char *buf, size_t n, int64_t secs) /* n >= 32 */
{
    int64_t m = (secs + 59) / 60;
    if (m < 1)
        m = 1;
    if (m >= 60)
        snprintf(buf, n, "%lld h %02lld min", (long long)(m / 60), (long long)(m % 60));
    else
        snprintf(buf, n, "%lld min", (long long)m);
}

static int64_t secs_to_next_day(int64_t now)
{
    int64_t key = time_day_key(now);
    return (key + 1) * LLV_SECONDS_PER_DAY + LLV_DAY_START_HOUR * 3600 - now;
}

static int worms(const GameState *gs)
{
    int n = 0;
    for (int i = 0; i < gs->bag_count; i++)
        n += gs->bag[i] == ITEM_LOMBRIZ;
    return n;
}

/* ---------- entrada ---------- */

static void start_cast(GameState *gs, const WorldCtx *ctx)
{
    char msg[96], when[32];
    bool prepared = false;
    switch (fish_cast(gs, ctx->index, &S.plan, &prepared)) {
    case CAST_OK:
        S.phase = PESCA_CAST;
        S.t0 = ctx->ms;
        snprintf(msg, sizeof(msg), "%s: Lombriz", prepared ? "Preparado" : "Cebo");
        world_toast(ctx, msg, 1600);
        world_tone(ctx, 330, 40);
        break;
    case CAST_NO_ACTIONS:
        duration_text(when, sizeof(when), secs_to_next_day(ctx->now));
        snprintf(msg, sizeof(msg), "El estanque descansa. Vuelve en %s.", when);
        world_toast(ctx, msg, 3200);
        break;
    case CAST_NO_WORM:
        world_toast(ctx, "Sin lombrices, la pesca es solo meditación.", 3200);
        break;
    }
}

static void pesca_tap(GameState *gs, const WorldCtx *ctx)
{
    if (ctx->absent)
        return;
    switch (S.phase) {
    case PESCA_IDLE:
        start_cast(gs, ctx);
        break;
    case PESCA_RESULT:
        if (ctx->ms - S.t0 >= 900) /* al soltar A tras ganar no se cierra la tarjeta sin querer */
            S.phase = PESCA_IDLE;
        break;
    default:
        break; /* lanzando, esperando o peleando: lo que cuenta es mantener A */
    }
}

static void pesca_hold(GameState *gs, const WorldCtx *ctx)
{
    /* Mantener A al empezar es ya el gesto del minijuego; fuera de él, vale como un toque. */
    pesca_tap(gs, ctx);
}

/* ---------- paso fijo ---------- */

static void finish(GameState *gs, const WorldCtx *ctx, bool won)
{
    fish_resolve(gs, &S.plan, won, &S.res);
    S.won = won;
    S.phase = PESCA_RESULT;
    S.t0 = ctx->ms;
    if (!won) {
        world_tone(ctx, 262, 80);
        world_tone(ctx, 196, 140);
    } else if (S.res.legend) {
        world_tone(ctx, 523, 70);
        world_tone(ctx, 659, 70);
        world_tone(ctx, 784, 70);
        world_tone(ctx, 1047, 240);
    } else if (S.res.new_species) {
        world_tone(ctx, 659, 60);
        world_tone(ctx, 880, 120);
    } else {
        world_tone(ctx, 784, 90);
    }
    if (S.res.complete)
        world_toast(ctx, "¡Peces completos!", 3600);
}

static void pesca_frame(GameState *gs, const WorldCtx *ctx)
{
    if (ctx->absent) /* "Salí. Vuelvo en 5 min.": el estanque también se toma su descanso */
        return;
    uint32_t ms = ctx->ms;
    switch (S.phase) {
    case PESCA_CAST:
        if (ms - S.t0 >= CAST_MS) {
            S.phase = PESCA_WAIT;
            S.t0 = ms;
            world_tone(ctx, 440, 30);
        }
        break;
    case PESCA_WAIT:
        if (ms - S.t0 >= S.plan.wait_ms) {
            S.phase = PESCA_BITE;
            S.t0 = ms;
            world_tone(ctx, 880, 50);
            world_tone(ctx, 1175, 90);
        }
        break;
    case PESCA_BITE:
        if (ms - S.t0 >= BITE_MS) {
            const FishInfo *f = fish_info(S.plan.idx);
            S.phase = PESCA_FIGHT;
            S.t0 = ms;
            S.last_ms = ms;
            S.acc = 0;
            fishgame_init(&S.game, S.plan.game_seed, f->temper, f->diff);
        }
        break;
    case PESCA_FIGHT: {
        uint32_t dt = ms - S.last_ms;
        S.last_ms = ms;
        if (dt > 100)
            dt = 100; /* tras una pausa no se recuperan los pasos perdidos */
        S.acc += dt;
        bool hold = (ctx->buttons & BTN_A) != 0;
        int st = FGS_PLAYING;
        while (S.acc >= LLV_FISH_STEP_MS && st == FGS_PLAYING) {
            st = fishgame_step(&S.game, hold);
            S.acc -= LLV_FISH_STEP_MS;
        }
        if (st != FGS_PLAYING)
            finish(gs, ctx, st == FGS_WON);
        break;
    }
    case PESCA_RESULT:
        if (ms - S.t0 >= RESULT_MS)
            S.phase = PESCA_IDLE;
        break;
    default:
        break;
    }
}

static const char *pesca_leave(GameState *gs)
{
    PescaPhase p = S.phase;
    if (p == PESCA_BITE || p == PESCA_FIGHT)
        fish_resolve(gs, &S.plan, false, NULL); /* el pez que picó se escapa y queda en el diario */
    pesca_ui_reset();
    if (p == PESCA_CAST || p == PESCA_WAIT || p == PESCA_BITE || p == PESCA_FIGHT)
        return "Soltaste la caña: la lombriz se perdió.";
    return NULL;
}

/* ---------- dibujo ---------- */

typedef enum { P_NIGHT, P_DAWN, P_DAY, P_DUSK } Period;

static Period period_of(int m)
{
    if (m < 330 || m >= 1230)
        return P_NIGHT;
    if (m < 450)
        return P_DAWN;
    if (m < 1080)
        return P_DAY;
    return P_DUSK;
}

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

static void line(Framebuffer *fb, int x0, int y0, int x1, int y1, Color c, int sag)
{
    int dx = x1 - x0, dy = y1 - y0;
    int n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (n == 0)
        n = 1;
    for (int i = 0; i <= n; i++) {
        int t = i * 1000 / n;
        int x = x0 + dx * i / n, y = y0 + dy * i / n + sag * 4 * t * (1000 - t) / 1000000;
        gfx_pset(fb, x, y, c);
    }
}

/* Un anillo de ondas (elipse vacía) que se abre sobre el agua. */
static void ring(Framebuffer *fb, int cx, int cy, int rx, int ry, Color c)
{
    int n = rx * 4 + 8;
    for (int i = 0; i < n; i++) {
        float a = 6.2831853f * (float)i / (float)n;
        gfx_pset(fb, cx + (int)lroundf(cosf(a) * (float)rx), cy + (int)lroundf(sinf(a) * (float)ry), c);
    }
}

static const Color SKY[4][4] = {
    {C_SKY_N0, C_SKY_N1, C_SKY_N2, C_SKY_N3},
    {C_SKY_S0, C_SKY_S1, C_SKY_S2, C_SKY_S3},
    {C_SKY_D0, C_SKY_D1, C_SKY_D2, C_SKY_D3},
    {C_SKY_N2, C_SKY_S0, C_SKY_S1, C_SKY_S2},
};
/* Agua: tres bandas (cerca del horizonte, media, honda), cada una con su color y su tramado. */
static const Color WATER[4][3][2] = {
    {{C_SKY_N3, C_SKY_N4}, {C_SKY_N2, C_SKY_N3}, {C_SKY_N1, C_SKY_N2}},
    {{C_SKY_S3, C_WATER_L}, {C_WATER_L, C_WATER}, {C_WATER, C_WATER_D}},
    {{C_WATER_L, C_WATER_HI}, {C_WATER, C_WATER_L}, {C_WATER_D, C_WATER}},
    {{C_SKY_S2, C_WATER}, {C_WATER_D, C_WATER}, {C_SKY_N2, C_WATER_D}},
};

static void sky_and_far(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx, Period p, int m,
                        Season season, bool rainy)
{
    for (int k = 0; k < 4; k++) {
        int y0 = LLV_HUD_H + (WATER_Y - LLV_HUD_H) * k / 4, y1 = LLV_HUD_H + (WATER_Y - LLV_HUD_H) * (k + 1) / 4;
        Color c = rainy && p != P_NIGHT ? (k < 2 ? C_GREY : C_GREY_L) : SKY[p][k];
        gfx_rect(fb, 0, y0, LLV_W, y1 - y0, c);
        if (k < 3) { /* dithering entre bandas */
            Color n = rainy && p != P_NIGHT ? (k + 1 < 2 ? C_GREY : C_GREY_L) : SKY[p][k + 1];
            for (int i = 0; i < LLV_W; i += 2)
                gfx_pset(fb, i + (y1 & 1), y1 - 1, n);
        }
    }
    bool fullmoon = time_moon_phase(ctx->now) == 4;
    if (!rainy && (p == P_NIGHT || p == P_DAWN || p == P_DUSK))
        for (int i = 0; i < 16; i++) {
            uint32_t h = hash((uint32_t)i * 13 + 5);
            if ((ctx->ms / 450 + (uint32_t)i) % 6 != 0)
                gfx_pset(fb, (int)(h % LLV_W), LLV_HUD_H + 1 + (int)((h >> 8) % 24), i % 3 ? C_MOON : C_STAR_BLUE);
        }
    /* sol o luna sobre su arco */
    if (!rainy) {
        if (m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN) {
            float f = (float)(m - LLV_SUNRISE_MIN) / (float)(LLV_SUNSET_MIN - LLV_SUNRISE_MIN);
            int sx = 8 + (int)(f * 80.0f), sy = 31 - (int)(sinf(3.14159f * f) * 19.0f);
            gfx_ellipse(fb, sx, sy, 4, 4, C_SUN);
            gfx_ellipse(fb, sx - 1, sy - 1, 2, 2, C_SUN_CORE);
        } else {
            int nm = m >= LLV_SUNSET_MIN ? m - LLV_SUNSET_MIN : m + 1440 - LLV_SUNSET_MIN;
            float f = (float)nm / (float)(1440 - (LLV_SUNSET_MIN - LLV_SUNRISE_MIN));
            int mx = 8 + (int)(f * 80.0f), my = 31 - (int)(sinf(3.14159f * f) * 19.0f);
            int ph = time_moon_phase(ctx->now);
            if (fullmoon) /* halo con dithering: esta noche pica el legendario */
                for (int y = -7; y <= 7; y++)
                    for (int x = -7; x <= 7; x++)
                        if (x * x + y * y <= 49 && ((x + y) & 1) == 0)
                            gfx_pset(fb, mx + x, my + y, C_SKY_N4);
            /* la luna con su fase: el disco menos otro disco desplazado (la parte a oscuras) */
            static const int8_t OCC[8] = {0, -2, -4, -6, 0, 6, 4, 2}; /* 0 nueva, 4 llena; creciente a la derecha */
            for (int y = -4; y <= 4; y++)
                for (int x = -4; x <= 4; x++) {
                    if (x * x + y * y > 17)
                        continue;
                    if (ph == 0)
                        gfx_pset(fb, mx + x, my + y, C_SKY_N2); /* luna nueva: apenas un disco oscuro */
                    else if (ph == 4 || (x - OCC[ph]) * (x - OCC[ph]) + y * y > 17)
                        gfx_pset(fb, mx + x, my + y, C_MOON);
                }
        }
    }
    /* nubes: blancas de día, grises y bajas con lluvia */
    if (p == P_DAY || p == P_DAWN || p == P_DUSK || rainy) {
        Color cc = rainy ? C_GREY_D : (p == P_DAY ? C_WHITE : C_SKY_S4);
        for (int i = 0; i < (rainy ? 4 : 2); i++) {
            int x = (int)(((ctx->ms / 120) + (uint32_t)i * 53) % 150) - 20, y = 14 + i * 7;
            gfx_ellipse(fb, x, y, 9, 2, cc);
            gfx_ellipse(fb, x + 5, y - 2, 6, 2, cc);
        }
    }
    /* orilla lejana según la estación (de noche, siluetas) */
    static const Color BANK[4] = {C_GREEN_L, C_GREEN, C_GRASS_A, C_SNOW};
    Color bc = p == P_NIGHT ? C_HILL_N1 : BANK[season];
    for (int x = 0; x < LLV_W; x++) {
        int hy = WATER_Y - 5 + (int)(2.0f * sinf((float)x * 0.17f + 0.5f)) + (int)(hash((uint32_t)x / 5) % 2);
        gfx_rect(fb, x, hy, 1, WATER_Y - hy, bc);
        if (p != P_NIGHT)
            gfx_pset(fb, x, hy, season == SEASON_WINTER ? C_SNOW_D : C_GREEN_D);
    }
    /* algún árbol lejano */
    for (int i = 0; i < 4; i++) {
        int x = 12 + i * 30 + (int)(hash((uint32_t)i + 77) % 9);
        gfx_ellipse(fb, x, WATER_Y - 9, 4, 4, p == P_NIGHT ? C_HILL_N2 : (season == SEASON_AUTUMN ? C_LEAF : C_GREEN_D));
        gfx_rect(fb, x, WATER_Y - 6, 1, 3, p == P_NIGHT ? C_HILL_N2 : C_GROUND_D);
    }
    (void)gs;
}

/* El agua: bandas con dithering, ondas de 4 fotogramas y el reflejo del sol o la luna. */
static void water(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx, Period p, int m, bool rainy)
{
    static const int B[4] = {WATER_Y, 62, 86, WATER_BOTTOM};
    for (int k = 0; k < 3; k++) {
        Color c1 = WATER[p][k][0], c2 = WATER[p][k][1];
        if (rainy && p != P_NIGHT) {
            c1 = k == 0 ? C_GREY : C_WATER_D;
            c2 = k == 0 ? C_WATER : C_WATER_D;
        }
        gfx_rect(fb, 0, B[k], LLV_W, B[k + 1] - B[k], c1);
        /* tramado en el tercio de la banda más cercano a la siguiente */
        int d0 = B[k + 1] - 5;
        for (int y = d0; y < B[k + 1]; y++)
            for (int x = 0; x < LLV_W; x++)
                if (((x + y) & 1) == 0 || (y > d0 + 2 && (x & 1) == 0))
                    gfx_pset(fb, x, y, k < 2 ? WATER[p][k + 1][0] : c1);
        /* y un poco de la siguiente luz arriba */
        for (int x = 0; x < LLV_W; x++)
            if (((x + B[k]) & 3) == 0)
                gfx_pset(fb, x, B[k], c2);
    }
    uint32_t frame = (ctx->ms / 320) % 4;
    Color hi = p == P_NIGHT ? C_SKY_N4 : C_WATER_HI;
    Color lo = p == P_NIGHT ? C_SKY_N3 : C_WATER_D;
    for (int i = 0; i < 30; i++) { /* ondas: se desplazan y parpadean en 4 fotogramas */
        uint32_t h = hash((uint32_t)i * 31 + 9);
        int y = WATER_Y + 3 + (int)(h % (uint32_t)(WATER_BOTTOM - WATER_Y - 6));
        int x = (int)((h >> 8) % LLV_W) + (int)frame - 1 + (y - WATER_Y) / 16;
        int len = 2 + (int)((h >> 3) % 3);
        if (((ctx->ms / 320 + (uint32_t)i) % 4) < 3)
            for (int k = 0; k < len; k++)
                gfx_pset(fb, x + k, y, k == 1 ? hi : lo);
    }
    /* reflejo del sol o de la luna: manchas con dithering que se parten con las ondas */
    if (!rainy) {
        int rx = -1;
        Color rc = C_SUN_CORE;
        if (m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN) {
            float f = (float)(m - LLV_SUNRISE_MIN) / (float)(LLV_SUNSET_MIN - LLV_SUNRISE_MIN);
            rx = 8 + (int)(f * 80.0f);
            rc = p == P_DAY ? C_SUN_CORE : C_SKY_S4;
        } else if (p == P_NIGHT || p == P_DUSK || p == P_DAWN) {
            int nm = m >= LLV_SUNSET_MIN ? m - LLV_SUNSET_MIN : m + 1440 - LLV_SUNSET_MIN;
            float f = (float)nm / (float)(1440 - (LLV_SUNSET_MIN - LLV_SUNRISE_MIN));
            rx = 8 + (int)(f * 80.0f);
            rc = C_MOON;
        }
        if (rx >= 0)
            for (int y = WATER_Y + 1; y < WATER_BOTTOM - 4; y++) {
                int w = 5 - (y - WATER_Y) / 14;
                if (w < 1)
                    w = 1;
                int off = (int)(ctx->ms / 260 + (uint32_t)y) % 3 - 1;
                if (((y + (int)frame) & 1) == 0)
                    for (int k = -w; k <= w; k++)
                        if (((k + y) & 1) == 0)
                            gfx_pset(fb, rx + k + off, y, rc);
            }
    }
    /* sombras de peces que pasan por debajo (solo adorno) */
    for (int i = 0; i < 2; i++) {
        int x = (int)((ctx->ms / (40 + (uint32_t)i * 17) + (uint32_t)i * 71) % 190) - 36;
        int y = 82 + i * 12;
        int idx = (int)((ctx->ms / 9000 + (uint32_t)i) % FISH_SPECIES);
        art_fish_tint(fb, x, y, idx, p == P_NIGHT ? C_SKY_N1 : C_WATER_D, i == 1);
    }
    (void)gs;
}

static void reeds_and_lilies(Framebuffer *fb, const WorldCtx *ctx, Period p, Season season)
{
    Color rc = p == P_NIGHT ? C_HILL_N2 : (season == SEASON_WINTER ? C_GRASS_A_D : C_REED);
    Color rl = p == P_NIGHT ? C_HILL_N1 : (season == SEASON_AUTUMN ? C_LEAF : C_REED_L);
    static const int8_t SWAY[4] = {0, 1, 0, -1};
    for (int i = 0; i < 6; i++) {
        int x = 80 + i * 4 + (i & 1), h = 9 + (i % 3) * 3;
        int sw = SWAY[(ctx->ms / 500 + (uint32_t)i) & 3];
        for (int y = 0; y < h; y++)
            gfx_pset(fb, x + (y > h / 2 ? sw : 0), WATER_BOTTOM - y, y > h - 4 ? rl : rc);
        if (i % 2 == 0)
            gfx_rect(fb, x + sw - 1, WATER_BOTTOM - h - 2, 2, 3, p == P_NIGHT ? C_GROUND_D : C_GROUND);
    }
    if (p != P_NIGHT || season != SEASON_WINTER) { /* nenúfares */
        gfx_ellipse(fb, 88, 97, 4, 1, p == P_NIGHT ? C_HILL_N1 : C_GREEN);
        gfx_pset(fb, 89, 96, p == P_NIGHT ? C_PINK : C_PINK);
        gfx_ellipse(fb, 62, 100, 3, 1, p == P_NIGHT ? C_HILL_N1 : C_GREEN_D);
    }
}

/* El muelle de madera y quien pesca (silueta sencilla). */
static void dock_and_angler(Framebuffer *fb, const WorldCtx *ctx, Period p, Season season, bool casting)
{
    Color wood = p == P_NIGHT ? C_GROUND_D : C_WOOD_L, wood_d = p == P_NIGHT ? C_HOUSE : C_GROUND_D;
    gfx_rect(fb, 0, 84, 34, 5, wood);
    for (int x = 4; x < 34; x += 6)
        gfx_rect(fb, x, 84, 1, 5, wood_d);
    gfx_rect(fb, 0, 84, 34, 1, p == P_NIGHT ? C_GROUND : C_SKY_D4);
    gfx_rect(fb, 3, 89, 2, 21, wood_d); /* pilotes */
    gfx_rect(fb, 28, 89, 2, 21, wood_d);
    if (season == SEASON_WINTER)
        gfx_rect(fb, 0, 83, 34, 1, C_SNOW);
    /* el pescador sentado: sombrero de paja, camisa y botas colgando */
    int ax = 15 + (casting ? 1 : 0);
    gfx_rect(fb, ax, 75, 6, 9, p == P_NIGHT ? C_VIOLET : C_BLUE_TXT);  /* torso */
    gfx_rect(fb, ax + 1, 69, 5, 5, C_SKIN);                           /* cabeza */
    gfx_rect(fb, ax - 1, 68, 9, 2, C_LEAF);                           /* ala del sombrero */
    gfx_rect(fb, ax + 1, 65, 5, 3, C_LEAF);
    gfx_pset(fb, ax + 4, 71, C_DARK);                                 /* ojo */
    gfx_rect(fb, ax + 4, 83, 6, 2, p == P_NIGHT ? C_HOUSE : C_ROOF);  /* piernas hacia el agua */
    gfx_rect(fb, ax + 9, 83, 2, 5, C_ROOF);
    (void)ctx;
}

/* Palos de la caña y su línea: la punta es TIP_X, TIP_Y. */
static void rod(Framebuffer *fb, bool bent)
{
    line(fb, 24, 78, TIP_X, TIP_Y + (bent ? 3 : 0), C_GROUND, 0);
    line(fb, 24, 79, TIP_X, TIP_Y + (bent ? 4 : 1), C_GROUND_D, 0);
    gfx_pset(fb, TIP_X, TIP_Y + (bent ? 3 : 0), C_WHITE);
}

static void bobber(Framebuffer *fb, int x, int y)
{
    gfx_rect(fb, x - 1, y - 2, 3, 2, C_RED);
    gfx_rect(fb, x - 1, y, 3, 2, C_WHITE);
    gfx_pset(fb, x, y - 3, C_DARK);
}

static void ripples(Framebuffer *fb, uint32_t ms, int cx, int cy, bool strong, Color c)
{
    int n = strong ? 3 : 2;
    for (int k = 0; k < n; k++) {
        uint32_t t = (ms / 70 + (uint32_t)k * 6) % 18;
        if (t < 15)
            ring(fb, cx, cy, 2 + (int)t / 2 * 1, 1 + (int)t / 5, c);
    }
}

static void legend_sparkles(Framebuffer *fb, uint32_t ms, int cx, int cy)
{
    for (int i = 0; i < 10; i++) {
        uint32_t h = hash((uint32_t)i * 19 + 3);
        uint32_t t = (ms / 60 + h) % 30;
        int x = cx - 14 + (int)(h % 29), y = cy + 6 - (int)t;
        Color c = (t & 1) ? C_SUN_CORE : C_SUN;
        gfx_pset(fb, x, y, c);
        if (t < 10)
            gfx_pset(fb, x + 1, y, C_WHITE);
    }
}

static void gauge(Framebuffer *fb, const WorldCtx *ctx)
{
    const FishGame *g = &S.game;
    bool in = fishgame_in_zone(g);
    /* marco y fondo de agua */
    gfx_rect(fb, G_X - 1, G_TOP - 1, 14, G_H + 2, C_DARK);
    gfx_rect(fb, G_X, G_TOP, 12, G_H, C_SKY_N3);
    for (int y = G_TOP; y < G_BOT; y += 2)
        gfx_rect(fb, G_X + 1 + (y / 2 & 1), y, 1, 1, C_SKY_N4);
    /* la barra del jugador: la zona verde */
    int y_top = G_BOT - (g->bar + g->zone / 2) * G_H / 1000, y_bot = G_BOT - (g->bar - g->zone / 2) * G_H / 1000;
    gfx_rect(fb, G_X, y_top, 12, y_bot - y_top, in ? C_GREEN_L : C_GREEN);
    gfx_frame(fb, G_X, y_top, 12, y_bot - y_top, in ? C_WHITE : C_GREEN_D);
    /* el pez dentro del gauge */
    int fy = G_BOT - g->fpos * G_H / 1000;
    static const int8_t HALO[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int k = 0; k < 4; k++) /* un halo blanco: el pez se ve sobre la zona verde y sobre el agua */
        art_fish_mini_tint(fb, G_X + HALO[k][0], fy - 4 + HALO[k][1], S.plan.idx, C_WHITE);
    art_fish_mini(fb, G_X, fy - 4, S.plan.idx, true);
    /* medidor de captura */
    gfx_rect(fb, M_X - 1, G_TOP - 1, 5, G_H + 2, C_DARK);
    gfx_rect(fb, M_X, G_TOP, 3, G_H, C_GREY_D);
    int mh = g->meter * G_H / 1000;
    Color mc = g->meter < 250 ? C_RED : g->meter < 600 ? C_SUN : C_GREEN_L;
    gfx_rect(fb, M_X, G_BOT - mh, 3, mh, mc);
    (void)ctx;
}

static void card(Framebuffer *fb, const WorldCtx *ctx)
{
    const FishInfo *f = fish_info(S.plan.idx);
    bool legend = S.won && S.res.legend;
    int y = 48, h = 60;
    gfx_textbox(fb, 5, y, 110, h);
    if (legend && ((ctx->ms / 200) & 1))
        gfx_frame(fb, 5, y, 110, h, C_SUN);
    /* retrato del pez a doble tamaño sobre agua */
    gfx_rect(fb, 10, y + 7, 36, 28, C_WATER);
    gfx_dither(fb, 10, y + 7, 36, 4, C_WATER_L, C_WATER_HI);
    art_fish_big(fb, 12, y + 9, S.plan.idx, S.won);
    if (!S.won) {
        gfx_text(fb, 50, y + 7, "¡Se escapó!", C_RED);
        gfx_text_wrap(fb, 50, y + 17, 62, f->name, C_DARK);
        gfx_text_center(fb, y + 44, "Otra vez será.", C_GREY);
        return;
    }
    if (legend)
        gfx_text(fb, 50, y + 7, "¡LEYENDA!", ((ctx->ms / 250) & 1) ? C_RED : C_ORANGE);
    else if (S.res.new_species)
        gfx_text(fb, 50, y + 7, "¡NUEVO!", ((ctx->ms / 300) & 1) ? C_RED : C_ORANGE);
    else if (S.res.record)
        gfx_text(fb, 50, y + 7, "¡RÉCORD!", ((ctx->ms / 300) & 1) ? C_BLUE_TXT : C_ORANGE);
    else if (f->junk)
        gfx_text(fb, 50, y + 7, "Un trasto", C_GREY);
    else
        gfx_text(fb, 50, y + 7, "¡Pescado!", C_BLUE_TXT);
    int lines = gfx_text_wrap(fb, 50, y + 17, 62, f->name, C_DARK);
    if (!f->junk) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d cm", S.plan.size);
        gfx_text(fb, 50, y + 17 + lines * GFX_LINE_H, buf, C_BLUE_TXT);
    }
    /* lo que deja, sin aspavientos; si no deja nada, su humor */
    char extra[40];
    if (S.res.gift_item)
        snprintf(extra, sizeof(extra), "Trae: %s", state_item_name((ItemKind)S.res.gift_item));
    else if (S.res.compost)
        snprintf(extra, sizeof(extra), "Restos al compost");
    else if (S.res.joy)
        snprintf(extra, sizeof(extra), "Tu mascota sonríe");
    else
        snprintf(extra, sizeof(extra), "%s", f->humor);
    bool gift = S.res.gift_item || S.res.compost || S.res.joy;
    gfx_rect(fb, 8, y + 46, 104, 10, gift ? C_BLUE_TXT : C_WHITE);
    gfx_text_center(fb, y + 47, extra, gift ? C_WHITE : C_GREY);
}

static void strip(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx)
{
    gfx_rect(fb, 0, WATER_BOTTOM, LLV_W, LLV_H - WATER_BOTTOM, C_GROUND_D);
    gfx_rect(fb, 0, WATER_BOTTOM, LLV_W, 1, C_WOOD_L);
    const char *what;
    char buf[32];
    switch (S.phase) {
    case PESCA_CAST: what = "Lanzando…"; break;
    case PESCA_WAIT: {
        int dots = 1 + (int)(ctx->ms / 400 % 3);
        snprintf(buf, sizeof(buf), "Picando%.*s", dots, "...");
        what = buf;
        break;
    }
    case PESCA_BITE: what = S.plan.idx == FISH_LEGEND ? "¡¡BRILLA!!" : "¡PICA!"; break;
    case PESCA_FIGHT: what = "Mantén A: sube"; break;
    case PESCA_RESULT: what = "A: seguir"; break;
    default:
        what = gs->actions_used[ctx->index] >= LLV_PESCA_ACTIONS ? "Hoy, descanso" : "A: lanzar caña";
        break;
    }
    gfx_text(fb, 3, WATER_BOTTOM + 1, what, C_WHITE);
    snprintf(buf, sizeof(buf), "x%d", worms(gs));
    int w = gfx_text_width(buf);
    gfx_text(fb, LLV_W - 3 - w, WATER_BOTTOM + 1, buf, worms(gs) ? C_ITEM_LOMBRIZ : C_GREY);
    /* mini lombriz */
    int wx = LLV_W - 3 - w - 12;
    for (int k = 0; k < 8; k++)
        gfx_pset(fb, wx + k, WATER_BOTTOM + 4 + (k & 1), k & 1 ? C_MAGENTA : C_ITEM_LOMBRIZ);
}

static void rain_and_snow(Framebuffer *fb, const GameState *gs, uint32_t ms)
{
    if (gs->weather == W_RAIN) {
        for (int i = 0; i < 40; i++) {
            uint32_t h = hash((uint32_t)i * 977 + 13);
            int y = (int)(((ms / 11) * 2 + (h >> 8)) % (uint32_t)(WATER_BOTTOM - LLV_HUD_H));
            int x = (int)(h % 140) - 10 - y / 6 + 20;
            gfx_pset(fb, x, LLV_HUD_H + y, (i & 1) ? C_SKY_D2 : C_SKY_D3);
            gfx_pset(fb, x - 1, LLV_HUD_H + y + 1, C_SKY_D3);
        }
        for (int i = 0; i < 6; i++) { /* gotas que ondulan el agua */
            uint32_t h = hash((uint32_t)i * 53 + 1);
            uint32_t t = (ms / 90 + h) % 12;
            if (t < 8)
                ring(fb, 6 + (int)(h % 88), WATER_Y + 8 + (int)((h >> 9) % 50), 1 + (int)t / 2, (int)t / 4, C_WATER_HI);
        }
    } else if (gs->weather == W_SNOW) {
        for (int i = 0; i < 30; i++) {
            uint32_t h = hash((uint32_t)i * 311 + 7);
            int y = (int)(((ms / 50) + (h >> 8)) % (uint32_t)(WATER_BOTTOM - LLV_HUD_H));
            gfx_pset(fb, (int)(h % LLV_W), LLV_HUD_H + y, C_WHITE);
        }
    }
}

static void pesca_render(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb)
{
    CivilTime c = time_to_civil(ctx->now);
    int m = c.hour * 60 + c.minute;
    Season season = time_season(c.month, c.day, LLV_SOUTH_HEMISPHERE);
    Period p = period_of(m);
    bool rainy = gs->weather == W_RAIN;
    uint32_t ms = ctx->ms;

    sky_and_far(fb, gs, ctx, p, m, season, rainy);
    water(fb, gs, ctx, p, m, rainy);
    reeds_and_lilies(fb, ctx, p, season);
    bool bent = S.phase == PESCA_FIGHT;
    dock_and_angler(fb, ctx, p, season, S.phase == PESCA_CAST);
    rod(fb, bent);

    /* la línea y el flotador según la fase */
    uint32_t t = ms - S.t0;
    int bx = TIP_X + 3, by = TIP_Y + 14;
    int sag = 3;
    bool in_water = false;
    switch (S.phase) {
    case PESCA_CAST: {
        int u = (int)(t > CAST_MS ? CAST_MS : t) * 1000 / CAST_MS;
        bx = TIP_X + (BOB_X - TIP_X) * u / 1000;
        by = TIP_Y + (BOB_Y - TIP_Y) * u / 1000 - 22 * 4 * u * (1000 - u) / 1000000;
        sag = 0;
        in_water = u >= 1000;
        break;
    }
    case PESCA_WAIT:
        bx = BOB_X;
        by = BOB_Y + ((ms / 420) % 4 == 3 ? 1 : 0);
        in_water = true;
        break;
    case PESCA_BITE:
        bx = BOB_X + (int)((ms / 45) & 1);
        by = BOB_Y + 3;
        sag = 1;
        in_water = true;
        break;
    case PESCA_FIGHT: {
        const FishGame *g = &S.game;
        bx = 52 + (g->fpos - FISHGAME_MIN) * 34 / (FISHGAME_MAX - FISHGAME_MIN);
        by = BOB_Y + (fishgame_in_zone(g) ? 0 : (int)((ms / 80) & 1) * 2);
        sag = 0;
        in_water = true;
        break;
    }
    case PESCA_RESULT:
        bx = BOB_X;
        by = BOB_Y;
        sag = 3;
        in_water = true;
        break;
    default:
        bx = TIP_X + 4 + (int)((ms / 300) & 1);
        by = TIP_Y + 16;
        break;
    }
    /* de noche y a oscuras, la línea se ve más clara */
    line(fb, TIP_X, TIP_Y + (bent ? 3 : 0), bx, by - 3, p == P_NIGHT ? C_GREY_L : C_WHITE, sag);
    if (in_water) {
        if (S.phase == PESCA_WAIT || S.phase == PESCA_CAST)
            ripples(fb, ms, bx, by + 2, false, C_WATER_HI);
        if (S.phase == PESCA_BITE)
            ripples(fb, ms, bx, by + 2, true, C_WHITE);
        if (S.phase == PESCA_FIGHT) {
            const FishInfo *f = fish_info(S.plan.idx);
            const FishGame *g = &S.game;
            /* la sombra del pez hacia donde va, bajo el flotador */
            art_fish_tint(fb, bx - 9, by + 5, S.plan.idx, p == P_NIGHT ? C_SKY_N1 : C_WATER_D, g->fvel > 0);
            ripples(fb, ms, bx, by + 2, !fishgame_in_zone(g), fishgame_in_zone(g) ? C_WATER_HI : C_WHITE);
            if (f && S.plan.idx == FISH_LEGEND)
                legend_sparkles(fb, ms, bx, by);
        }
    }
    bobber(fb, bx, by);
    if (S.phase == PESCA_BITE) { /* ¡! sobre el flotador */
        Color c2 = ((ms / 90) & 1) ? C_RED : C_ORANGE;
        gfx_rect(fb, bx - 1, by - 14, 3, 6, c2);
        gfx_rect(fb, bx - 1, by - 6, 3, 2, c2);
    }
    rain_and_snow(fb, gs, ms);
    if (S.phase == PESCA_FIGHT)
        gauge(fb, ctx);
    if (S.phase == PESCA_RESULT) {
        if (S.won && S.res.legend)
            legend_sparkles(fb, ms, 60, 70);
        card(fb, ctx);
    }
    strip(fb, gs, ctx);
}

const World world_pesca = {
    .name = "Pesca",
    .daily_actions = LLV_PESCA_ACTIONS,
    .tick = NULL,
    .render = pesca_render,
    .on_tap = pesca_tap,
    .on_hold = pesca_hold,
    .frame = pesca_frame,
    .leave = pesca_leave,
};
