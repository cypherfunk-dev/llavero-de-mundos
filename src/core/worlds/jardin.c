/* 🌳 Jardín (spec §4.2). Cuadrícula de 7x6 casillas de 16 px sobre la pantalla de 120x120.
 * A (toque): usa un objeto de la bolsa (límite 2 al día). A (mantener): lupa, sin coste.
 * La simulación está en garden.c; aquí solo hay entrada, mensajes y dibujo.
 * Las animaciones usan solo ctx->ms: nunca tocan el estado. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../art.h"
#include "../garden.h"
#include "../timeutil.h"
#include "../world.h"

#define GX0 4
#define GY0 14
#define CELL 16
#define FIELD_BOTTOM (GY0 + LLV_GARDEN_ROWS * CELL) /* 110 */

/* Efímero, no se guarda: solo cambia lo que se ve unos segundos. */
static uint32_t lupa_until;
static int lupa_index;
static int lupa_cell = -1;

void jardin_ui_reset(void)
{
    lupa_until = 0;
    lupa_index = 0;
    lupa_cell = -1;
}

/* ---------- textos ---------- */

static const char *const SEASON_NAME[4] = {"Primavera", "Verano", "Otoño", "Invierno"};

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

/* ---------- entrada ---------- */

static void jardin_tap(GameState *gs, const WorldCtx *ctx)
{
    ItemKind used = ITEM_NONE;
    int prepared = 0;
    const char *effect = "";
    char msg[96], when[32];
    switch (garden_act(gs, ctx->index, &used, &prepared, &effect)) {
    case ACT_OK:
        snprintf(msg, sizeof(msg), "%s: %s\n%s", prepared ? "Preparado" : "Sacaste",
                 state_item_name(used), effect);
        world_toast(ctx, msg, 3200);
        break;
    case ACT_NO_ACTIONS:
        duration_text(when, sizeof(when), secs_to_next_day(ctx->now));
        snprintf(msg, sizeof(msg), "El jardín descansa. Vuelve en %s.", when);
        world_toast(ctx, msg, 3200);
        break;
    case ACT_NO_ITEMS:
        duration_text(when, sizeof(when), (int64_t)gs->refill_left * LLV_TICK_SECONDS);
        snprintf(msg, sizeof(msg), "La bolsa está vacía. +1 en %s.", when);
        world_toast(ctx, msg, 3200);
        break;
    }
}

/* Entidades en orden de casilla (fila a fila). Devuelve cuántas hay. */
static int ordered(const GameState *gs, const Entity **out)
{
    int n = 0;
    for (int c = 0; c < LLV_GARDEN_CELLS; c++) {
        const Entity *e = garden_at(gs, c);
        if (e)
            out[n++] = e;
    }
    return n;
}

static void jardin_hold(GameState *gs, const WorldCtx *ctx)
{
    const Entity *list[LLV_GARDEN_MAX];
    int n = ordered(gs, list);
    if (n == 0) {
        world_toast(ctx, "No hay nada que mirar.", 1800);
        return;
    }
    lupa_index = ms_pending(ctx->ms, lupa_until) ? (lupa_index + 1) % n : 0;
    lupa_until = ctx->ms + 5000;
    const Entity *e = list[lupa_index];
    lupa_cell = e->cell;
    /* Mirar de cerca también descubre: la lupa apunta lo que ve en la colección. */
    for (const GardenDiscover *d = GARDEN_DISCOVER; d->sp; d++)
        if (d->sp == e->sp && (d->st == ST_ANY || d->st == e->st))
            state_collect(gs, (CollSlot)d->slot);
}

/* ---------- dibujo ---------- */

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

static const int8_t WAVE[8] = {0, 1, 2, 1, 0, -1, -2, -1};

/* Tablas de color para la noche y el día gris: solo usan colores de la paleta maestra. */
typedef struct {
    uint8_t map[C_COUNT];
} Lut;
static Lut NIGHT_LUT, DIM_LUT;
static bool luts_ready;

static void build_luts(void)
{
    for (int i = 0; i < C_COUNT; i++)
        NIGHT_LUT.map[i] = DIM_LUT.map[i] = (uint8_t)i;
    static const uint8_t N[][2] = {
        {C_GREEN_L, C_SKY_N4}, {C_GREEN, C_SKY_N3}, {C_GREEN_D, C_SKY_N2},
        {C_GRASS_A, C_SKY_N3}, {C_GRASS_A_D, C_SKY_N2}, {C_GROUND, C_HILL_FAR_N},
        {C_GROUND_D, C_HILL_N2}, {C_WOOD_L, C_GREY_D}, {C_LEAF, C_GROUND_D}, {C_ORANGE, C_ROOF},
        {C_RED, C_ROOF}, {C_SNOW, C_GREY}, {C_SNOW_D, C_SKY_N4}, {C_WHITE, C_GREY_L},
        {C_PINK, C_VIOLET}, {C_SKY_D0, C_SKY_N3}, {C_SKY_D1, C_SKY_N4}, {C_SKY_D2, C_GREY},
        {C_SKY_D3, C_GREY_L}, {C_BIRD, C_SKY_N4}, {C_BIRD_L, C_GREY}, {C_STONE, C_GREY_D},
        {C_STONE_L, C_GREY}, {C_SUN, C_MOON}, {C_ITEM_ALPISTE, C_GREY}, {C_MAGENTA, C_VIOLET},
        {C_ITEM_LOMBRIZ, C_VIOLET}, {C_GREY, C_GREY_D}, {C_GREY_D, C_HOUSE}, {C_GREY_L, C_GREY},
        {C_APPLE_ROT, C_HILL_N1}, {C_SUN_CORE, C_MOON}, {C_CYAN, C_BLUE_TXT},
    };
    for (unsigned i = 0; i < sizeof(N) / sizeof(N[0]); i++)
        NIGHT_LUT.map[N[i][0]] = N[i][1];
    static const uint8_t D[][2] = {
        {C_GREEN_L, C_GREEN}, {C_GREEN, C_GREEN_D}, {C_GRASS_A, C_GRASS_A_D}, {C_SNOW, C_SNOW_D},
        {C_ORANGE, C_GROUND}, {C_LEAF, C_ORANGE}, {C_WHITE, C_GREY_L}, {C_SKY_D1, C_SKY_D0},
        {C_WOOD_L, C_GROUND}, {C_PINK, C_MAGENTA},
    };
    for (unsigned i = 0; i < sizeof(D) / sizeof(D[0]); i++)
        DIM_LUT.map[D[i][0]] = D[i][1];
    luts_ready = true;
}

/* level 1..3: tramado ordenado 2x2 (25 %, 50 %, 75 % de los píxeles) para los crepúsculos y
 * el nublado; 4: todos. Así el paso de día a noche se ve con dithering y no de golpe. */
static void remap(Framebuffer *fb, int y0, int y1, const Lut *lut, int level)
{
    static const uint8_t BAYER[4] = {0, 2, 3, 1};
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < LLV_W; x++) {
            if (level < 4 && BAYER[(y & 1) * 2 + (x & 1)] >= level)
                continue;
            uint8_t *p = &fb->px[y * LLV_W + x];
            *p = lut->map[*p];
        }
}

static void ground(Framebuffer *fb, Season season)
{
    static const Color BASE[4] = {C_GREEN_L, C_GREEN, C_GRASS_A, C_SNOW};
    static const Color TUFT[4] = {C_GREEN, C_GREEN_D, C_GRASS_A_D, C_SNOW_D};
    static const Color SPECK[4] = {C_PINK, C_SUN, C_LEAF, C_WHITE};
    gfx_rect(fb, 0, LLV_HUD_H, LLV_W, FIELD_BOTTOM - LLV_HUD_H, BASE[season]);
    /* césped segado a franjas: puntitos más oscuros en las columnas pares */
    for (int y = GY0; y < FIELD_BOTTOM; y++)
        for (int x = GX0; x < GX0 + LLV_GARDEN_COLS * CELL; x++)
            if (((x - GX0) / CELL) % 2 == 0 && ((x + y * 2) & 3) == 0)
                gfx_pset(fb, x, y, TUFT[season]);
    for (int by = 1; by < 14; by++)
        for (int bx = 0; bx < 15; bx++) {
            uint32_t h = hash((uint32_t)(bx * 131 + by * 7 + 5));
            int x = bx * 8 + (int)(h % 6), y = by * 8 + 4 + (int)((h >> 6) % 6);
            if (y < LLV_HUD_H + 2 || y >= FIELD_BOTTOM - 2)
                continue;
            switch ((h >> 12) % 10) {
            case 0: case 1: case 2: case 3: case 4: /* mata pequeña */
                gfx_pset(fb, x, y, TUFT[season]);
                gfx_pset(fb, x + 2, y, TUFT[season]);
                gfx_pset(fb, x + 1, y + 1, TUFT[season]);
                break;
            case 5: case 6: /* mata grande */
                gfx_rect(fb, x, y, 1, 2, TUFT[season]);
                gfx_rect(fb, x + 2, y - 1, 1, 3, TUFT[season]);
                gfx_rect(fb, x + 4, y, 1, 2, TUFT[season]);
                gfx_rect(fb, x, y + 2, 5, 1, TUFT[season]);
                break;
            case 7: /* piedrecitas */
                gfx_pset(fb, x, y, season == SEASON_WINTER ? C_GREY_L : C_STONE);
                gfx_pset(fb, x + 1, y, season == SEASON_WINTER ? C_GREY_L : C_STONE_L);
                gfx_pset(fb, x + 1, y + 1, C_GREY);
                break;
            default: /* florecillas sueltas */
                gfx_pset(fb, x, y, SPECK[season]);
                if (season != SEASON_WINTER && season != SEASON_AUTUMN) {
                    gfx_pset(fb, x - 1, y + 1, SPECK[season]);
                    gfx_pset(fb, x + 1, y + 1, SPECK[season]);
                    gfx_pset(fb, x, y + 1, C_SUN_CORE);
                }
                break;
            }
        }
    /* la valla del fondo */
    for (int x = 0; x < LLV_W; x += 15) {
        gfx_rect(fb, x, LLV_HUD_H, 2, 6, C_GROUND_D);
        gfx_pset(fb, x, LLV_HUD_H, C_WOOD_L);
        if (season == SEASON_WINTER)
            gfx_rect(fb, x - 1, LLV_HUD_H, 4, 1, C_SNOW);
    }
    gfx_rect(fb, 0, LLV_HUD_H + 1, LLV_W, 1, C_WOOD_L);
    gfx_rect(fb, 0, LLV_HUD_H + 3, LLV_W, 1, C_GROUND);
    gfx_rect(fb, 0, LLV_HUD_H + 5, LLV_W, 1, C_GROUND_D);
}

static void rain(Framebuffer *fb, uint32_t ms, int n)
{
    int span = FIELD_BOTTOM - LLV_HUD_H;
    for (int i = 0; i < n; i++) {
        uint32_t h = hash((uint32_t)i * 977 + 13);
        int speed = 2 + (int)((h >> 20) % 2);
        int y = (int)(((ms / 12) * (uint32_t)speed + (h >> 8)) % (uint32_t)span);
        int x = (int)(h % 140) - 10 - y / 6 + 20;
        Color c = (i & 1) ? C_SKY_D2 : C_SKY_D3;
        gfx_pset(fb, x, LLV_HUD_H + y, c);
        gfx_pset(fb, x - 1, LLV_HUD_H + y + 1, c);
    }
}

static void snow(Framebuffer *fb, uint32_t ms, int n)
{
    int span = FIELD_BOTTOM - LLV_HUD_H;
    for (int i = 0; i < n; i++) {
        uint32_t h = hash((uint32_t)i * 311 + 7);
        int y = (int)(((ms / 45) + (h >> 8)) % (uint32_t)span);
        int x = (int)(h % LLV_W) + WAVE[((ms / 280) + (uint32_t)i) & 7];
        gfx_pset(fb, x, LLV_HUD_H + y, C_WHITE);
        if (i % 4 == 0)
            gfx_pset(fb, x + 1, LLV_HUD_H + y, C_SNOW_D);
    }
}

static void wind(Framebuffer *fb, uint32_t ms, Season season)
{
    for (int i = 0; i < 9; i++) {
        uint32_t h = hash((uint32_t)i * 53 + 3);
        int y = LLV_HUD_H + 8 + (int)(h % (uint32_t)(FIELD_BOTTOM - LLV_HUD_H - 12));
        int x = (int)(((ms / 5) + (h >> 9)) % 190) - 40;
        int len = 8 + (int)((h >> 5) % 8);
        for (int k = 0; k < len; k += 2)
            gfx_pset(fb, x + k, y + (k > len - 5 ? -1 : 0), C_WHITE);
        if (season == SEASON_AUTUMN && i % 2 == 0)
            gfx_rect(fb, x + len + 3, y + WAVE[(ms / 90 + (uint32_t)i) & 7], 2, 1, C_ORANGE);
    }
}

static void rainbow(Framebuffer *fb)
{
    static const Color BAND[6] = {C_RED, C_ORANGE, C_SUN, C_GREEN_L, C_SKY_D1, C_VIOLET};
    for (int k = 0; k < 6; k++) {
        float r = 86.0f - 2.5f * (float)k;
        for (int x = 0; x < LLV_W; x++) {
            float dx = (float)(x - 40);
            if (dx * dx >= r * r)
                continue;
            int y = 126 - (int)sqrtf(r * r - dx * dx);
            if (y >= LLV_HUD_H + 2 && y < FIELD_BOTTOM) {
                gfx_pset(fb, x, y, BAND[k]);
                if ((x + k) & 1)
                    gfx_pset(fb, x, y + 1, BAND[k]);
            }
        }
    }
}

static void shooting_star(Framebuffer *fb, uint32_t ms)
{
    uint32_t t = ms % 3400;
    if (t > 1400)
        return;
    int hx = 112 - (int)(t * 90 / 1400), hy = 14 + (int)(t * 50 / 1400);
    static const Color TAIL[6] = {C_WHITE, C_SUN_CORE, C_MOON, C_STAR_BLUE, C_SKY_D2, C_SKY_N4};
    for (int k = 0; k < 6; k++)
        gfx_pset(fb, hx + k * 3, hy - k * 2, TAIL[k]), gfx_pset(fb, hx + k * 3 + 1, hy - k * 2, TAIL[k]);
    if ((t / 100) & 1)
        gfx_pset(fb, hx - 2, hy + 1, C_WHITE);
}

/* Vida ambiental que no se guarda: solo adorna lo que se ve. */
static void ambient(Framebuffer *fb, Season season, bool day, int weather, uint32_t ms)
{
    if (weather == W_RAIN || weather == W_SNOW)
        return;
    if (day && (season == SEASON_SPRING || season == SEASON_SUMMER)) {
        static const Color WING[3] = {C_PINK, C_SUN, C_WHITE};
        for (int i = 0; i < 2; i++) {
            float t = (float)ms / 900.0f + (float)i * 3.0f;
            int x = 10 + (int)(((ms / 38) + (uint32_t)i * 53) % 100);
            int y = 30 + i * 28 + (int)(9.0f * sinf(t));
            Color c = WING[i];
            bool up = (ms / 110 + (uint32_t)i) & 1;
            gfx_rect(fb, x, y + (up ? 0 : 1), 2, 2, c);
            gfx_rect(fb, x + 3, y + (up ? 0 : 1), 2, 2, c);
            gfx_pset(fb, x + 2, y + 1, C_DARK);
        }
    } else if (!day && (season == SEASON_SPRING || season == SEASON_SUMMER)) {
        for (int i = 0; i < 8; i++) {
            uint32_t h = hash((uint32_t)i * 29 + 1);
            int x = 8 + (int)(h % 104) + WAVE[(ms / 420 + (uint32_t)i) & 7] * 2;
            int y = 22 + (int)((h >> 8) % 80) + WAVE[(ms / 510 + (uint32_t)i * 3) & 7];
            if ((ms / 330 + (uint32_t)i) % 5 != 0) {
                gfx_pset(fb, x, y, C_SUN_CORE);
                gfx_pset(fb, x + 1, y, C_SUN);
            }
        }
    } else if (day && season == SEASON_AUTUMN) {
        static const Color LC[3] = {C_ORANGE, C_LEAF, C_RED};
        for (int i = 0; i < 4; i++) {
            uint32_t h = hash((uint32_t)i * 71 + 9);
            int y = LLV_HUD_H + (int)(((ms / 70) + (h >> 8)) % 98);
            int x = (int)(h % LLV_W) + WAVE[(ms / 240 + (uint32_t)i) & 7] * 2 + y / 8;
            gfx_rect(fb, x % LLV_W, y, 2, 1, LC[i % 3]);
        }
    }
}

static void weather_icon(Framebuffer *fb, int x, int y, int weather, bool day)
{
    if (weather == W_CLEAR && day) {
        gfx_ellipse(fb, x + 4, y + 4, 2, 2, C_SUN);
        gfx_pset(fb, x + 4, y, C_SUN);
        gfx_pset(fb, x + 4, y + 8, C_SUN);
        gfx_pset(fb, x, y + 4, C_SUN);
        gfx_pset(fb, x + 8, y + 4, C_SUN);
    } else if (weather == W_CLEAR) {
        gfx_ellipse(fb, x + 4, y + 4, 3, 3, C_MOON);
        gfx_ellipse(fb, x + 6, y + 3, 3, 3, C_GROUND_D);
    } else if (weather == W_WIND) {
        for (int k = 0; k < 3; k++)
            gfx_rect(fb, x + k, y + 2 + k * 2, 8 - k, 1, C_WHITE);
    } else {
        gfx_ellipse(fb, x + 3, y + 3, 2, 2, C_GREY_L);
        gfx_ellipse(fb, x + 6, y + 3, 2, 2, C_GREY_L);
        gfx_rect(fb, x + 1, y + 3, 7, 2, C_GREY_L);
        if (weather == W_RAIN) {
            gfx_pset(fb, x + 2, y + 7, C_SKY_D1);
            gfx_pset(fb, x + 5, y + 8, C_SKY_D1);
        } else {
            gfx_pset(fb, x + 2, y + 7, C_WHITE);
            gfx_pset(fb, x + 5, y + 8, C_WHITE);
        }
    }
}

static void strip(Framebuffer *fb, const GameState *gs, Season season, bool day)
{
    gfx_rect(fb, 0, FIELD_BOTTOM, LLV_W, LLV_H - FIELD_BOTTOM, C_GROUND_D);
    gfx_rect(fb, 0, FIELD_BOTTOM, LLV_W, 1, C_WOOD_L);
    weather_icon(fb, 3, FIELD_BOTTOM + 1, gs->weather, day);
    const char *what;
    if (gs->sky == SKY_RAINBOW && day)
        what = "¡Arcoíris!";
    else if (gs->sky == SKY_STAR && !day)
        what = "Estrella fugaz";
    else
        what = gs->weather == W_RAIN ? "Lluvia" : gs->weather == W_WIND ? "Viento"
             : gs->weather == W_SNOW ? "Nieve" : day ? "Sol" : "Noche";
    gfx_text(fb, 15, FIELD_BOTTOM + 1, what, C_WHITE);
    const char *s = SEASON_NAME[season];
    if (15 + gfx_text_width(what) + 8 + gfx_text_width(s) < LLV_W - 2)
        gfx_text(fb, LLV_W - 3 - gfx_text_width(s), FIELD_BOTTOM + 1, s, C_LEAF);
}

static void lupa_label(const GameState *gs, const Entity *e, char *buf, size_t n)
{
    const GardenRule *r = garden_rule(e->sp, e->st);
    int left = garden_ticks_left(gs, e);
    if (!r)
        return;
    if (!r->then || left < 0) {
        snprintf(buf, n, "%s · sin prisa", r->name);
        return;
    }
    int min = left * (LLV_TICK_SECONDS / 60);
    char t[16];
    if (min < 60)
        snprintf(t, sizeof(t), "~%d min", min < 1 ? 1 : min);
    else if (min < 48 * 60)
        snprintf(t, sizeof(t), "~%d h", (min + 30) / 60);
    else
        snprintf(t, sizeof(t), "~%d d", (min + 720) / 1440);
    snprintf(buf, n, "%s · %s en %s", r->name, r->then, t);
}

static void draw_lupa(Framebuffer *fb, const GameState *gs, uint32_t ms)
{
    if (!ms_pending(ms, lupa_until) || lupa_cell < 0)
        return;
    const Entity *e = garden_at(gs, lupa_cell);
    if (!e)
        return;
    int cx = GX0 + (lupa_cell % LLV_GARDEN_COLS) * CELL, cy = GY0 + (lupa_cell / LLV_GARDEN_COLS) * CELL;
    Color c = ((ms / 260) & 1) ? C_WHITE : C_SUN;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int x = i ? cx + CELL - 1 : cx, y = j ? cy + CELL - 1 : cy;
            int sx = i ? -1 : 1, sy = j ? -1 : 1;
            for (int k = 0; k < 5; k++) { /* esquina en L con sombra oscura */
                gfx_pset(fb, x + sx * k, y + sy, C_DARK);
                gfx_pset(fb, x + sx, y + sy * k, C_DARK);
                gfx_pset(fb, x + sx * k, y, c);
                gfx_pset(fb, x, y + sy * k, c);
            }
        }
    char buf[64];
    lupa_label(gs, e, buf, sizeof(buf));
    int lines = gfx_text_wrap(NULL, 0, 0, 100, buf, C_DARK);
    int h = lines * GFX_LINE_H + 9;
    int y = (lupa_cell / LLV_GARDEN_COLS) < 3 ? FIELD_BOTTOM - 2 - h : GY0 + 1;
    gfx_textbox(fb, 4, y, 112, h);
    gfx_text_wrap(fb, 10, y + 5, 100, buf, C_DARK);
}

static void jardin_render(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb)
{
    if (!luts_ready)
        build_luts();
    CivilTime c = time_to_civil(ctx->now);
    Season season = time_season(c.month, c.day, LLV_SOUTH_HEMISPHERE);
    int m = c.hour * 60 + c.minute;
    bool day = m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN;

    ground(fb, season);
    for (int row = 0; row < LLV_GARDEN_ROWS; row++)
        for (int col = 0; col < LLV_GARDEN_COLS; col++) {
            const Entity *e = garden_at(gs, row * LLV_GARDEN_COLS + col);
            if (e)
                art_entity(fb, e, GX0 + col * CELL, GY0 + row * CELL, season, ctx->ms,
                           hash((uint32_t)(row * LLV_GARDEN_COLS + col) + 11));
        }

    /* Luz del día: noche cerrada, crepúsculo a medias y nublado en lluvia. */
    if (!day) {
        /* En los 40 min tras ponerse (o antes de salir) el sol, la luz baja por tramos con
         * dithering: primero el día gris, luego la noche entra a medias. */
        int away = m >= LLV_SUNSET_MIN ? m - LLV_SUNSET_MIN : LLV_SUNRISE_MIN - m;
        if (away >= 40) {
            remap(fb, LLV_HUD_H, FIELD_BOTTOM, &NIGHT_LUT, 4);
        } else {
            remap(fb, LLV_HUD_H, FIELD_BOTTOM, &DIM_LUT, away < 13 ? 2 : 4);
            if (away >= 26)
                remap(fb, LLV_HUD_H, FIELD_BOTTOM, &NIGHT_LUT, 2);
        }
    }
    if (gs->weather == W_RAIN)
        remap(fb, LLV_HUD_H, FIELD_BOTTOM, &DIM_LUT, 2);

    ambient(fb, season, day, gs->weather, ctx->ms);
    if (gs->sky == SKY_RAINBOW && day && gs->weather != W_RAIN)
        rainbow(fb);
    if (gs->sky == SKY_STAR && !day)
        shooting_star(fb, ctx->ms);
    switch (gs->weather) {
    case W_RAIN: rain(fb, ctx->ms, gs->weather_left > 20 ? 46 : 30); break;
    case W_SNOW: snow(fb, ctx->ms, 34); break;
    case W_WIND: wind(fb, ctx->ms, season); break;
    default: break;
    }
    draw_lupa(fb, gs, ctx->ms);
    strip(fb, gs, season, day);
}

const World world_jardin = {
    .name = "Jardín",
    .daily_actions = LLV_GARDEN_ACTIONS,
    .tick = garden_tick,
    .render = jardin_render,
    .on_tap = jardin_tap,
    .on_hold = jardin_hold,
};
