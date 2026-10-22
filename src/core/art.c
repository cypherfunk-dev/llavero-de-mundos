/* Sprites y dibujo procedural del Jardín, el Cobertizo y la mascota.
 * Los sprites son de 4 colores (spec §12.2): '.' transparente y '1' '2' '3' -> pal[0..2],
 * con la paleta elegida según la estación o la especie. Animaciones de 2 frames. */
#include "art.h"

#include <string.h>

#include "pet.h"

/* ---------- sprites ---------- */

static const char *const S_CANOPY_A_ROWS[] = {
    "....111111.....",
    "...13333331....",
    "...1333323311..",
    ".1133333222221.",
    "13323322322221.",
    "133323322222111",
    "132322222212211",
    "13222222221111.",
    "12322122112111.",
    ".112221121111..",
    "...11211111....",
    ".....1111......",
};
static const Sprite S_CANOPY_A = {15, 12, S_CANOPY_A_ROWS};

static const char *const S_CANOPY_B_ROWS[] = {
    "......11.......",
    "....113311.....",
    "...133332211...",
    "...1332332231..",
    ".1133332222211.",
    "13332232222221.",
    "132332222212211",
    "133222221221121",
    "12322222212111.",
    "12222211211111.",
    ".122122112111..",
    "..11111111.....",
    ".....1111......",
};
static const Sprite S_CANOPY_B = {15, 13, S_CANOPY_B_ROWS};

static const char *const S_YOUNG_A_ROWS[] = {
    "....11.....",
    "...1321....",
    "..1333211..",
    ".133232221.",
    "12332222221",
    "13223221121",
    "1322212211.",
    ".122221111.",
    "..111211...",
    "....11.....",
};
static const Sprite S_YOUNG_A = {11, 10, S_YOUNG_A_ROWS};

static const char *const S_YOUNG_B_ROWS[] = {
    "...1111....",
    "..133331...",
    "..13322311.",
    ".132332221.",
    "13332222211",
    "12232221221",
    "1322222111.",
    "122221121..",
    ".1112211...",
    "....11.....",
};
static const Sprite S_YOUNG_B = {11, 10, S_YOUNG_B_ROWS};

static const char *const S_STONE_ROWS[] = {
    "...111111...",
    ".1133323211.",
    "133323222221",
    "132323222221",
    "132322222211",
    "132222222211",
    "122222221111",
    "122222121111",
    ".1121211111.",
    "...111111...",
};
static const Sprite S_STONE = {12, 10, S_STONE_ROWS};

static const char *const S_APPLE_ROWS[] = {
    "...1...",
    ".11311.",
    ".13221.",
    "1322221",
    ".12211.",
    ".11111.",
    "...1...",
};
static const Sprite S_APPLE = {7, 7, S_APPLE_ROWS};

static const char *const S_APPLE_ROT_ROWS[] = {
    "...1...",
    ".113.1.",
    ".13221.",
    "1322221",
    ".12211.",
    ".11111.",
    "...1...",
};
static const Sprite S_APPLE_ROT = {7, 7, S_APPLE_ROT_ROWS};

static const char *const S_SPROUT_A_ROWS[] = {
    "........1..",
    "..1...11211",
    "11211111211",
    "112111..1..",
    "..1..1.....",
    ".....1.....",
    ".....1.....",
    "....111....",
};
static const Sprite S_SPROUT_A = {11, 8, S_SPROUT_A_ROWS};

static const char *const S_SPROUT_B_ROWS[] = {
    "........1..",
    "......11211",
    "......11211",
    "..1.....1..",
    "112111.....",
    "112111.....",
    "..1..1.....",
    ".....1.....",
    "....111....",
};
static const Sprite S_SPROUT_B = {11, 9, S_SPROUT_B_ROWS};

static const char *const S_SEED_ROWS[] = {
    ".1133111.",
    "133122221",
    "122222111",
    ".1111111.",
};
static const Sprite S_SEED = {9, 4, S_SEED_ROWS};

static const char *const S_BIRD_A_ROWS[] = {
    "....111.....",
    "..1133211...",
    ".133222121..",
    "122222222111",
    "1122222111..",
    "1.1121111...",
    "....111.....",
    "....1.1.....",
    "....1111....",
};
static const Sprite S_BIRD_A = {12, 9, S_BIRD_A_ROWS};

static const char *const S_BIRD_B_ROWS[] = {
    "....111.....",
    "..1133211...",
    ".133222221..",
    "1222222121..",
    "112222211111",
    "1.1121111...",
    "....111.....",
    "....1.1.....",
    "....1111....",
};
static const Sprite S_BIRD_B = {12, 9, S_BIRD_B_ROWS};

static const char *const S_FROG_A_ROWS[] = {
    ".11.....11.",
    ".13.....31.",
    "..1111111..",
    "11333222211",
    "12322222211",
    "11221111111",
    "..1111111..",
};
static const Sprite S_FROG_A = {11, 7, S_FROG_A_ROWS};

static const char *const S_FROG_B_ROWS[] = {
    ".11.....11.",
    ".11.....11.",
    "..1111111..",
    "11333222211",
    "12322222211",
    "11221111111",
    "..1111111..",
};
static const Sprite S_FROG_B = {11, 7, S_FROG_B_ROWS};


static const char *const S_TRUNK_ROWS[] = {"..11..", ".1221.", "..21..", ".1221.", "122221"};
static const Sprite S_TRUNK = {6, 5, S_TRUNK_ROWS};
static const char *const S_TRUNK_S_ROWS[] = {"..11..", "..21..", ".1221."};
static const Sprite S_TRUNK_S = {6, 3, S_TRUNK_S_ROWS};

static const char *const S_MUSH_S_ROWS[] = {"..111..", ".12221.", "1232221", "1222321",
                                            ".11311.", "..131..", "..111.."};
static const Sprite S_MUSH_S = {7, 7, S_MUSH_S_ROWS};
static const char *const S_MUSH_L_ROWS[] = {
    "...11111...", "..1222221..", ".122322231.", "12222322221", "12232222231",
    ".111111111.", "...13331...", "...13331...", "...13331...", "...11111...",
};
static const Sprite S_MUSH_L = {11, 10, S_MUSH_L_ROWS};

static const char *const S_BUD_ROWS[] = {"...3...", "..323..", "..333..", "...1...",
                                         "..11...", "...11..", "...1...", "...1..."};
static const Sprite S_BUD = {7, 8, S_BUD_ROWS};
static const char *const S_FLOWER_A_ROWS[] = {
    "...3.3...", "..33333..", ".3322233.", "..33333..", "...3.3...",
    "....1....", "...11....", "....11...", "....1....", "....1....",
};
static const Sprite S_FLOWER_A = {9, 10, S_FLOWER_A_ROWS};
static const char *const S_FLOWER_B_ROWS[] = {
    "....3.3..", "...33333.", "..3322233", "...33333.", "....3.3..",
    "....1....", "...11....", "....11...", "....1....", "....1....",
};
static const Sprite S_FLOWER_B = {9, 10, S_FLOWER_B_ROWS};

static const char *const S_BEE_A_ROWS[] = {"..33.33.", "...3.3..", ".122122.", "12212211", ".122122."};
static const Sprite S_BEE_A = {8, 5, S_BEE_A_ROWS};
static const char *const S_BEE_B_ROWS[] = {"........", "..3333..", ".122122.", "12212211", ".122122."};
static const Sprite S_BEE_B = {8, 5, S_BEE_B_ROWS};

static const char *const S_STAR_ROWS[] = {
    "....1....", "...131...", "...131...", "111133111", "133333331",
    ".1333331.", "..13331..", ".131.131.", ".11...11.",
};
static const Sprite S_STAR = {9, 9, S_STAR_ROWS};

static const char *const S_HEART_ROWS[] = {".1.1.", "12121", ".121.", "..1.."};
static const Sprite S_HEART = {5, 4, S_HEART_ROWS};

static const char *const S_NUBE_ROWS[] = {
    "....1111....", "..11333311..", ".13333333311", "133333333331", "122333333221",
    ".1222222221.", "..11111111..", "...1..1..1..", "..1..1..1...",
};
static const Sprite S_NUBE = {12, 9, S_NUBE_ROWS};

/* ---------- utilidades ---------- */

static void spr(Framebuffer *fb, int x, int y, const Sprite *s, Color a, Color b, Color c)
{
    Color pal[3] = {a, b, c};
    gfx_sprite(fb, x, y, s, pal, 1);
}

static void spr_dir(Framebuffer *fb, int x, int y, const Sprite *s, Color a, Color b, Color c, bool flip)
{
    Color pal[3] = {a, b, c};
    if (flip)
        gfx_sprite_flip(fb, x, y, s, pal);
    else
        gfx_sprite(fb, x, y, s, pal, 1);
}

static const int8_t WAVE[8] = {0, 1, 2, 1, 0, -1, -2, -1};

/* Sombra con dithering: deja ver el suelo y da volumen sin gastar colores. */
static void shadow(Framebuffer *fb, int cx, int cy, int rx, Season season)
{
    Color c = season == SEASON_AUTUMN ? C_GRASS_A_D : season == SEASON_WINTER ? C_SNOW_D : C_GREEN_D;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -rx; dx <= rx; dx++)
            if (dx * dx * 4 + dy * dy * rx * rx * 4 <= rx * rx * 4 + rx && ((cx + dx + cy + dy) & 1) == 0)
                gfx_pset(fb, cx + dx, cy + dy, c);
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

static const Color CANOPY_PAL[4][3] = {
    [SEASON_SPRING] = {C_GREEN_D, C_GREEN, C_PINK},
    [SEASON_SUMMER] = {C_GREEN_D, C_GREEN, C_GREEN_L},
    [SEASON_AUTUMN] = {C_GROUND_D, C_ORANGE, C_LEAF},
    [SEASON_WINTER] = {C_GREY, C_SNOW_D, C_SNOW},
};

static void canopy(Framebuffer *fb, int x, int y, const Sprite *s, Season season)
{
    spr(fb, x, y, s, CANOPY_PAL[season][0], CANOPY_PAL[season][1], CANOPY_PAL[season][2]);
}

static void apple_dot(Framebuffer *fb, int x, int y)
{
    gfx_rect(fb, x, y, 3, 3, C_RED);
    gfx_pset(fb, x, y, C_PINK);
    gfx_pset(fb, x + 2, y + 2, C_ROOF);
}

static void puddle(Framebuffer *fb, int x, int y, uint32_t ms, uint32_t var)
{
    gfx_ellipse(fb, x + 8, y + 11, 7, 3, C_SKY_D0);
    gfx_ellipse(fb, x + 8, y + 11, 6, 2, C_SKY_D1);
    gfx_pset(fb, x + 5, y + 10, C_WHITE);
    gfx_pset(fb, x + 6, y + 10, C_WHITE);
    int r = (int)((ms / 260 + var) % 6); /* ondas que se abren */
    if (r < 4) {
        for (int i = -r; i <= r; i++) {
            gfx_pset(fb, x + 10 + i, y + 11 - (r > 1 ? 1 : 0), C_SKY_D3);
            if (r > 1)
                gfx_pset(fb, x + 10 + i, y + 11 + 1, C_SKY_D3);
        }
    }
}

static void worm(Framebuffer *fb, int x, int y, uint32_t ms, uint32_t var)
{
    for (int k = 0; k < 8; k++) {
        int dy = WAVE[(k + (int)(ms / 140) + (int)var) & 7] / 2;
        Color c = k == 7 ? C_ROOF : (k == 6 ? C_ITEM_LOMBRIZ : (k & 1 ? C_MAGENTA : C_ITEM_LOMBRIZ));
        if (k == 6)
            c = C_ITEM_LOMBRIZ;
        gfx_pset(fb, x + k, y + dy, c);
        gfx_pset(fb, x + k, y + dy + 1, k & 1 ? C_ITEM_LOMBRIZ : C_MAGENTA);
    }
}

static void leaves(Framebuffer *fb, int x, int y, uint32_t var)
{
    static const Color LC[4] = {C_ORANGE, C_LEAF, C_GROUND, C_RED};
    for (int i = 0; i < 9; i++) {
        uint32_t h = hash(var * 31 + (uint32_t)i);
        int px = x + 2 + (int)(h % 11), py = y + 8 + (int)((h >> 8) % 6);
        Color c = LC[(h >> 16) & 3];
        gfx_pset(fb, px, py, c);
        gfx_pset(fb, px + 1, py, c);
        if (i % 3 == 0)
            gfx_pset(fb, px, py + 1, C_GROUND_D);
    }
}

static void snow_mound(Framebuffer *fb, int x, int y)
{
    gfx_ellipse(fb, x + 8, y + 12, 7, 3, C_SNOW_D);
    gfx_ellipse(fb, x + 8, y + 11, 6, 3, C_SNOW);
    gfx_pset(fb, x + 11, y + 10, C_WHITE);
}

static void moss(Framebuffer *fb, int x, int y, uint32_t var)
{
    for (int i = 0; i < 10; i++) {
        uint32_t h = hash(var * 17 + (uint32_t)i);
        int px = x + 3 + (int)(h % 8), py = y + 6 + (int)((h >> 8) % 3);
        gfx_pset(fb, px, py, (h >> 16) & 1 ? C_GREEN : C_GREEN_L);
    }
}

static const Color FLOWER_COLOR[3] = {C_PINK, C_WHITE, C_SKY_D1};

void art_entity(Framebuffer *fb, const Entity *e, int x, int y, Season season, uint32_t ms, uint32_t var)
{
    int f = (int)((ms / 520 + var) & 1);
    switch (e->sp) {
    case SP_SEMILLA:
        spr(fb, x + 3, y + 10, &S_SEED, C_GROUND_D, C_GROUND, C_LEAF);
        break;
    case SP_BROTE:
        shadow(fb, x + 8, y + 14, 3, season);
        spr(fb, x + 2, y + 5, f ? &S_SPROUT_B : &S_SPROUT_A, C_GREEN_D,
            season == SEASON_WINTER ? C_GREEN : C_GREEN_L, C_SUN_CORE);
        break;
    case SP_ARBOL_JOVEN:
        shadow(fb, x + 8, y + 14, 5, season);
        spr(fb, x + 5, y + 10, &S_TRUNK_S, C_GROUND_D, C_GROUND, C_WOOD_L);
        canopy(fb, x + 3, y + 3 + f, f ? &S_YOUNG_B : &S_YOUNG_A, season);
        break;
    case SP_ARBOL:
        if (e->st == 2) { /* viejo: tronco hueco y retoños de hongo */
            shadow(fb, x + 8, y + 14, 6, season);
            spr(fb, x + 5, y + 3, &S_TRUNK_S, C_GROUND_D, C_GROUND, C_WOOD_L);
            spr(fb, x + 5, y + 6, &S_TRUNK, C_GROUND_D, C_GROUND, C_WOOD_L);
            spr(fb, x + 5, y + 10, &S_TRUNK_S, C_GROUND_D, C_GROUND, C_WOOD_L);
            gfx_rect(fb, x + 2, y + 5, 3, 1, C_GROUND_D);
            gfx_rect(fb, x + 11, y + 4, 3, 1, C_GROUND_D);
            gfx_pset(fb, x + 2, y + 4, C_GROUND_D);
            gfx_pset(fb, x + 13, y + 3, C_GROUND_D);
            gfx_rect(fb, x + 7, y + 8, 2, 3, C_HOUSE); /* el hueco */
            spr(fb, x + 9, y + 9, &S_MUSH_S, C_ROOF, C_RED, C_WHITE);
            break;
        }
        shadow(fb, x + 8, y + 14, 7, season);
        spr(fb, x + 5, y + 8, &S_TRUNK, C_GROUND_D, C_GROUND, C_WOOD_L);
        canopy(fb, x + 1, y - 4, f ? &S_CANOPY_B : &S_CANOPY_A, season);
        if (e->st == 1) {
            apple_dot(fb, x + 3, y + 0);
            apple_dot(fb, x + 9, y + 1 + f);
            apple_dot(fb, x + 6, y + 4);
        }
        break;
    case SP_MANZANA:
        shadow(fb, x + 8, y + 14, 3, season);
        if (e->st == 0) {
            spr(fb, x + 5, y + 8, &S_APPLE, C_ROOF, C_RED, C_PINK);
        } else {
            spr(fb, x + 5, y + 8, &S_APPLE_ROT, C_ROOF, C_APPLE_ROT, C_GROUND);
            int t = (int)(ms / 110 + var * 5);
            gfx_pset(fb, x + 5 + WAVE[t & 7] + 3, y + 5 + WAVE[(t + 2) & 7] / 2, C_DARK); /* moscas */
            gfx_pset(fb, x + 11 - WAVE[(t + 3) & 7], y + 6 + WAVE[(t + 5) & 7] / 2, C_DARK);
        }
        break;
    case SP_HONGO:
        if (e->st == 0) {
            shadow(fb, x + 8, y + 14, 3, season);
            spr(fb, x + 4, y + 8, &S_MUSH_S, C_ROOF, C_RED, C_WHITE);
        } else {
            shadow(fb, x + 8, y + 14, 4, season);
            spr(fb, x + 2, y + 5, &S_MUSH_L, C_ROOF, C_RED, C_WHITE);
        }
        break;
    case SP_FLOR: {
        Color pc = FLOWER_COLOR[var % 3];
        shadow(fb, x + 8, y + 14, 3, season);
        if (e->st == 0)
            spr(fb, x + 5, y + 7, &S_BUD, C_GREEN_D, C_SUN, pc);
        else if (e->st == 2)
            spr(fb, x + 5, y + 8, &S_BUD, C_GROUND_D, C_GROUND, C_LEAF);
        else if (e->st == 1)
            spr(fb, x + 3, y + 4, f ? &S_FLOWER_B : &S_FLOWER_A, C_GREEN_D, C_SUN, pc);
        else {
            spr(fb, x + 3, y + 4, f ? &S_FLOWER_B : &S_FLOWER_A, C_GREEN_D, C_CYAN, C_VIOLET);
            int t = (int)(ms / 180 + var);
            gfx_pset(fb, x + 1 + (t % 3) * 5, y + 3 + (t % 2) * 5, C_WHITE);
            gfx_pset(fb, x + 13 - (t % 4) * 3, y + 7 - (t % 3) * 2, C_SUN_CORE);
        }
        break;
    }
    case SP_ABEJA: {
        int t = (int)(ms / 75 + var * 3);
        int dx = WAVE[t & 7] * 3, dy = WAVE[(t + 2) & 7] * 2;
        gfx_pset(fb, x + 8 + dx, y + 13, C_GREEN_D); /* sombra */
        spr(fb, x + 4 + dx, y + 3 + dy, ((ms / 60) & 1) ? &S_BEE_A : &S_BEE_B, C_DARK, C_SUN, C_WHITE);
        break;
    }
    case SP_PAJARO: {
        int peck = (int)((ms / 330 + var) & 1);
        int hop = ((ms / 700 + var) % 5 == 0) ? 2 : 0;
        shadow(fb, x + 8, y + 14, 4, season);
        spr_dir(fb, x + 2, y + 6 - hop, peck ? &S_BIRD_B : &S_BIRD_A, C_DARK, C_BIRD, C_BIRD_L, var & 2);
        if (e->st == 1) {
            gfx_pset(fb, x + 12, y + 14, C_ITEM_ALPISTE);
            gfx_pset(fb, x + 13, y + 13, C_ITEM_ALPISTE);
            gfx_pset(fb, x + 11, y + 15, C_LEAF);
        }
        break;
    }
    case SP_LOMBRIZ:
        worm(fb, x + 4, y + 11, ms, var);
        break;
    case SP_CHARCO:
        puddle(fb, x, y, ms, var);
        break;
    case SP_RANA: {
        int hop = ((ms / 600 + var) % 4 == 0) ? 3 : 0;
        shadow(fb, x + 8, y + 14, 4, season);
        spr(fb, x + 2, y + 7 - hop, ((ms / 900 + var) % 5 == 0) ? &S_FROG_B : &S_FROG_A, C_GREEN_D, C_GREEN,
            C_SUN_CORE);
        break;
    }
    case SP_PIEDRA:
        shadow(fb, x + 8, y + 14, 6, season);
        spr(fb, x + 2, y + 5, &S_STONE, C_GREY_D, C_STONE, C_STONE_L);
        moss(fb, x, y, var);
        break;
    case SP_HOJAS:
        leaves(fb, x, y, var);
        break;
    case SP_NIEVE:
        snow_mound(fb, x, y);
        break;
    default:
        break;
    }
}

/* ---------- iconos ---------- */

void art_star(Framebuffer *fb, int x, int y, Color body, Color edge)
{
    spr(fb, x, y, &S_STAR, edge, body, C_SUN_CORE);
}

static void worm_icon(Framebuffer *fb, int x, int y, uint32_t ms)
{
    for (int k = 0; k < 9; k++) {
        int dy = WAVE[(k + (int)(ms / 160)) & 7];
        gfx_pset(fb, x + k, y + dy, k & 1 ? C_MAGENTA : C_ITEM_LOMBRIZ);
        gfx_pset(fb, x + k, y + dy + 1, C_ITEM_LOMBRIZ);
    }
    gfx_pset(fb, x + 8, y + WAVE[(8 + (int)(ms / 160)) & 7], C_DARK);
}

void art_item(Framebuffer *fb, int x, int y, ItemKind item, uint32_t ms)
{
    switch (item) {
    case ITEM_NUBE:
        spr(fb, x, y + 1, &S_NUBE, C_BLUE_TXT, C_SKY_D2, C_WHITE);
        if (ms / 200 % 3 == 0)
            gfx_pset(fb, x + 3, y + 11, C_SKY_D0);
        break;
    case ITEM_VIENTO:
        for (int k = 0; k < 3; k++) {
            int ly = y + 2 + k * 3, xs = x + k, xe = x + 9 - k;
            gfx_rect(fb, xs, ly, xe - xs, 1, C_WHITE);
            gfx_rect(fb, xs + 1, ly + 1, xe - xs, 1, C_SKY_D0);
            gfx_pset(fb, xe, ly - 1, C_WHITE);
            gfx_pset(fb, xe + 1, ly - 1, C_WHITE);
            gfx_pset(fb, xe + 2, ly, C_WHITE);
        }
        break;
    case ITEM_SEMILLA:
        spr(fb, x, y, &S_SPROUT_A, C_GREEN_D, C_GREEN_L, C_SUN_CORE);
        spr(fb, x + 1, y + 7, &S_SEED, C_GROUND_D, C_GROUND, C_LEAF);
        break;
    case ITEM_ALPISTE:
        gfx_ellipse(fb, x + 6, y + 8, 5, 4, C_GROUND_D);
        gfx_ellipse(fb, x + 6, y + 8, 4, 3, C_ITEM_ALPISTE);
        gfx_rect(fb, x + 4, y + 2, 5, 3, C_ITEM_ALPISTE);
        gfx_rect(fb, x + 3, y + 4, 7, 1, C_RED);
        gfx_pset(fb, x + 4, y + 7, C_LEAF);
        gfx_pset(fb, x + 7, y + 9, C_GROUND);
        gfx_pset(fb, x + 6, y + 6, C_WHITE);
        break;
    case ITEM_LOMBRIZ:
        worm_icon(fb, x + 1, y + 5, ms);
        break;
    case ITEM_DESEO:
        spr(fb, x + 1, y + 1, &S_STAR, C_ORANGE, C_LEAF, C_SUN);
        if ((ms / 250) & 1)
            gfx_pset(fb, x + 10, y + 1, C_WHITE);
        else
            gfx_pset(fb, x + 1, y + 9, C_WHITE);
        break;
    default:
        break;
    }
}

static void fish(Framebuffer *fb, int x, int y, Color body, Color edge)
{
    gfx_ellipse(fb, x + 5, y + 4, 5, 3, edge);
    gfx_ellipse(fb, x + 5, y + 4, 4, 2, body);
    gfx_pset(fb, x + 10, y + 1, edge);
    gfx_rect(fb, x + 9, y + 2, 2, 5, edge);
    gfx_pset(fb, x + 9, y + 4, body);
    gfx_pset(fb, x + 2, y + 3, edge == C_GREY_D ? C_GREY_D : C_DARK);
}

void art_coll_icon(Framebuffer *fb, int x, int y, CollSlot slot, bool known)
{
    Color d = C_GREY_D;
    switch (slot) {
    case COLL_MANZANA:
        spr(fb, x + 2, y + 1, &S_APPLE, known ? C_ROOF : d, known ? C_RED : d, known ? C_PINK : d);
        break;
    case COLL_HONGO:
        spr(fb, x, y, &S_MUSH_L, known ? C_ROOF : d, known ? C_RED : d, known ? C_WHITE : d);
        break;
    case COLL_FLOR:
        spr(fb, x + 1, y, &S_FLOWER_A, known ? C_GREEN_D : d, known ? C_SUN : d, known ? C_PINK : d);
        break;
    case COLL_FLOR_RARA:
        spr(fb, x + 1, y, &S_FLOWER_A, known ? C_GREEN_D : d, known ? C_CYAN : d, known ? C_VIOLET : d);
        break;
    case COLL_ABEJA:
        spr(fb, x + 2, y + 2, &S_BEE_A, known ? C_DARK : d, known ? C_SUN : d, known ? C_WHITE : d);
        break;
    case COLL_PAJARO:
        spr(fb, x, y, &S_BIRD_A, known ? C_DARK : d, known ? C_BIRD : d, known ? C_BIRD_L : d);
        break;
    case COLL_LOMBRIZ:
        if (known)
            worm_icon(fb, x + 1, y + 3, 0);
        else
            gfx_rect(fb, x + 1, y + 4, 9, 2, d);
        break;
    case COLL_RANA:
        spr(fb, x, y + 1, &S_FROG_A, known ? C_GREEN_D : d, known ? C_GREEN : d, known ? C_SUN_CORE : d);
        break;
    case COLL_ESTRELLA:
        spr(fb, x + 1, y, &S_STAR, known ? C_ORANGE : d, known ? C_LEAF : d, known ? C_SUN : d);
        break;
    case COLL_ARCOIRIS: {
        static const Color R[4] = {C_RED, C_LEAF, C_GREEN_L, C_SKY_D0};
        for (int k = 0; k < 4; k++) {
            int r = 6 - k;
            for (int j = 0; j <= r; j++)
                for (int i = -r; i <= r; i++)
                    if (i * i + j * j >= r * r - r && i * i + j * j <= r * r + r / 2)
                        gfx_pset(fb, x + 5 + i, y + 8 - j, known ? R[k] : d);
        }
        break;
    }
    default:
        fish(fb, x + 1, y + 1, known ? C_CYAN : d, known ? C_BLUE_TXT : d);
        break;
    }
}

/* ---------- mascota (dibujada con formas: crece y se infla sin guardar sprites) ---------- */

static int sc(int v, int inflate)
{
    return v + v * inflate * 4 / 10000 + ((v * inflate * 4 % 10000) >= 5000);
}

static void blobc(Framebuffer *fb, int cx, int cy, int rx, int ry, Color fill, Color edge, Color hi)
{
    gfx_ellipse(fb, cx, cy, rx + 1, ry + 1, edge);
    gfx_ellipse(fb, cx, cy, rx, ry, fill);
    if (hi != fill && rx >= 3)
        gfx_ellipse(fb, cx - rx / 2, cy - ry / 2, rx / 3, ry / 3, hi);
}

static void heart(Framebuffer *fb, int x, int y)
{
    spr(fb, x, y, &S_HEART, C_RED, C_PINK, C_PINK);
}

/* Cara: ojos sueltos y una boca. joy > 0: ojos felices ^ ^ y rubor. */
static void face(Framebuffer *fb, int cx, int y, int gap, uint32_t ms, int joy)
{
    int lx = cx - gap, rx = cx + gap;
    bool blink = (ms % 4200) < 150;
    if (joy > 150) {
        for (int i = 0; i < 2; i++) {
            int ex = i ? rx : lx;
            gfx_pset(fb, ex - 1, y + 1, C_DARK);
            gfx_pset(fb, ex, y, C_DARK);
            gfx_pset(fb, ex + 1, y + 1, C_DARK);
        }
        gfx_pset(fb, lx - 2, y + 3, C_PINK);
        gfx_pset(fb, rx + 2, y + 3, C_PINK);
    } else if (blink) {
        gfx_rect(fb, lx - 1, y + 1, 3, 1, C_DARK);
        gfx_rect(fb, rx - 1, y + 1, 3, 1, C_DARK);
    } else {
        gfx_rect(fb, lx, y, 1, 2, C_DARK);
        gfx_rect(fb, rx, y, 1, 2, C_DARK);
    }
    if (joy > 80) {
        gfx_pset(fb, cx - 1, y + 3, C_DARK);
        gfx_pset(fb, cx, y + 4, C_DARK);
        gfx_pset(fb, cx + 1, y + 3, C_DARK);
    } else {
        gfx_rect(fb, cx - 1, y + 4, 3, 1, C_DARK);
    }
}

static void pet_hongo(Framebuffer *fb, int cx, int by, int stage, uint32_t ms, int inf, int joy)
{
    static const uint8_t STEM_W[6] = {0, 0, 3, 4, 6, 7}, STEM_H[6] = {0, 0, 4, 6, 8, 10};
    static const uint8_t CAP_RX[6] = {0, 0, 3, 5, 8, 9}, CAP_RY[6] = {0, 0, 2, 3, 5, 6};
    if (stage <= 1) { /* espora y punto: pequeños, pero con ojitos para que se les quiera */
        int rx = sc(stage == 0 ? 3 : 4, inf), ry = sc(stage == 0 ? 2 : 3, inf);
        blobc(fb, cx, by - ry - 1, rx, ry, C_SNOW, C_GREY, C_WHITE);
        gfx_pset(fb, cx - 1, by - ry - 1, C_DARK);
        gfx_pset(fb, cx + 1, by - ry - 1, C_DARK);
        if ((ms / 450) & 1)
            gfx_pset(fb, cx + rx + 2, by - ry - 3, C_SUN_CORE);
        return;
    }
    int sw = sc(STEM_W[stage], inf), sh = sc(STEM_H[stage], inf);
    int rx = sc(CAP_RX[stage], inf), ry = sc(CAP_RY[stage], inf);
    int bob = stage >= 4 ? (int)((ms / 900) & 1) : 0; /* respira muy despacio */
    int x0 = cx - sw / 2;
    gfx_rect(fb, x0 - 1, by - sh - bob, sw + 2, sh + bob, C_GROUND_D);
    gfx_rect(fb, x0, by - sh - bob, sw, sh + bob - 1, C_SKY_D4);
    gfx_rect(fb, x0 + sw - 1, by - sh - bob, 1, sh + bob - 1, C_LEAF);
    int cy = by - sh - ry + 2 - bob;
    blobc(fb, cx, cy, rx, ry, C_ORANGE, C_GROUND_D, C_LEAF);
    if (stage >= 3) {
        gfx_pset(fb, cx - rx / 2, cy - ry / 2 + 1, C_SUN_CORE);
        gfx_pset(fb, cx + rx / 2, cy - ry / 3, C_SUN_CORE);
        gfx_pset(fb, cx + 1, cy - ry / 2, C_SUN_CORE);
        if (stage >= 4) {
            gfx_pset(fb, cx - rx + 2, cy + 1, C_SUN_CORE);
            gfx_pset(fb, cx + rx - 3, cy + 2, C_SUN_CORE);
        }
    }
    if (stage >= 3)
        face(fb, cx, by - sh + 1 - bob, stage >= 4 ? 2 : 1, ms, joy);
    else {
        gfx_pset(fb, cx - 1, by - 2, C_DARK);
        gfx_pset(fb, cx + 1, by - 2, C_DARK);
    }
    if (stage == 5) {
        /* barba: guedejas blancas que cuelgan de la cara */
        for (int r = 0; r < 5; r++) {
            int bw = sw + 3 - r * 2;
            if (bw > 0) {
                gfx_rect(fb, cx - bw / 2, by - sh + 5 + r - bob, bw, 1, C_WHITE);
                gfx_pset(fb, cx - bw / 2, by - sh + 5 + r - bob, C_GREY);
                gfx_pset(fb, cx - bw / 2 + bw - 1, by - sh + 5 + r - bob, C_GREY_L);
            }
        }
        gfx_rect(fb, cx - 4, by - sh - 1 - bob, 3, 1, C_GREY_L); /* cejas */
        gfx_rect(fb, cx + 2, by - sh - 1 - bob, 3, 1, C_GREY_L);
        for (int k = 0; k < 3; k++) { /* esporas que suben */
            int t = (int)((ms / 90 + (uint32_t)k * 13) % 40);
            gfx_pset(fb, cx - 6 + k * 6 + WAVE[(t / 3) & 7] / 2, cy - ry - 2 - t / 3, C_GREY_L);
        }
    }
}

static void pet_semilla(Framebuffer *fb, int cx, int by, int stage, uint32_t ms, int inf, int joy, int lean)
{
    int pw = sc(13, inf), ph = sc(7, inf);
    int x0 = cx - pw / 2;
    /* maceta de terracota */
    gfx_rect(fb, x0 + 1, by - ph + 2, pw - 2, ph - 2, C_ORANGE);
    gfx_rect(fb, x0 + pw - 4, by - ph + 2, 3, ph - 2, C_GROUND);
    gfx_rect(fb, x0, by - ph, pw, 3, C_LEAF);
    gfx_rect(fb, x0, by - ph + 2, pw, 1, C_GROUND_D);
    gfx_rect(fb, x0 + 2, by - ph - 1, pw - 4, 1, C_GROUND_D); /* tierra */
    face(fb, cx, by - ph + 3, 2, ms, joy);
    int sy = by - ph - 1; /* nivel del suelo de la maceta */
    int sway = (int)((ms / 700) & 1);
    switch (stage) {
    case 0:
        gfx_ellipse(fb, cx, sy, 1, 0, C_ITEM_ALPISTE);
        gfx_pset(fb, cx, sy - 1, C_LEAF);
        break;
    case 1:
        for (int j = 0; j < 4; j++)
            gfx_pset(fb, cx + lean * j / 4, sy - j, C_GREEN);
        gfx_rect(fb, cx - 2 + lean / 2, sy - 5 + sway, 2, 1, C_GREEN_L);
        gfx_rect(fb, cx + 1 + lean / 2, sy - 4 - sway, 2, 1, C_GREEN_L);
        break;
    case 2:
        for (int j = 0; j < 8; j++)
            gfx_pset(fb, cx + lean * j / 8, sy - j, C_GREEN);
        for (int k = 0; k < 2; k++) {
            int ly = sy - 4 - k * 3;
            gfx_rect(fb, cx - 3 + lean * (4 + k * 3) / 8, ly + sway, 3, 1, C_GREEN_L);
            gfx_rect(fb, cx + 1 + lean * (4 + k * 3) / 8, ly - sway + 1, 3, 1, C_GREEN_L);
        }
        gfx_rect(fb, cx - 1 + lean, sy - 9, 2, 2, C_GREEN_L);
        break;
    case 3:
    case 4: {
        int h = stage == 3 ? 10 : 13, rx = stage == 3 ? 7 : 10, ry = stage == 3 ? 4 : 5;
        int w = stage == 3 ? 2 : 3;
        for (int j = 0; j < h; j++) {
            int bend = (j > 3 && j < 8) ? 1 : 0; /* tronco con carácter */
            gfx_rect(fb, cx + lean * j / h + bend - w / 2, sy - j, w, 1, j & 1 ? C_GROUND : C_GROUND_D);
        }
        int tx = cx + lean + 1, ty = sy - h - ry + 3;
        blobc(fb, tx, ty, rx, ry, C_GREEN, C_GREEN_D, C_GREEN_L);
        if (stage == 4) {
            blobc(fb, tx - 7 + sway, ty + 2, 4, 3, C_GREEN, C_GREEN_D, C_GREEN_L);
            gfx_pset(fb, tx - 3, ty - 2, C_PINK);
            gfx_pset(fb, tx + 4, ty, C_PINK);
            gfx_pset(fb, tx + 1, ty + 3, C_PINK);
            gfx_pset(fb, tx - 8, ty + 1, C_PINK);
        }
        break;
    }
    default:
        break;
    }
}

static void pet_piedra(Framebuffer *fb, int cx, int by, int stage, uint32_t ms, int inf, int joy)
{
    int rx = sc(9, inf), ry = sc(6, inf);
    int cy = by - ry;
    blobc(fb, cx, cy, rx, ry, C_STONE, C_GREY_D, C_STONE_L);
    gfx_rect(fb, cx - rx + 1, cy + ry - 1, 2 * rx - 1, 1, C_GREY);
    if (stage >= 1) { /* musgo */
        for (int i = 0; i < 14; i++) {
            uint32_t h = hash(77 + (uint32_t)i);
            int px = cx - rx + 1 + (int)(h % (uint32_t)(2 * rx - 1));
            int py = cy - ry + 1 + (int)((h >> 8) % 4);
            gfx_pset(fb, px, py, (h >> 16) & 1 ? C_GREEN : C_GREEN_L);
        }
    }
    if (stage >= 2) { /* liquen */
        for (int i = 0; i < 6; i++) {
            uint32_t h = hash(901 + (uint32_t)i);
            int px = cx - rx + 2 + (int)(h % (uint32_t)(2 * rx - 3));
            int py = cy - ry + 2 + (int)((h >> 8) % (uint32_t)(ry + 1));
            gfx_pset(fb, px, py, C_LEAF);
            gfx_pset(fb, px + 1, py, C_SUN);
        }
    }
    face(fb, cx, cy - 1, 3, ms, 0); /* la piedra jamás sonríe: solo parpadea (y se sonroja) */
    if (joy > 150) {
        gfx_pset(fb, cx - 5, cy + 2, C_PINK);
        gfx_pset(fb, cx + 5, cy + 2, C_PINK);
    }
    if (stage >= 3) { /* una mariquita vive encima */
        int t = (int)((ms / 700) % 8);
        int dx = t < 4 ? t : 8 - t;
        int lx = cx - 4 + dx * 2, ly = cy - ry - 1;
        gfx_rect(fb, lx, ly, 3, 2, C_RED);
        gfx_pset(fb, lx + 1, ly, C_DARK);
        gfx_pset(fb, lx + (t < 4 ? 3 : -1), ly + 1, C_DARK);
    }
}

static void pet_compost(Framebuffer *fb, int cx, int by, int worms, uint32_t ms, int inf, int joy)
{
    int bw = sc(20, inf), bh = sc(13, inf);
    int x0 = cx - bw / 2;
    gfx_rect(fb, x0, by - bh, bw, bh, C_GROUND);
    for (int px = x0 + 5; px < x0 + bw - 2; px += 5)
        gfx_rect(fb, px, by - bh + 3, 1, bh - 3, C_GROUND_D);
    gfx_rect(fb, x0 - 1, by - bh, bw + 2, 3, C_WOOD_L);
    gfx_rect(fb, x0 - 1, by - bh + 2, bw + 2, 1, C_GROUND_D);
    gfx_rect(fb, x0 + 1, by - bh - 2, bw - 2, 2, C_HOUSE); /* se ve el compost por arriba */
    gfx_pset(fb, x0 + 4, by - bh - 3, C_APPLE_ROT);
    gfx_pset(fb, x0 + bw - 6, by - bh - 3, C_LEAF);
    int n = worms > 8 ? 8 : worms;
    for (int k = 0; k < n; k++) {
        int wx = x0 + 2 + (k * (bw - 4)) / (n > 1 ? n : 1) + 1;
        int up = (int)(((ms / 260) + (uint32_t)k * 3) % 6);
        up = up > 3 ? 6 - up : up; /* asoma y se esconde */
        gfx_rect(fb, wx, by - bh - 2 - up, 1, up + 2, k & 1 ? C_MAGENTA : C_ITEM_LOMBRIZ);
        if (up > 1)
            gfx_pset(fb, wx + (k & 1 ? -1 : 1), by - bh - 2 - up, C_ITEM_LOMBRIZ); /* la cabecita se dobla */
    }
    face(fb, cx, by - bh + 5, 3, ms, joy);
    if ((ms / 500) & 1)
        gfx_pset(fb, cx + 2, by - bh - 6 - (int)((ms / 250) & 1), C_GREEN_L); /* olorcito */
}

void art_pet(Framebuffer *fb, int cx, int base_y, const Pet *p, uint32_t ms, int inflate, int joy, int sun_dx)
{
    int stage = pet_stage(p);
    switch (p->kind) {
    case PET_HONGO: pet_hongo(fb, cx, base_y, stage, ms, inflate, joy); break;
    case PET_SEMILLA: pet_semilla(fb, cx, base_y, stage, ms, inflate, joy, sun_dx); break;
    case PET_PIEDRA: pet_piedra(fb, cx, base_y, stage, ms, inflate, joy); break;
    case PET_COMPOSTERA: pet_compost(fb, cx, base_y, pet_worms(p), ms, inflate, joy); break;
    default: break;
    }
    if (joy > 250) {
        int t = (int)(ms / 120);
        for (int k = 0; k < 2; k++) {
            int ph = (t + k * 7) % 16;
            heart(fb, cx - 10 + k * 14 + WAVE[ph & 7], base_y - 26 - ph / 2);
        }
    }
}

/* ---------- comprobación de los sprites (tests) ---------- */

int art_selfcheck(void)
{
    static const Sprite *const ALL[] = {
        &S_CANOPY_A, &S_CANOPY_B, &S_YOUNG_A, &S_YOUNG_B, &S_STONE, &S_APPLE, &S_APPLE_ROT,
        &S_SPROUT_A, &S_SPROUT_B, &S_SEED, &S_BIRD_A, &S_BIRD_B, &S_FROG_A, &S_FROG_B,
        &S_TRUNK, &S_TRUNK_S, &S_MUSH_S, &S_MUSH_L, &S_BUD, &S_FLOWER_A, &S_FLOWER_B,
        &S_BEE_A, &S_BEE_B, &S_STAR, &S_HEART, &S_NUBE,
    };
    int bad = 0;
    for (unsigned i = 0; i < sizeof(ALL) / sizeof(ALL[0]); i++) {
        const Sprite *s = ALL[i];
        for (int j = 0; j < s->h; j++) {
            if ((int)strlen(s->rows[j]) != s->w)
                bad++;
            for (int k = 0; k < s->w && s->rows[j][k]; k++)
                if (!strchr(".123", s->rows[j][k]))
                    bad++;
        }
    }
    return bad;
}
