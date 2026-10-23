/* 🏚 El Cobertizo (spec §4.6): la bolsa, la colección y la mascota en el alféizar.
 * A (toque): mueve el cursor; sobre la mascota empieza "respirar juntos" (sin límite):
 *   mantener A inhala, soltar exhala, y un toque sale del ejercicio.
 * A (mantener): prepara el objeto bajo el cursor para la próxima acción del Jardín, o
 *   cuida a la mascota (1 al día). La primera vez se elige mascota: toque cicla, mantener confirma.
 * A (mantener) sobre la colección: abre AJUSTES (spec §3.3): activar y desactivar cada hábito y
 *   el modo apto para la abuela. Dentro, A (toque) baja de fila y A (mantener) cambia o sale.
 * Todo lo de aquí que no es GameState es visual y efímero. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../art.h"
#include "../garden.h"
#include "../habits.h"
#include "../input.h"
#include "../pet.h"
#include "../timeutil.h"
#include "../world.h"

#define SILL_Y 44
#define PET_X 60
#define PANEL_TOP 55

/* Efímero, no se guarda. */
static int cursor;                 /* 0..bag_count-1: bolsa; luego colección; luego mascota */
static bool breathing;
static int choose = PET_HONGO;
static uint32_t breath_last_ms;
static int breath_level;           /* 0..1000 */
static int breath_cycles;
static bool breath_full;
static uint32_t joy_until;
static bool menu_open;             /* AJUSTES de hábitos */
static int menu_row;
#define MENU_ROWS (HAB_COUNT + 2)  /* hábitos, modo abuela y salir */

static uint32_t hash(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

static int positions(const GameState *gs)
{
    return gs->bag_count + 1 + (gs->pet.kind != PET_NONE ? 1 : 0);
}

static bool on_pet(const GameState *gs)
{
    return gs->pet.kind != PET_NONE && cursor % positions(gs) == positions(gs) - 1;
}

static bool on_collection(const GameState *gs)
{
    return cursor % positions(gs) == gs->bag_count;
}

void cobertizo_ui_reset(void)
{
    cursor = 0;
    breathing = false;
    choose = PET_HONGO;
    breath_level = breath_cycles = 0;
    breath_full = false;
    joy_until = 0;
    menu_open = false;
    menu_row = 0;
}

bool cobertizo_menu_open(void)
{
    return menu_open;
}

bool cobertizo_on_pet(const GameState *gs)
{
    return on_pet(gs);
}

bool cobertizo_breathing(void)
{
    return breathing;
}

static int collected(const GameState *gs)
{
    int n = 0;
    for (int i = 0; i < COLL_COUNT; i++)
        n += (gs->collection >> i) & 1;
    return n;
}

/* ---------- entrada ---------- */

static void cobertizo_tap(GameState *gs, const WorldCtx *ctx)
{
    if (gs->pet.kind == PET_NONE) {
        choose = choose % (PET_COUNT - 1) + 1;
        return;
    }
    if (menu_open) {
        menu_row = (menu_row + 1) % MENU_ROWS;
        return;
    }
    if (breathing) {
        breathing = false;
        cursor = 0;
        /* Respirar con la mascota es un hábito: con 3 respiraciones completas, ella se alegra. */
        if (breath_cycles >= 3 && habit_enabled(gs, HAB_PAUSA) && !(gs->hab.done_today >> HAB_PAUSA & 1)) {
            habit_breath(gs, breath_cycles);
            joy_until = ctx->ms + 3500;
            world_toast(ctx, "Tu mascota está contenta.", 2600);
        }
        return;
    }
    if (on_pet(gs)) {
        breathing = true;
        breath_level = 0;
        breath_cycles = 0;
        breath_full = false;
        breath_last_ms = ctx->ms;
        return;
    }
    cursor = (cursor % positions(gs) + 1) % positions(gs);
}

static void cobertizo_hold(GameState *gs, const WorldCtx *ctx)
{
    char msg[96];
    if (gs->pet.kind == PET_NONE) {
        if (pet_choose(gs, (PetKind)choose)) {
            snprintf(msg, sizeof(msg), "Llega %s. Cuídalo despacio.", pet_name((PetKind)choose));
            world_toast(ctx, msg, 3000);
            cursor = positions(gs) - 1;
            joy_until = ctx->ms + 3500;
        }
        return;
    }
    if (breathing)
        return; /* la pulsación larga es la inhalación */
    if (menu_open) {
        if (menu_row < HAB_COUNT) {
            habit_set_enabled(gs, menu_row, !habit_enabled(gs, menu_row));
        } else if (menu_row == HAB_COUNT) {
            gs->flags = (uint8_t)(gs->flags ^ SF_GRANDMA);
        } else {
            menu_open = false;
            menu_row = 0;
        }
        return;
    }
    int c = cursor % positions(gs);
    if (on_pet(gs)) {
        CareResult r = pet_care(gs, ctx->index, msg, sizeof(msg));
        if (r == CARE_OK)
            joy_until = ctx->ms + 3500;
        world_toast(ctx, msg, 3000);
    } else if (c < gs->bag_count) {
        if (gs->prepared == gs->bag[c]) {
            gs->prepared = ITEM_NONE;
            world_toast(ctx, "Ya no hay nada preparado.", 2000);
        } else {
            gs->prepared = gs->bag[c];
            snprintf(msg, sizeof(msg), "Preparado: %s\nPara la próxima acción.", state_item_name(gs->bag[c]));
            world_toast(ctx, msg, 2600);
        }
    } else {
        menu_open = true;
        menu_row = 0;
    }
}

/* ---------- escena ---------- */

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

static void window_view(Framebuffer *fb, int x, int y, int w, int h, const GameState *gs,
                        const WorldCtx *ctx, Season season, int m)
{
    static const Color SKY[4][4] = {
        {C_SKY_N0, C_SKY_N1, C_SKY_N2, C_SKY_N3},
        {C_SKY_S0, C_SKY_S1, C_SKY_S2, C_SKY_S3},
        {C_SKY_D0, C_SKY_D1, C_SKY_D2, C_SKY_D3},
        {C_SKY_N2, C_SKY_S0, C_SKY_S1, C_SKY_S2},
    };
    Period p = period_of(m);
    bool rainy = gs->weather == W_RAIN || gs->weather == W_SNOW;
    for (int k = 0; k < 4; k++) {
        int y0 = y + h * k / 4, y1 = y + h * (k + 1) / 4;
        gfx_rect(fb, x, y0, w, y1 - y0, rainy && p != P_NIGHT ? (k < 2 ? C_GREY : C_GREY_L) : SKY[p][k]);
        if (k < 3)
            for (int i = 0; i < w; i++)
                if (i & 1)
                    gfx_pset(fb, x + i, y1 - 1, rainy && p != P_NIGHT ? C_GREY_L : SKY[p][k + 1]);
    }
    if (p == P_NIGHT || p == P_DAWN || p == P_DUSK)
        for (int i = 0; i < 12; i++) {
            uint32_t hh = hash((uint32_t)i * 13 + 5);
            if ((ctx->ms / 500 + (uint32_t)i) % 7 != 0 && !rainy)
                gfx_pset(fb, x + (int)(hh % (uint32_t)w), y + (int)((hh >> 8) % (uint32_t)(h - 12)),
                         i % 3 ? C_MOON : C_STAR_BLUE);
        }
    if (!rainy) {
        if (m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN) {
            float f = (float)(m - LLV_SUNRISE_MIN) / (float)(LLV_SUNSET_MIN - LLV_SUNRISE_MIN);
            int sx = x + 5 + (int)(f * (float)(w - 10)), sy = y + 12 - (int)(sinf(3.14159f * f) * 9.0f);
            gfx_ellipse(fb, sx, sy, 3, 3, C_SUN);
            gfx_ellipse(fb, sx - 1, sy - 1, 1, 1, C_SUN_CORE);
        } else {
            int nm = m >= LLV_SUNSET_MIN ? m - LLV_SUNSET_MIN : m + 1440 - LLV_SUNSET_MIN;
            float f = (float)nm / (float)(1440 - (LLV_SUNSET_MIN - LLV_SUNRISE_MIN));
            int mx = x + 5 + (int)(f * (float)(w - 10)), my = y + 12 - (int)(sinf(3.14159f * f) * 9.0f);
            gfx_ellipse(fb, mx, my, 3, 3, C_MOON);
            gfx_ellipse(fb, mx + 2, my - 1, 2, 2, SKY[p][1]);
        }
    } else {
        gfx_ellipse(fb, x + 12, y + 7, 7, 3, C_GREY_D);
        gfx_ellipse(fb, x + 30, y + 6, 9, 3, C_GREY_D);
    }
    /* colinas del fondo según la estación (o la noche) */
    static const Color HILL[4] = {C_GREEN_L, C_GREEN, C_GRASS_A, C_SNOW};
    Color hc = p == P_NIGHT ? C_HILL_N1 : HILL[season];
    for (int i = 0; i < w; i++) {
        int hy = y + h - 7 + (int)(2.5f * sinf((float)i * 0.19f + 1.0f));
        gfx_rect(fb, x + i, hy, 1, y + h - hy, hc);
        if (p != P_NIGHT)
            gfx_pset(fb, x + i, hy, season == SEASON_WINTER ? C_SNOW_D : C_GREEN_D);
    }
    if (rainy && gs->weather == W_RAIN)
        for (int i = 0; i < 22; i++) {
            uint32_t hh = hash((uint32_t)i * 977 + 13);
            int ry = (int)(((ctx->ms / 14) * 2 + (hh >> 8)) % (uint32_t)h);
            int rx = (int)(hh % (uint32_t)w) - ry / 6 + 4;
            if (rx > 0 && rx < w && ry + 1 < h) {
                gfx_pset(fb, x + rx, y + ry, C_SKY_D2);
                gfx_pset(fb, x + rx - 1, y + ry + 1, C_SKY_D3);
            }
        }
    if (gs->weather == W_SNOW)
        for (int i = 0; i < 16; i++) {
            uint32_t hh = hash((uint32_t)i * 311 + 7);
            int sy = (int)(((ctx->ms / 50) + (hh >> 8)) % (uint32_t)h);
            gfx_pset(fb, x + (int)(hh % (uint32_t)w), y + sy, C_WHITE);
        }
    if (gs->sky == SKY_RAINBOW && p == P_DAY) {
        static const Color BAND[4] = {C_RED, C_SUN, C_GREEN_L, C_SKY_D1};
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < w; i++) {
                float dx = (float)(i - w / 2 - 6), r = 30.0f - (float)k * 1.6f;
                if (dx * dx < r * r) {
                    int ry = h + 6 - (int)sqrtf(r * r - dx * dx);
                    if (ry >= 0 && ry < h - 3)
                        gfx_pset(fb, x + i, y + ry, BAND[k]);
                }
            }
    }
    if (gs->sky == SKY_STAR && p == P_NIGHT) {
        uint32_t t = ctx->ms % 3000;
        if (t < 900) {
            int hx = x + w - 4 - (int)(t * 34 / 900), hy = y + 2 + (int)(t * 14 / 900);
            gfx_pset(fb, hx, hy, C_WHITE);
            gfx_pset(fb, hx + 2, hy - 1, C_SUN_CORE);
            gfx_pset(fb, hx + 4, hy - 2, C_STAR_BLUE);
        }
    }
}

static void scene(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx)
{
    CivilTime c = time_to_civil(ctx->now);
    int m = c.hour * 60 + c.minute;
    Season season = time_season(c.month, c.day, LLV_SOUTH_HEMISPHERE);
    bool night = period_of(m) == P_NIGHT;

    /* pared de tablones */
    gfx_rect(fb, 0, LLV_HUD_H, LLV_W, 46, C_GROUND);
    for (int px = 0; px < LLV_W; px += 10) {
        gfx_rect(fb, px, LLV_HUD_H, 1, 46, C_GROUND_D);
        gfx_rect(fb, px + 1, LLV_HUD_H, 1, 46, C_WOOD_L);
        uint32_t h = hash((uint32_t)px + 3);
        gfx_pset(fb, px + 4 + (int)(h % 3), LLV_HUD_H + 4 + (int)((h >> 4) % 36), C_GROUND_D);
    }
    if (night)
        gfx_dither(fb, 0, LLV_HUD_H, LLV_W, 46, C_GROUND, C_GROUND_D);

    /* ventana */
    int wx = 34, wy = 12, ww = 52, wh = 31;
    gfx_rect(fb, wx - 2, wy - 2, ww + 4, wh + 4, C_WOOD_L);
    gfx_frame(fb, wx - 2, wy - 2, ww + 4, wh + 4, C_GROUND_D);
    window_view(fb, wx, wy, ww, wh, gs, ctx, season, m);
    gfx_rect(fb, wx + 16, wy, 1, wh, C_WOOD_L); /* travesaños a los lados: la mascota va en el panel central */
    gfx_rect(fb, wx + ww - 17, wy, 1, wh, C_WOOD_L);
    gfx_rect(fb, wx, wy + 13, ww, 1, C_WOOD_L);
    /* alféizar */
    gfx_rect(fb, wx - 8, SILL_Y, ww + 16, 4, C_WOOD_L);
    gfx_rect(fb, wx - 8, SILL_Y + 3, ww + 16, 2, C_GROUND_D);
    gfx_rect(fb, wx - 8, SILL_Y, ww + 16, 1, C_SKY_D4);

    /* herramientas colgadas y un farolillo */
    gfx_rect(fb, 12, 16, 2, 26, C_WOOD_L);
    gfx_rect(fb, 9, 40, 8, 6, C_GREY_L);
    gfx_rect(fb, 10, 44, 6, 2, C_GREY);
    gfx_pset(fb, 11, 41, C_WHITE);
    gfx_ellipse(fb, 23, 20, 6, 2, C_LEAF);
    gfx_rect(fb, 20, 17, 7, 3, C_LEAF);
    gfx_rect(fb, 20, 19, 7, 1, C_RED);
    gfx_rect(fb, 100, 14, 1, 7, C_GREY_D);
    gfx_rect(fb, 97, 21, 7, 9, C_GREY_D);
    gfx_rect(fb, 98, 22, 5, 7, night ? C_WINDOW : C_SKY_D4);
    if (night)
        gfx_dither(fb, 95, 19, 11, 13, C_GROUND, C_WINDOW);
    gfx_rect(fb, 97, 21, 7, 1, C_DARK);
    /* maceta de albahaca junto a la ventana */
    gfx_rect(fb, 91, 38, 8, 6, C_ORANGE);
    gfx_rect(fb, 90, 37, 10, 2, C_LEAF);
    gfx_ellipse(fb, 95, 34, 4, 3, C_GREEN);
    gfx_pset(fb, 93, 33, C_GREEN_L);
    gfx_pset(fb, 97, 35, C_GREEN_D);
}

/* ---------- paneles ---------- */

static void bracket(Framebuffer *fb, int x, int y, int w, int h, Color c)
{
    for (int k = 0; k < 4; k++) {
        gfx_pset(fb, x + k, y, c);
        gfx_pset(fb, x, y + k, c);
        gfx_pset(fb, x + w - 1 - k, y, c);
        gfx_pset(fb, x + w - 1, y + k, c);
        gfx_pset(fb, x + k, y + h - 1, c);
        gfx_pset(fb, x, y + h - 1 - k, c);
        gfx_pset(fb, x + w - 1 - k, y + h - 1, c);
        gfx_pset(fb, x + w - 1, y + h - 1 - k, c);
    }
}

static void countdown(char *buf, size_t n, const GameState *gs)
{
    if (gs->bag_count >= LLV_BAG_SIZE) {
        snprintf(buf, n, "Bolsa llena");
        return;
    }
    int min = gs->refill_left * (LLV_TICK_SECONDS / 60);
    snprintf(buf, n, "+1 en %d h %02d min", min / 60, min % 60);
}

static void panel_bag(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx, bool show_cursor)
{
    gfx_textbox(fb, 3, PANEL_TOP, 114, 32);
    int sel = cursor % positions(gs);
    for (int i = 0; i < LLV_BAG_SIZE; i++) {
        int x = 8 + i * 22, y = PANEL_TOP + 3;
        gfx_rect(fb, x, y, 16, 16, C_SKY_D4);
        gfx_frame(fb, x, y, 16, 16, i < gs->bag_count ? C_GROUND : C_GREY_L);
        if (i < gs->bag_count) {
            art_item(fb, x + 2, y + 2, (ItemKind)gs->bag[i], ctx->ms);
            if (gs->prepared == gs->bag[i] && gs->prepared != ITEM_NONE) {
                /* preparado: estrellita en la esquina */
                gfx_rect(fb, x + 11, y + 1, 4, 4, C_SUN);
                gfx_pset(fb, x + 12, y + 2, C_WHITE);
                gfx_frame(fb, x + 10, y, 6, 6, C_ORANGE);
            }
        }
        if (show_cursor && i == sel && ((ctx->ms / 300) & 1) == 0)
            bracket(fb, x - 1, y - 1, 18, 18, C_RED);
    }
    char buf[32];
    if (show_cursor && on_collection(gs)) {
        /* sobre la colección: va nombrando lo que hay en la estantería */
        int total = collected(gs);
        if (total == 0)
            snprintf(buf, sizeof(buf), "Colección vacía");
        else {
            int want = (int)(ctx->ms / 1400) % total, seen = 0, slot = 0;
            for (int i = 0; i < COLL_COUNT; i++)
                if ((gs->collection >> i) & 1) {
                    if (seen++ == want) {
                        slot = i;
                        break;
                    }
                }
            snprintf(buf, sizeof(buf), "%s", state_coll_name((CollSlot)slot));
        }
    } else if (show_cursor && sel < gs->bag_count && ctx->ms % 4000 >= 2400) {
        /* sobre un objeto: alterna su nombre con la cuenta atrás (siempre a la vista) */
        snprintf(buf, sizeof(buf), "%s%s", state_item_name((ItemKind)gs->bag[sel]),
                 gs->prepared == gs->bag[sel] ? " (preparado)" : "");
    } else {
        countdown(buf, sizeof(buf), gs);
    }
    bool is_name = show_cursor && (on_collection(gs) || (sel < gs->bag_count && ctx->ms % 4000 >= 2400));
    /* azul: lo seleccionado; gris: la cuenta atrás de la bolsa */
    gfx_text_center(fb, PANEL_TOP + 22, buf, is_name ? C_BLUE_TXT : C_GREY);
}

static void panel_collection(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx, bool show_cursor)
{
    gfx_textbox(fb, 3, 88, 114, 32);
    for (int i = 0; i < COLL_COUNT; i++) {
        int x = 8 + (i % 8) * 13, y = 92 + (i / 8) * 13;
        gfx_rect(fb, x, y, 12, 11, C_SKY_D4);
        art_coll_icon(fb, x, y, (CollSlot)i, (gs->collection >> i) & 1);
        gfx_frame(fb, x - 1, y - 1, 14, 13, C_GREY_L);
    }
    if (show_cursor && on_collection(gs) && ((ctx->ms / 300) & 1) == 0)
        bracket(fb, 4, 89, 112, 30, C_RED);
}

static void panel_choose(Framebuffer *fb, const WorldCtx *ctx)
{
    gfx_textbox(fb, 3, PANEL_TOP, 114, 65);
    char title[24];
    snprintf(title, sizeof(title), "ELIGE: %s", pet_name((PetKind)choose));
    gfx_text_center(fb, PANEL_TOP + 4, title, C_BLUE_TXT);
    static const uint16_t AGE_FOR[PET_COUNT] = {0, 11000, 8000, 5500, 9000};
    for (int k = 1; k < PET_COUNT; k++) {
        int cx = 20 + (k - 1) * 27;
        Pet p;
        memset(&p, 0, sizeof(p));
        p.kind = (uint8_t)k;
        p.age = AGE_FOR[k];
        if (k == choose)
            bracket(fb, cx - 12, PANEL_TOP + 11, 25, 25, ((ctx->ms / 300) & 1) ? C_RED : C_SUN);
        art_pet(fb, cx, PANEL_TOP + 32, &p, ctx->ms, 0, 0, 0);
    }
    gfx_text_center(fb, PANEL_TOP + 37, pet_blurb((PetKind)choose), C_DARK);
    gfx_text_center(fb, PANEL_TOP + 46, "A: ver otro", C_GREY);
    gfx_text_center(fb, PANEL_TOP + 54, "Mantén A: elegir", C_GREY);
}

static void panel_ficha(Framebuffer *fb, const GameState *gs, const WorldCtx *ctx)
{
    char lines[10][24];
    int n = pet_ficha(&gs->pet, lines, 10);
    int split = n;
    for (int i = 0; i < n; i++)
        if (!strcmp(lines[i], "Humor:")) {
            split = i;
            break;
        }
    bool page_b = (ctx->ms / 3600) & 1;
    gfx_textbox(fb, 3, PANEL_TOP, 114, 65);
    int y = PANEL_TOP + 5, from = page_b ? split : 0, to = page_b ? n : split;
    for (int i = from; i < to; i++) {
        Color col = (i == 0) ? C_BLUE_TXT : (page_b && i == split + 1) ? C_BLUE_TXT : C_DARK;
        gfx_text(fb, 9, y, lines[i], col);
        y += GFX_LINE_H;
    }
    gfx_rect(fb, 100, PANEL_TOP + 6, 3, 3, page_b ? C_GREY_L : C_BLUE_TXT);
    gfx_rect(fb, 105, PANEL_TOP + 6, 3, 3, page_b ? C_BLUE_TXT : C_GREY_L);
    bool cared = gs->actions_used[ctx->index] >= LLV_PET_ACTIONS;
    gfx_text(fb, 9, PANEL_TOP + 53, page_b ? (cared ? "Mañana, más." : "Mantén: cuidar") : "A: respirar", C_GREY);
}

static void panel_breath(Framebuffer *fb, const WorldCtx *ctx)
{
    gfx_textbox(fb, 3, PANEL_TOP, 114, 65);
    gfx_text_center(fb, PANEL_TOP + 5, "RESPIRAR JUNTOS", C_BLUE_TXT);
    bool in = (ctx->buttons & BTN_A) != 0;
    gfx_text_center(fb, PANEL_TOP + 20, in ? "Inhala…" : "Exhala…", in ? C_RED : C_BLUE_TXT);
    gfx_rect(fb, 12, PANEL_TOP + 33, 96, 8, C_SKY_D4);
    gfx_rect(fb, 13, PANEL_TOP + 34, 94 * breath_level / 1000, 6, in ? C_RED : C_SKY_D0);
    gfx_frame(fb, 12, PANEL_TOP + 33, 96, 8, C_GREY);
    char buf[32];
    snprintf(buf, sizeof(buf), "Respiraciones: %d", breath_cycles);
    gfx_text_center(fb, PANEL_TOP + 44, buf, C_DARK);
    gfx_text_center(fb, PANEL_TOP + 53, "Toca A para salir", C_GREY);
}

static void panel_menu(Framebuffer *fb, const GameState *gs)
{
    gfx_textbox(fb, 3, 11, 114, 108);
    gfx_text_center(fb, 16, "AJUSTES", C_BLUE_TXT);
    for (int r = 0; r < MENU_ROWS; r++) {
        int y = 27 + r * 9;
        char line[32];
        bool on;
        if (r < HAB_COUNT) {
            on = habit_enabled(gs, r);
            snprintf(line, sizeof(line), "[%c] %s", on ? 'x' : ' ', habit_title(r));
        } else if (r == HAB_COUNT) {
            on = (gs->flags & SF_GRANDMA) != 0;
            snprintf(line, sizeof(line), "[%c] Apto abuela", on ? 'x' : ' ');
        } else {
            snprintf(line, sizeof(line), "Salir");
        }
        if (r == menu_row)
            gfx_rect(fb, 7, y - 1, 106, 9, C_BLUE_TXT);
        gfx_text(fb, 11, y, line, r == menu_row ? C_WHITE : C_DARK);
    }
    gfx_text_center(fb, 108, "A: mover  Mant: ok", C_GREY);
}

/* Estado visual de la respiración: solo depende de lo que pasa en pantalla. */
static void breath_update(const WorldCtx *ctx)
{
    uint32_t dt = ctx->ms - breath_last_ms;
    if (dt > 200)
        dt = 200;
    breath_last_ms = ctx->ms;
    int d = (int)(dt * 1000 / 3500);
    breath_level += (ctx->buttons & BTN_A) ? d : -d;
    if (breath_level > 1000)
        breath_level = 1000;
    if (breath_level < 0)
        breath_level = 0;
    if (breath_level >= 950)
        breath_full = true;
    if (breath_level <= 50 && breath_full) {
        breath_full = false;
        breath_cycles++;
    }
}

static void cobertizo_render(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb)
{
    scene(fb, gs, ctx);
    const Pet *p = &gs->pet;
    if (p->kind == PET_NONE) {
        panel_choose(fb, ctx);
        return;
    }
    if (menu_open) {
        panel_menu(fb, gs);
        return;
    }
    if (breathing)
        breath_update(ctx);
    CivilTime c = time_to_civil(ctx->now);
    int m = c.hour * 60 + c.minute;
    int lean = 0;
    if (m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN) /* la semilla se gira hacia el sol */
        lean = (int)lroundf((((float)(m - LLV_SUNRISE_MIN) / (float)(LLV_SUNSET_MIN - LLV_SUNRISE_MIN)) - 0.5f) * 4.8f);
    int inflate = breathing ? breath_level : 0;
    int joy = ms_pending(ctx->ms, joy_until) ? 600 : (breathing && breath_cycles > 0 ? 500 : (breathing ? breath_level / 4 : 0));
    art_pet(fb, PET_X, SILL_Y, p, ctx->ms, inflate, joy, lean);
    if (on_pet(gs) || breathing)
        bracket(fb, PET_X - 17, 14, 35, SILL_Y - 12, (ctx->ms / 300) & 1 ? C_RED : C_SUN);

    if (breathing) {
        panel_breath(fb, ctx);
    } else if (on_pet(gs)) {
        panel_ficha(fb, gs, ctx);
    } else {
        panel_bag(fb, gs, ctx, true);
        panel_collection(fb, gs, ctx, true);
    }
}

const World world_cobertizo = {
    .name = "Cobertizo",
    .daily_actions = LLV_PET_ACTIONS,
    .tick = pet_tick,
    .render = cobertizo_render,
    .on_tap = cobertizo_tap,
    .on_hold = cobertizo_hold,
};
