#include "cast.h"

#include <string.h>

#include "phrases_vocab.h"

/* Cada personaje son tres capas de 16 px de ancho: cabeza (12 filas), torso (8) y piernas o
 * falda (4, con dos frames). '.' transparente, '1' contorno, '2' color principal, '3' detalle. */

/* ---------- cabezas ---------- */

static const char *const HEAD_ABUELA[12] = {
    "......1111......", ".....122221.....", "......1221......", "....11222211....",
    "...1222222221...", "..122223322221..", "..122333333221..", "..123333333321..",
    "..123333333321..", "...1333333331...", "....13333331....", ".....111111.....",
};
static const char *const HEAD_ROBOT[12] = {
    "......1331......", ".......11.......", "..111111111111..", "..122222222221..",
    ".12222222222221.", ".12133333333121.", ".12133333333121.", ".12133333333121.",
    ".12133333333121.", ".12133333333121.", ".12222222222221.", "..111111111111..",
};
static const char *const HEAD_GOTICA[12] = {
    ".....111111.....", "....12222221....", "...1222222221...", "..122222222221..",
    "..122222222221..", "..122233332221..", "..122333333221..", "..123333333321..",
    "..123333333321..", "..122333333221..", "..122133331221..", "..122211112221..",
};
static const char *const HEAD_GAMER[12] = {
    "....11111111....", "...1222222221...", "..122222222221..", "..122222222221..",
    ".11111111111111.", "..133333333331..", ".12333333333321.", ".12333333333321.",
    ".12333333333321.", "..133333333331..", "...1333333331...", "....11111111....",
};
static const char *const HEAD_BOOMER[12] = {
    ".....111111.....", "....13333331....", "...1333333331...", "..123333333321..",
    "..123333333321..", "..123333333321..", "..123333333321..", "..123333333321..",
    "..123333333321..", "...1333333331...", "....13333331....", ".....111111.....",
};

/* ---------- torsos ---------- */

static const char *const TORSO_ROUND[8] = {
    "....11222211....", "...1222222221...", "..122222222221..", "..122222222221..",
    "..132222222231..", "..132222222231..", "...1222222221...", "...1222222221...",
};
static const char *const TORSO_BOXY_A[8] = {
    "...1111111111...", "..122222222221..", "..122222222221..", "..122233332221..",
    "..132233332231..", "..132222222231..", "..122222222221..", "..111111111111..",
};
static const char *const TORSO_BOXY_B[8] = {
    "...1111111111...", "..122222222221..", "..122222222221..", "..122323232221..",
    "..132232322231..", "..132222222231..", "..122222222221..", "..111111111111..",
};

/* ---------- piernas y faldas (frames A y B) ---------- */

static const char *const SKIRT_A[4] = {
    ".12222222222221.", ".12222222222221.", "..111222222111..", "....33....33....",
};
static const char *const SKIRT_B[4] = {
    ".12222222222221.", "1222222222222221", ".11122222222111.",
    ".....33..33.....",
};
static const char *const LEGS_A[4] = {
    "...1222112221...", "...1221..1221...", "...1221..1221...", "..13311..11331..",
};
static const char *const LEGS_B[4] = {
    "...1222112221...", "..12221..1221...", "..12221..1221...", ".133311..11331..",
};
static const char *const ROBOT_LEGS_A[4] = {
    "...121....121...", "...121....121...", "..1221....1221..", "..1111....1111..",
};
static const char *const ROBOT_LEGS_B[4] = {
    "...121....121...", "..121......121..", "..121.....1221..", ".1221.....1111..",
};

/* ---------- el gato ---------- */

static const char *const CAT[16] = {
    ".11..........11.", ".121........121.", ".12211111111221.", ".12222222222221.",
    ".12222222222221.", ".12222222222221.", ".12223333332221.", "..122233332221..",
    "..111222222111..", "..122222222221..", ".12222222222221.", ".12222222222221.",
    ".12222222222221.", ".12222222222221.", "..111222222111..", "..1111....1111..",
};

typedef struct {
    Sprite head, torso_a, torso_b, low_a, low_b;
    Color head_pal[3], torso_pal[3], low_pal[3];
} Look;

#define SPR(rows, h) {16, (h), (rows)}

static const Look LOOK_ABUELA = {
    SPR(HEAD_ABUELA, 12), SPR(TORSO_ROUND, 8), SPR(TORSO_ROUND, 8), SPR(SKIRT_A, 4), SPR(SKIRT_B, 4),
    {C_GREY_D, C_HAIR_GREY, C_SKIN}, {C_GROUND_D, C_GREEN, C_SKIN}, {C_GROUND_D, C_GREEN, C_SKIN_D},
};
static const Look LOOK_ROBOT = {
    SPR(HEAD_ROBOT, 12), SPR(TORSO_BOXY_A, 8), SPR(TORSO_BOXY_B, 8), SPR(ROBOT_LEGS_A, 4), SPR(ROBOT_LEGS_B, 4),
    {C_GREY_D, C_METAL, C_DARK}, {C_GREY_D, C_METAL, C_SUN}, {C_GREY_D, C_METAL_D, C_GREY_D},
};
static const Look LOOK_GOTICA = {
    SPR(HEAD_GOTICA, 12), SPR(TORSO_ROUND, 8), SPR(TORSO_ROUND, 8), SPR(SKIRT_A, 4), SPR(SKIRT_B, 4),
    {C_DARK, C_VIOLET_D, C_SNOW}, {C_DARK, C_ROOF, C_SNOW}, {C_DARK, C_ROOF, C_GREY_L},
};
static const Look LOOK_GAMER = {
    SPR(HEAD_GAMER, 12), SPR(TORSO_ROUND, 8), SPR(TORSO_ROUND, 8), SPR(LEGS_A, 4), SPR(LEGS_B, 4),
    {C_DARK, C_RED, C_SKIN}, {C_DARK, C_BLUE_TXT, C_SKIN}, {C_DARK, C_SKY_N3, C_WHITE},
};
static const Look LOOK_BOOMER = {
    SPR(HEAD_BOOMER, 12), SPR(TORSO_ROUND, 8), SPR(TORSO_ROUND, 8), SPR(LEGS_A, 4), SPR(LEGS_B, 4),
    {C_GREY_D, C_GREY_L, C_SKIN}, {C_DARK, C_ORANGE, C_SKIN}, {C_DARK, C_LEAF, C_GROUND_D},
};

static const Look *look_of(int spk)
{
    switch (spk) {
    case SPK_ABUELA: return &LOOK_ABUELA;
    case SPK_ROBOT: return &LOOK_ROBOT;
    case SPK_GOTICA: return &LOOK_GOTICA;
    case SPK_GAMER: return &LOOK_GAMER;
    case SPK_BOOMER: return &LOOK_BOOMER;
    default: return NULL;
    }
}

static const Sprite CAT_S = SPR(CAT, 16);

int cast_selfcheck(void)
{
    int bad = 0;
    static const Look *const ALL[] = {&LOOK_ABUELA, &LOOK_ROBOT, &LOOK_GOTICA, &LOOK_GAMER, &LOOK_BOOMER};
    for (unsigned i = 0; i < sizeof(ALL) / sizeof(ALL[0]); i++) {
        const Sprite *ss[] = {&ALL[i]->head, &ALL[i]->torso_a, &ALL[i]->torso_b, &ALL[i]->low_a, &ALL[i]->low_b};
        for (unsigned k = 0; k < 5; k++)
            for (int r = 0; r < ss[k]->h; r++)
                bad += (int)strlen(ss[k]->rows[r]) != ss[k]->w;
    }
    for (int r = 0; r < CAT_S.h; r++)
        bad += (int)strlen(CAT_S.rows[r]) != CAT_S.w;
    return bad;
}

/* ---------- caras ---------- */

static void px2(Framebuffer *fb, int x, int y, Color c)
{
    gfx_pset(fb, x, y, c);
    gfx_pset(fb, x + 1, y, c);
}

/* Cara de persona sobre la cabeza (x, y = esquina de los 16x12): ojos en la fila 7, boca en la 9. */
static void human_face(Framebuffer *fb, int spk, int x, int y, Expr e, uint32_t ms)
{
    bool blink = (ms / 130) % 28 == 0;
    Color eye = spk == SPK_GOTICA ? C_VIOLET_D : C_DARK;
    int ex[2] = {x + 6, x + 9}, ey = y + 7;
    if (spk == SPK_GAMER) {
        ex[0] = x + 5;
        ex[1] = x + 10;
    }
    for (int i = 0; i < 2; i++) {
        switch (e) {
        case EXPR_HAPPY: /* ojos sonrientes: arquitos */
            gfx_pset(fb, ex[i], ey, eye);
            gfx_pset(fb, ex[i] - 1, ey + 1, eye);
            gfx_pset(fb, ex[i] + 1, ey + 1, eye);
            break;
        case EXPR_SLEEPY:
            px2(fb, ex[i] - 1, ey + 1, eye);
            break;
        case EXPR_SURPRISE:
            gfx_rect(fb, ex[i], ey - 1, 2, 3, eye);
            gfx_pset(fb, ex[i], ey - 1, C_WHITE);
            break;
        case EXPR_SMUG:
            if (i == 0)
                px2(fb, ex[i] - 1, ey, eye);
            else
                gfx_rect(fb, ex[i], ey, 1, 2, eye);
            break;
        default:
            if (blink)
                px2(fb, ex[i] - 1, ey + 1, eye);
            else
                gfx_rect(fb, ex[i], ey, 1, 2, eye);
            break;
        }
    }
    if (e == EXPR_HAPPY || e == EXPR_SMUG) { /* mejillas */
        gfx_pset(fb, x + 4, y + 9, C_PINK);
        gfx_pset(fb, x + 11, y + 9, C_PINK);
    }
    int mx = x + 7, my = y + 9;
    Color mouth = spk == SPK_GOTICA ? C_MAGENTA : C_ROOF;
    switch (e) {
    case EXPR_TALK:
        if ((ms / 110) & 1)
            gfx_rect(fb, mx, my, 2, 2, C_DARK);
        else
            px2(fb, mx, my, mouth);
        break;
    case EXPR_HAPPY:
        gfx_pset(fb, mx - 1, my, mouth);
        px2(fb, mx, my + 1, mouth);
        gfx_pset(fb, mx + 2, my, mouth);
        break;
    case EXPR_SURPRISE:
        gfx_rect(fb, mx, my, 2, 2, C_DARK);
        break;
    case EXPR_SLEEPY:
        px2(fb, mx, my + 1, mouth);
        break;
    case EXPR_SMUG:
        px2(fb, mx, my + 1, mouth);
        gfx_pset(fb, mx + 2, my, mouth);
        break;
    default:
        px2(fb, mx, my + 1, mouth);
        break;
    }
    /* detalles de cada uno */
    if (spk == SPK_ABUELA) { /* gafas redondas y moño con horquilla */
        gfx_frame(fb, ex[0] - 1, ey - 1, 3, 4, C_GROUND_D);
        gfx_frame(fb, ex[1] - 1, ey - 1, 3, 4, C_GROUND_D);
        gfx_pset(fb, ex[0] + 2, ey, C_GROUND_D);
        gfx_pset(fb, ex[0] + 2, ey - 1 + 0, C_GROUND_D);
        gfx_pset(fb, x + 7, y + 1, C_RED);
    } else if (spk == SPK_GOTICA) { /* flequillo con mechón violeta y eyeliner */
        gfx_pset(fb, x + 6, y + 4, C_VIOLET);
        gfx_pset(fb, x + 7, y + 3, C_VIOLET);
        gfx_pset(fb, ex[0] - 1, ey, C_VIOLET);
        gfx_pset(fb, ex[1] + 1, ey, C_VIOLET);
    } else if (spk == SPK_BOOMER) { /* gafas cuadradas y bigote */
        gfx_frame(fb, ex[0] - 1, ey - 1, 3, 4, C_DARK);
        gfx_frame(fb, ex[1] - 1, ey - 1, 3, 4, C_DARK);
        gfx_pset(fb, ex[0] + 2, ey, C_DARK);
        gfx_pset(fb, ex[0] + 2, ey + 1, C_DARK);
        gfx_rect(fb, x + 5, y + 8, 6, 1, C_GREY_L);
        gfx_pset(fb, x + 4, y + 9, C_GREY_L);
        gfx_pset(fb, x + 11, y + 9, C_GREY_L);
    } else if (spk == SPK_GAMER) { /* la visera de la gorra y un brillo en el pelo */
        gfx_rect(fb, x + 3, y + 4, 10, 1, C_ORANGE);
        gfx_pset(fb, x + 5, y + 2, C_PINK);
    }
}

/* Pantalla de Unidad-7: ojos cian sobre fondo oscuro. */
static void robot_face(Framebuffer *fb, int x, int y, Expr e, uint32_t ms)
{
    Color eye = C_CYAN;
    int ex[2] = {x + 5, x + 9};
    bool blink = (ms / 130) % 24 == 0;
    for (int i = 0; i < 2; i++) {
        switch (e) {
        case EXPR_HAPPY:
            gfx_pset(fb, ex[i] + 1, y + 6, eye);
            gfx_pset(fb, ex[i], y + 7, eye);
            gfx_pset(fb, ex[i] + 2, y + 7, eye);
            break;
        case EXPR_SLEEPY:
            gfx_rect(fb, ex[i], y + 7, 3, 1, C_SKY_D0);
            break;
        case EXPR_SURPRISE:
            gfx_rect(fb, ex[i], y + 5, 3, 3, eye);
            gfx_pset(fb, ex[i] + 1, y + 6, C_DARK);
            break;
        case EXPR_SMUG:
            if (i == 0)
                gfx_rect(fb, ex[i], y + 7, 3, 1, eye);
            else
                gfx_rect(fb, ex[i], y + 6, 3, 2, eye);
            break;
        default:
            if (blink)
                gfx_rect(fb, ex[i], y + 7, 3, 1, eye);
            else
                gfx_rect(fb, ex[i], y + 6, 2, 2, eye);
            break;
        }
    }
    int my = y + 9;
    switch (e) {
    case EXPR_TALK: {
        int h = ((ms / 90) % 3);
        gfx_rect(fb, x + 6, my - h / 2, 4, 1 + h / 2 + (h > 0), C_SKY_D0);
        break;
    }
    case EXPR_HAPPY:
        gfx_pset(fb, x + 5, my - 1, C_SKY_D0);
        gfx_rect(fb, x + 6, my, 4, 1, C_SKY_D0);
        gfx_pset(fb, x + 10, my - 1, C_SKY_D0);
        break;
    case EXPR_SURPRISE:
        gfx_rect(fb, x + 7, my - 1, 2, 2, C_SKY_D0);
        break;
    default:
        gfx_rect(fb, x + 6, my, 4, 1, C_SKY_D0);
        break;
    }
    /* la luz de la antena parpadea */
    if ((ms / 400) & 1)
        gfx_rect(fb, x + 7, y, 2, 1, C_SUN);
}

/* ---------- el gato ---------- */

static void cat_draw(Framebuffer *fb, int x, int y, Expr e, uint32_t ms)
{
    /* Superposición: de vez en cuando se vuelve fantasma (tramado) y los ojos cambian. */
    bool ghost = (ms / 1400) % 3 == 2;
    static const Color PAL[3] = {C_GREY_D, C_HOUSE, C_GREY};
    if (!ghost) {
        gfx_sprite(fb, x, y, &CAT_S, PAL, 1);
    } else {
        for (int j = 0; j < CAT_S.h; j++)
            for (int i = 0; i < CAT_S.w; i++) {
                char c = CAT_S.rows[j][i];
                if (c != '.' && ((i + j) & 1))
                    gfx_pset(fb, x + i, y + j, PAL[c - '1']);
            }
    }
    /* ojos: amarillos y despiertos, o cruces si toca "estado cuántico" */
    if (ghost) {
        gfx_pset(fb, x + 4, y + 4, C_SUN);
        gfx_pset(fb, x + 6, y + 4, C_SUN);
        gfx_pset(fb, x + 5, y + 5, C_SUN);
        gfx_pset(fb, x + 9, y + 4, C_SUN);
        gfx_pset(fb, x + 11, y + 4, C_SUN);
        gfx_pset(fb, x + 10, y + 5, C_SUN);
    } else if (e == EXPR_HAPPY || e == EXPR_SLEEPY) {
        px2(fb, x + 4, y + 5, C_SUN);
        px2(fb, x + 10, y + 5, C_SUN);
    } else {
        gfx_rect(fb, x + 4, y + 4, 2, 2, C_SUN);
        gfx_rect(fb, x + 10, y + 4, 2, 2, C_SUN);
        gfx_pset(fb, x + 5, y + 4, C_DARK);
        gfx_pset(fb, x + 11, y + 4, C_DARK);
    }
    gfx_pset(fb, x + 7, y + 6, C_PINK);
    gfx_pset(fb, x + 8, y + 6, C_PINK);
    /* bigotes y la cola que se mece */
    gfx_rect(fb, x - 2, y + 6, 3, 1, C_GREY_L);
    gfx_rect(fb, x + 15, y + 6, 3, 1, C_GREY_L);
    int sway = (int)((ms / 350) % 2);
    for (int k = 0; k < 6; k++)
        gfx_pset(fb, x + 14 + (k >= 4 ? sway + 1 : 0), y + 14 - k, C_HOUSE);
}

/* ---------- el pijama ---------- */

static void nightcap(Framebuffer *fb, int x, int y)
{
    gfx_rect(fb, x + 3, y + 1, 10, 2, C_SKY_D1);
    gfx_rect(fb, x + 5, y - 1, 7, 2, C_SKY_D1);
    gfx_rect(fb, x + 8, y - 3, 5, 2, C_SKY_D1);
    gfx_rect(fb, x + 3, y + 2, 10, 1, C_WHITE);
    gfx_ellipse(fb, x + 13, y - 3, 1, 1, C_WHITE);
}

/* ---------- API ---------- */

static void draw_layer(Framebuffer *fb, int x, int y, const Sprite *s, const Color pal[3])
{
    gfx_sprite(fb, x, y, s, pal, 1);
}

void cast_draw_head(Framebuffer *fb, int spk, int x, int y, Expr e, uint32_t ms)
{
    const Look *l = look_of(spk);
    if (!l) {
        if (spk == SPK_GATO)
            cat_draw(fb, x, y, e, ms);
        return;
    }
    draw_layer(fb, x, y, &l->head, l->head_pal);
    if (spk == SPK_ROBOT)
        robot_face(fb, x, y, e, ms);
    else
        human_face(fb, spk, x, y, e, ms);
}

void cast_draw(Framebuffer *fb, int spk, int x, int y, Expr e, uint32_t ms, bool pajama)
{
    const Look *l = look_of(spk);
    if (!l) {
        if (spk == SPK_GATO)
            cat_draw(fb, x, y + 2, e, ms);
        return;
    }
    bool b = ((ms / 450) & 1) != 0; /* frame A o B */
    Color tp[3] = {l->torso_pal[0], l->torso_pal[1], l->torso_pal[2]};
    Color lp[3] = {l->low_pal[0], l->low_pal[1], l->low_pal[2]};
    if (pajama && spk != SPK_ROBOT) { /* pijama de rayas azules */
        tp[1] = C_SKY_D1;
        lp[1] = C_SKY_D1;
    }
    draw_layer(fb, x, y + 12, b ? &l->torso_b : &l->torso_a, tp);
    draw_layer(fb, x, y + 20, b ? &l->low_b : &l->low_a, lp);
    /* detalles de la ropa */
    switch (spk) {
    case SPK_ABUELA:
        if (!pajama) {
            gfx_rect(fb, x + 5, y + 16, 6, 6, C_CREAM);
            gfx_rect(fb, x + 5, y + 16, 6, 1, C_GROUND_D);
            gfx_pset(fb, x + 7, y + 18, C_RED);
        }
        break;
    case SPK_GOTICA:
        gfx_rect(fb, x + 6, y + 12, 4, 1, C_VIOLET); /* gargantilla */
        gfx_rect(fb, x + 2, y + 21, 12, 1, C_VIOLET);
        break;
    case SPK_GAMER:
        gfx_rect(fb, x + 7, y + 17, 2, 3, C_GREY_L); /* cordones de la sudadera */
        if (b) { /* los pulgares en el mando */
            gfx_rect(fb, x + 5, y + 18, 6, 2, C_GREY_D);
            gfx_pset(fb, x + 6, y + 18, C_RED);
            gfx_pset(fb, x + 9, y + 18, C_CYAN);
        }
        break;
    case SPK_BOOMER:
        gfx_rect(fb, x + 3, y + 19, 10, 1, C_GROUND_D); /* cinturón */
        gfx_rect(fb, x + 7, y + 19, 2, 1, C_SUN);
        gfx_rect(fb, x + 7, y + 12, 2, 2, C_WHITE); /* cuello de polo */
        break;
    case SPK_ROBOT:
        if (pajama)
            gfx_rect(fb, x + 4, y + 16, 8, 1, C_SKY_D1);
        break;
    default:
        break;
    }
    draw_layer(fb, x, y, &l->head, l->head_pal);
    if (spk == SPK_ROBOT)
        robot_face(fb, x, y, e, ms);
    else
        human_face(fb, spk, x, y, e, ms);
    if (pajama)
        nightcap(fb, x, y);
}
