/* 👵🤖 La Sala (spec §4.3): la Abuela Sol y Unidad-7 en una habitación cálida, con visitantes
 * a ratos. A (toque): dar pie (o contestar sí / seguir la escena); A (mantener): tocar el timbre
 * (o contestar no); agitar: alguien protesta. Máximo 2 conversaciones nuevas al día: después,
 * frases de relleno con gracia. La lógica está en talk.c; aquí solo hay entrada, la máquina de
 * escribir con su pitido por letra y el dibujo. Las variables estáticas son solo visuales. */
#include <stdio.h>
#include <string.h>

#include "../cast.h"
#include "../director.h"
#include "../font.h"
#include "../phrases.h"
#include "../talk.h"
#include "../timeutil.h"
#include "../world.h"

#define BOX_X 2
#define BOX_Y 58
#define BOX_W 116
#define BOX_H 61
#define FLOOR_Y 44   /* la habitación llega hasta BOX_Y */
#define TEXT_X 7
#define TYPE_MS 34   /* una letra cada 34 ms */

/* Efímero, no se guarda. */
static uint32_t type_key;       /* identifica la frase que se está escribiendo */
static uint32_t type_start_ms;
static int type_last_shown;     /* letras ya pitadas */

void sala_ui_reset(void)
{
    type_key = 0;
    type_start_ms = 0;
    type_last_shown = 0;
}

static PhraseEnv env_of(const WorldCtx *ctx)
{
    PhraseEnv e = {ctx->now, ctx->shaken, ctx->battery, ctx->charging};
    return e;
}

static uint32_t key_of(const GameState *gs)
{
    const Sala *s = &gs->sala;
    return (uint32_t)s->phrase | (uint32_t)s->recent_head << 16 | (uint32_t)(s->stage + 1) << 24;
}

/* Letras en total y letras visibles ahora de la frase actual. */
static int typed_state(const GameState *gs, uint32_t ms, int *total)
{
    char buf[256];
    *total = 0;
    if (!talk_text(gs, buf, sizeof(buf)))
        return 0;
    gfx_text_wrap_n(NULL, 0, 0, PHRASE_BOX_W, buf, C_DARK, -1, total);
    if (key_of(gs) != type_key)
        return 0;
    uint32_t n = (ms - type_start_ms) / TYPE_MS;
    return n > (uint32_t)*total ? *total : (int)n;
}

static bool typing(const GameState *gs, uint32_t ms)
{
    int total;
    int shown = typed_state(gs, ms, &total);
    return total > 0 && key_of(gs) == type_key && shown < total;
}

/* ---------- entrada ---------- */

/* La frase nueva empieza a escribirse en el instante del gesto (no cuando se pinte): así lo que
 * se traga un toque no depende de cuántos fotogramas haya habido. */
static void restart_typing(const GameState *gs, const WorldCtx *ctx)
{
    type_key = key_of(gs);
    type_start_ms = ctx->ms;
    type_last_shown = 0;
}

static void sala_tap(GameState *gs, const WorldCtx *ctx)
{
    if (ctx->absent) { /* "Salí. Vuelvo en 5 min.": no hay nadie con quien hablar */
        world_toast(ctx, "No hay nadie. Espera un poco.", 1800);
        return;
    }
    if (typing(gs, ctx->ms)) { /* primero se termina de leer la frase */
        int total;
        typed_state(gs, ctx->ms, &total);
        type_start_ms = ctx->ms - (uint32_t)total * TYPE_MS;
        return;
    }
    PhraseEnv env = env_of(ctx);
    talk_tap(gs, &env);
    restart_typing(gs, ctx);
}

static void sala_hold(GameState *gs, const WorldCtx *ctx)
{
    if (ctx->absent) {
        world_toast(ctx, "El timbre suena a hueco.", 1800);
        return;
    }
    if (typing(gs, ctx->ms)) {
        int total;
        typed_state(gs, ctx->ms, &total);
        type_start_ms = ctx->ms - (uint32_t)total * TYPE_MS;
    }
    PhraseEnv env = env_of(ctx);
    talk_hold(gs, &env);
    restart_typing(gs, ctx);
}

static void sala_shake(GameState *gs, const WorldCtx *ctx)
{
    if (ctx->absent)
        return;
    PhraseEnv env = env_of(ctx);
    talk_shake(gs, &env);
    restart_typing(gs, ctx);
}

/* ---------- la habitación ---------- */

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

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

static void window(Framebuffer *fb, int x, int y, int w, int h, Period p, uint32_t ms, int weather)
{
    static const Color SKY[4][3] = {
        {C_SKY_N1, C_SKY_N2, C_SKY_N3},
        {C_SKY_S1, C_SKY_S2, C_SKY_S3},
        {C_SKY_D0, C_SKY_D1, C_SKY_D2},
        {C_SKY_S0, C_SKY_S2, C_SKY_S4},
    };
    for (int k = 0; k < 3; k++)
        gfx_rect(fb, x, y + h * k / 3, w, h * (k + 1) / 3 - h * k / 3, SKY[p][k]);
    if (p == P_NIGHT || p == P_DUSK || p == P_DAWN)
        for (int i = 0; i < 7; i++) {
            uint32_t hh = hash((uint32_t)i * 17 + 3);
            if ((ms / 600 + (uint32_t)i) % 6 != 0)
                gfx_pset(fb, x + (int)(hh % (uint32_t)w), y + (int)((hh >> 8) % (uint32_t)(h - 6)),
                         i & 1 ? C_MOON : C_STAR_BLUE);
        }
    if (p == P_NIGHT)
        gfx_ellipse(fb, x + w - 7, y + 6, 3, 3, C_MOON);
    else if (p == P_DAY)
        gfx_ellipse(fb, x + 7, y + 6, 3, 3, C_SUN);
    gfx_rect(fb, x, y + h - 5, w, 5, p == P_NIGHT ? C_HILL_N1 : C_GREEN); /* colinas */
    if (weather == W_RAIN)
        for (int i = 0; i < 8; i++) {
            uint32_t hh = hash((uint32_t)i * 977 + 13);
            int ry = (int)(((ms / 18) + (hh >> 8)) % (uint32_t)(h - 2));
            gfx_pset(fb, x + (int)(hh % (uint32_t)w), y + ry, C_SKY_D3);
        }
    /* marco y travesaños */
    gfx_frame(fb, x - 1, y - 1, w + 2, h + 2, C_GROUND_D);
    gfx_rect(fb, x + w / 2, y, 1, h, C_GROUND_D);
    gfx_rect(fb, x, y + h / 2, w, 1, C_GROUND_D);
    gfx_rect(fb, x - 3, y + h + 1, w + 6, 2, C_WOOD_L);
}

static void lamp(Framebuffer *fb, int x, bool glow, Color shade)
{
    gfx_rect(fb, x, LLV_HUD_H, 1, 8, C_GREY_D);
    gfx_rect(fb, x - 5, LLV_HUD_H + 8, 11, 2, shade);
    gfx_rect(fb, x - 4, LLV_HUD_H + 10, 9, 3, shade);
    gfx_rect(fb, x - 3, LLV_HUD_H + 13, 7, 1, C_LAMP);
    gfx_pset(fb, x, LLV_HUD_H + 14, C_WHITE);
    if (glow) /* halo de luz cálida con tramado */
        for (int j = -9; j <= 9; j++)
            for (int i = -14; i <= 14; i++) {
                int d = i * i / 2 + j * j * 3 / 2;
                int xx = x + i, yy = LLV_HUD_H + 15 + j;
                if (d < 60 && (xx & 1) == 0 && (yy & 1) == 0 && yy > LLV_HUD_H + 14 && gfx_get(fb, xx, yy) != C_DARK)
                    gfx_pset(fb, xx, yy, C_LAMP);
            }
}

static void plant_pot(Framebuffer *fb, int x, int y, int leaves)
{
    gfx_rect(fb, x, y, 7, 5, C_ORANGE);
    gfx_rect(fb, x - 1, y, 9, 1, C_GROUND);
    gfx_rect(fb, x + 1, y + 4, 5, 1, C_GROUND_D);
    for (int k = 0; k < leaves; k++) {
        int lx = x + 1 + (k * 5) % 6, ly = y - 3 - (k * 3) % 5;
        gfx_rect(fb, lx, ly, 2, 4, k & 1 ? C_GREEN : C_GREEN_L);
        gfx_pset(fb, lx, ly, C_GREEN_D);
    }
}

static void teapot(Framebuffer *fb, int x, int y, uint32_t ms)
{
    gfx_ellipse(fb, x + 4, y + 3, 4, 3, C_CREAM);
    gfx_rect(fb, x, y + 2, 8, 3, C_CREAM);
    gfx_rect(fb, x + 8, y + 1, 3, 1, C_CREAM); /* pico */
    gfx_pset(fb, x + 10, y, C_CREAM);
    gfx_rect(fb, x - 2, y + 1, 2, 3, C_GREY_L); /* asa */
    gfx_rect(fb, x + 2, y - 1, 4, 1, C_RED);    /* tapa */
    gfx_pset(fb, x + 4, y - 2, C_RED);
    gfx_pset(fb, x + 2, y + 3, C_RED);
    gfx_pset(fb, x + 5, y + 2, C_RED);
    for (int k = 0; k < 3; k++) { /* vaho */
        int t = (int)((ms / 160 + (uint32_t)k * 3) % 9);
        if (t < 7)
            gfx_pset(fb, x + 10 + (t & 1) + (t > 3), y - 2 - t, k == 1 ? C_WHITE : C_GREY_L);
    }
}

static void cup(Framebuffer *fb, int x, int y, uint32_t ms)
{
    gfx_rect(fb, x, y, 5, 4, C_CREAM);
    gfx_rect(fb, x + 1, y, 3, 1, C_TEA);
    gfx_pset(fb, x + 5, y + 1, C_CREAM);
    gfx_pset(fb, x + 5, y + 2, C_CREAM);
    if ((ms / 700) & 1)
        gfx_pset(fb, x + 2, y - 2, C_GREY_L);
    else
        gfx_pset(fb, x + 3, y - 2, C_GREY_L);
}

static void deco(Framebuffer *fb, int date, uint32_t ms)
{
    switch (date) {
    case DATE_HALLOWEEN: {
        /* telarañas en la esquina, murciélagos y una calabaza con vela en la mesa */
        for (int k = 0; k < 14; k += 3) {
            gfx_rect(fb, 0, LLV_HUD_H + k, 1 + (14 - k) / 2, 1, C_GREY_L);
            gfx_rect(fb, k / 2, LLV_HUD_H, 1, 14 - k, C_GREY_L);
        }
        for (int b = 0; b < 2; b++) {
            int bx = 52 + b * 40 + (int)((ms / 90 + (uint32_t)b * 5) % 4), by = 22 + b * 3;
            bool up = (ms / 140 + (uint32_t)b) & 1;
            gfx_rect(fb, bx, by, 3, 2, C_DARK);
            gfx_pset(fb, bx - 1, by + (up ? -1 : 1), C_DARK);
            gfx_pset(fb, bx + 3, by + (up ? -1 : 1), C_DARK);
        }
        break;
    }
    case DATE_ANIO_NUEVO: {
        static const Color CF[5] = {C_RED, C_SUN, C_CYAN, C_MAGENTA, C_GREEN_L};
        for (int i = 0; i < 26; i++) {
            uint32_t h = hash((uint32_t)i * 41 + 9);
            int y = LLV_HUD_H + (int)(((ms / 60) + (h >> 8)) % 54);
            gfx_rect(fb, (int)(h % LLV_W), y, 2, 1, CF[i % 5]);
        }
        for (int k = 0; k < LLV_W; k += 2) /* guirnalda */
            gfx_pset(fb, k, LLV_HUD_H + 2 + (k / 2) % 3, CF[(k / 4) % 5]);
        break;
    }
    case DATE_NAVIDAD:
        for (int k = 0; k < LLV_W; k += 4) { /* lucecitas */
            gfx_pset(fb, k, LLV_HUD_H + 1 + (k / 4) % 2, ((ms / 400 + (uint32_t)k) & 1) ? C_SUN : C_RED);
            gfx_pset(fb, k + 1, LLV_HUD_H + 1 + (k / 4) % 2, C_GREEN);
        }
        break;
    case DATE_VIERNES13: /* el 13 en el calendario (la vela va en la mesa) */
        gfx_rect(fb, 96, 13, 14, 12, C_CREAM);
        gfx_rect(fb, 96, 13, 14, 3, C_RED);
        gfx_text(fb, 98, 17, "13", C_DARK);
        break;
    default:
        break;
    }
}

static void room(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx, int date)
{
    CivilTime c = time_to_civil(ctx->now);
    int m = c.hour * 60 + c.minute;
    Period per = period_of(m);
    bool halloween = date == DATE_HALLOWEEN;
    gfx_rect(fb, 0, BOX_Y, LLV_W, LLV_H - BOX_Y, C_GROUND_D); /* tras la caja de texto */

    /* pared de tablones con friso */
    gfx_rect(fb, 0, LLV_HUD_H, LLV_W, FLOOR_Y - LLV_HUD_H, halloween ? C_GROUND_D : C_WALL);
    for (int px = 0; px < LLV_W; px += 12) {
        gfx_rect(fb, px, LLV_HUD_H, 1, 25, halloween ? C_ROOF : C_WALL_D);
        uint32_t hh = hash((uint32_t)px + 11);
        gfx_pset(fb, px + 4 + (int)(hh % 5), LLV_HUD_H + 3 + (int)((hh >> 4) % 19), halloween ? C_ROOF : C_WALL_D);
    }
    gfx_rect(fb, 0, 34, LLV_W, 10, C_GROUND);
    gfx_rect(fb, 0, 34, LLV_W, 1, C_WOOD_L);
    gfx_rect(fb, 0, 35, LLV_W, 1, C_GROUND_D);
    for (int px = 4; px < LLV_W; px += 12)
        gfx_rect(fb, px, 37, 7, 5, C_GROUND_D);
    if (per == P_NIGHT || halloween)
        gfx_dither(fb, 0, LLV_HUD_H, LLV_W, FLOOR_Y - LLV_HUD_H, halloween ? C_GROUND_D : C_WALL, C_WALL_D); /* luz baja */

    /* suelo de tarima y alfombra */
    gfx_rect(fb, 0, FLOOR_Y, LLV_W, BOX_Y - FLOOR_Y, C_WOOD_L);
    for (int y = FLOOR_Y + 4; y < BOX_Y; y += 5)
        gfx_rect(fb, 0, y, LLV_W, 1, C_GROUND);
    for (int y = FLOOR_Y; y < BOX_Y; y += 5)
        for (int px = (y / 5) % 2 ? 6 : 22; px < LLV_W; px += 34)
            gfx_rect(fb, px, y, 1, 4, C_GROUND);
    gfx_rect(fb, 18, 51, 84, 7, halloween ? C_VIOLET_D : C_TEA);
    gfx_rect(fb, 18, 51, 84, 1, C_CREAM);
    gfx_rect(fb, 20, 53, 80, 1, C_RED);
    gfx_rect(fb, 0, FLOOR_Y, LLV_W, 1, C_GROUND_D); /* zócalo */

    /* ventana, lámpara, estantería con plantas y el panel solar de la Abuela */
    window(fb, 50, 13, 26, 21, per, ctx->ms, gs->weather);
    gfx_rect(fb, 46, 12, 3, 22, halloween ? C_VIOLET : C_ORANGE); /* cortinas */
    gfx_rect(fb, 77, 12, 3, 22, halloween ? C_VIOLET : C_ORANGE);
    gfx_rect(fb, 46, 12, 34, 1, C_GROUND_D);
    lamp(fb, 33, per == P_NIGHT || per == P_DUSK || halloween, halloween ? C_PUMPKIN : C_RED);
    gfx_rect(fb, 90, 31, 26, 2, C_WOOD_L); /* repisa */
    plant_pot(fb, 92, 26, 3);
    plant_pot(fb, 101, 26, 2);
    plant_pot(fb, 109, 26, 3);
    for (int j = 0; j < 7; j++) /* panel solar enmarcado */
        for (int i = 0; i < 12; i++)
            gfx_pset(fb, 92 + i, 12 + j, (i % 4 == 0 || j % 3 == 0) ? C_SKY_D2 : C_BLUE_TXT);
    gfx_frame(fb, 91, 11, 14, 9, C_GREY_D);
    gfx_rect(fb, 5, LLV_HUD_H, 1, 12, C_GREEN_D); /* planta colgante */
    gfx_rect(fb, 13, LLV_HUD_H, 1, 8, C_GREEN_D);
    gfx_rect(fb, 3, 21, 12, 4, C_ORANGE);
    for (int k = 0; k < 6; k++)
        gfx_pset(fb, 4 + k * 2, 25 + (k & 1), C_GREEN);
    deco(fb, date, ctx->ms);
}

static void table(Framebuffer *fb, uint32_t ms, int date)
{
    gfx_rect(fb, 36, 46, 48, 3, C_WOOD_L); /* tablero */
    gfx_rect(fb, 36, 46, 48, 1, C_CREAM);
    gfx_rect(fb, 36, 49, 48, 1, C_GROUND_D);
    gfx_rect(fb, 38, 50, 3, 8, C_GROUND_D);
    gfx_rect(fb, 79, 50, 3, 8, C_GROUND_D);
    teapot(fb, 38, 41, ms);
    cup(fb, 72, 43, ms);
    if (date == DATE_HALLOWEEN) { /* una calabaza con vela */
        gfx_ellipse(fb, 78, 43, 4, 3, C_PUMPKIN);
        gfx_rect(fb, 77, 39, 2, 2, C_GREEN_D);
        gfx_pset(fb, 76, 42, C_DARK);
        gfx_pset(fb, 80, 42, C_DARK);
        gfx_rect(fb, 77, 44, 3, 1, ((ms / 200) & 1) ? C_SUN : C_LAMP);
    } else if (date == DATE_VIERNES13) { /* una vela */
        gfx_rect(fb, 77, 41, 2, 5, C_CREAM);
        gfx_pset(fb, 77, 40 - (int)((ms / 150) & 1), C_SUN);
    } else {
        gfx_rect(fb, 78, 44, 4, 2, C_RED); /* pastas */
    }
}

/* ---------- los personajes ---------- */

static Expr expr_of(const GameState *gs, int spk, bool pajama, bool talking, bool speaker)
{
    PhraseRec r;
    if (pajama)
        return talking && speaker ? EXPR_TALK : EXPR_SLEEPY;
    if (speaker) {
        if (talking)
            return EXPR_TALK;
        if (gs->sala.phrase && phrase_get(gs->sala.phrase - 1, &r)) {
            switch (r.kind) {
            case PK_SI: return EXPR_HAPPY;
            case PK_MENTIRA: case PK_REBELDIA: return EXPR_SMUG;
            case PK_CUARTA_PARED: return EXPR_SURPRISE;
            default: break;
            }
            if (r.picaro)
                return EXPR_SMUG;
        }
        return EXPR_NEUTRAL;
    }
    uint8_t aff = gs->sala.affinity[spk];
    return aff >= LLV_AFF_HIGH ? EXPR_HAPPY : aff <= LLV_AFF_LOW ? EXPR_SMUG : EXPR_NEUTRAL;
}

static void cast_scene(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx, bool talking)
{
    CivilTime c = time_to_civil(ctx->now);
    bool pajama = c.hour < 5;
    int who = gs->sala.stage != DLG_IDLE ? gs->sala.who : 0;
    int vis = gs->sala.visitor;
    struct { int spk, x; } seats[3] = {{SPK_ABUELA, 12}, {SPK_ROBOT, 92}, {vis, 52}};
    table(fb, ctx->ms, phrase_special_date(ctx->now));
    for (int i = 2; i >= 0; i--) { /* el visitante primero (al centro, delante de la mesa) */
        int spk = seats[i].spk;
        if (!spk)
            continue;
        bool speaker = spk == who;
        int bob = speaker && talking && ((ctx->ms / 120) & 1) ? 1 : 0;
        int y = 29 - bob;
        bool pj = pajama && (spk == SPK_ABUELA || spk == SPK_ROBOT);
        Expr e = expr_of(gs, spk, pj, talking, speaker);
        cast_draw(fb, spk, seats[i].x, y, e, ctx->ms, pj);
    }
    if (vis == SPK_GATO) { /* la caja: el gato asoma por encima */
        gfx_rect(fb, 50, 43, 20, 12, C_WOOD_L);
        gfx_frame(fb, 50, 43, 20, 12, C_GROUND_D);
        gfx_rect(fb, 50, 43, 20, 2, C_GROUND);
        gfx_text(fb, 57, 46, "?", C_GROUND_D);
    }
    if (pajama) { /* zzz sobre quien no habla */
        int zy = 31 - (int)((ctx->ms / 500) % 3);
        gfx_text(fb, 30, zy, "z", C_WHITE);
        gfx_text(fb, 35, zy - 7, "Z", C_SKY_D2);
    }
}

/* ---------- caja de texto con máquina de escribir ---------- */

static const Color TAB[SPK_COUNT] = {
    [SPK_ABUELA] = C_GREEN_D, [SPK_ROBOT] = C_BLUE_TXT, [SPK_GOTICA] = C_VIOLET, [SPK_GAMER] = C_RED,
    [SPK_BOOMER] = C_GROUND_D, [SPK_GATO] = C_GREY_D, [SPK_SISTEMA] = C_GREY_D,
};

static const uint16_t VOICE[SPK_COUNT] = {
    [SPK_ABUELA] = 330, [SPK_ROBOT] = 700, [SPK_GOTICA] = 247, [SPK_GAMER] = 988,
    [SPK_BOOMER] = 294, [SPK_GATO] = 784, [SPK_SISTEMA] = 440,
};

/* Pitido de cada letra: tono base del personaje y un poco de variación por letra. */
static void blip(const WorldCtx *ctx, int spk, const char *text, int from, int to)
{
    if (!ctx->tone || spk <= 0 || spk >= SPK_COUNT)
        return;
    const char *p = text;
    int i = 0, emitted = 0;
    while (*p && i < to && emitted < 2) {
        uint32_t cp = font_utf8_next(&p);
        if (i >= from && cp != ' ') {
            uint16_t f = (uint16_t)(VOICE[spk] + (cp % 7) * (VOICE[spk] / 18));
            if (spk == SPK_ROBOT && (cp & 1))
                f = (uint16_t)(f * 5 / 6);
            ctx->tone(ctx->user, f, 22);
            emitted++;
        }
        i++;
    }
}

static void arrow(Framebuffer *fb, int x, int y, uint32_t ms)
{
    if ((ms / 350) & 1)
        return;
    gfx_rect(fb, x, y, 5, 1, C_RED);
    gfx_rect(fb, x + 1, y + 1, 3, 1, C_RED);
    gfx_pset(fb, x + 2, y + 2, C_RED);
}

static void text_box(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx, int *talking_out)
{
    const Sala *s = &gs->sala;
    gfx_textbox(fb, BOX_X, BOX_Y, BOX_W, BOX_H);
    char buf[256];
    *talking_out = 0;
    if (!talk_text(gs, buf, sizeof(buf))) {
        gfx_text_center(fb, BOX_Y + 8, "Toca A: charlar", C_DARK);
        gfx_text_center(fb, BOX_Y + 18, "Mantén A: timbre", C_DARK);
        char line[40];
        int left = LLV_SALA_ACTIONS - (int)gs->actions_used[WORLD_SALA];
        if (left >= 2)
            snprintf(line, sizeof(line), "Charlas de hoy: %d", left);
        else if (left == 1)
            snprintf(line, sizeof(line), "Última charla de hoy");
        else
            snprintf(line, sizeof(line), "Hoy ya charlamos");
        gfx_text_center(fb, BOX_Y + 32, line, C_GREY);
        if (s->visitor) {
            snprintf(line, sizeof(line), "De visita: %s", speaker_short(s->visitor));
            gfx_text_center(fb, BOX_Y + 41, line, C_BLUE_TXT);
        }
        return;
    }
    /* máquina de escribir: la frase empieza a escribirse cuando cambia (pitido por letra) */
    uint32_t key = key_of(gs);
    if (key != type_key) {
        type_key = key;
        type_start_ms = ctx->ms;
        type_last_shown = 0;
    }
    int total, shown = typed_state(gs, ctx->ms, &total);
    if (shown > type_last_shown)
        blip(ctx, s->who, buf, type_last_shown, shown);
    type_last_shown = shown;
    *talking_out = shown < total;

    int lines = gfx_text_wrap_n(fb, TEXT_X, BOX_Y + 5, PHRASE_BOX_W, buf, C_DARK, shown, NULL);
    if (shown >= total) {
        if (s->stage == DLG_ASK && lines <= PHRASE_ASK_LINES)
            gfx_text(fb, TEXT_X, BOX_Y + 5 + 5 * GFX_LINE_H, "A: sí · Mantén: no", C_BLUE_TXT);
        else if (s->stage == DLG_SAY)
            arrow(fb, BOX_X + BOX_W - 11, BOX_Y + BOX_H - 8, ctx->ms);
    }
    /* etiqueta con el nombre, encima de la caja */
    const char *name = speaker_name(s->who);
    if (name[0]) {
        int w = gfx_text_width(name) + 8;
        gfx_rect(fb, BOX_X + 5, BOX_Y - 6, w, 10, C_DARK);
        gfx_rect(fb, BOX_X + 6, BOX_Y - 5, w - 2, 8, TAB[s->who]);
        gfx_text(fb, BOX_X + 9, BOX_Y - 4, name, C_WHITE);
    }
}

static void sala_render(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb)
{
    int date = phrase_special_date(ctx->now);
    room(fb, gs, ctx, date);
    int talking = 0;
    /* El texto se pinta después de los personajes, pero hay que saber si siguen hablando. */
    char buf[8];
    bool has_text = talk_text(gs, buf, sizeof(buf));
    int total = 0, shown = has_text ? typed_state(gs, ctx->ms, &total) : 0;
    bool still = has_text && key_of(gs) == type_key && shown < total;
    if (!ctx->absent)
        cast_scene(fb, gs, ctx, still);
    text_box(fb, gs, ctx, &talking);
}

const World world_sala = {
    .name = "La Sala",
    .daily_actions = LLV_SALA_ACTIONS,
    .tick = sala_tick,
    .render = sala_render,
    .on_tap = sala_tap,
    .on_hold = sala_hold,
    .on_shake = sala_shake,
};
