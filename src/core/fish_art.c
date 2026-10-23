/* Sprites de los peces y las basuras simpáticas (spec §12.2, §13): 16x12 píxeles de 4 colores
 * ('.' transparente, '1' contorno, '2' cuerpo, '3' vientre o brillo), todos mirando a la izquierda.
 * Cada especie tiene su paleta. La estantería del Cobertizo usa una versión reducida. */
#include <string.h>

#include "fish.h"

static const char *const S_LUNES_ROWS[] = {
    "................",
    "......11........",
    ".....1221......1",
    "...1122221...111",
    "..13333322111221",
    ".11222222222221.",
    ".12222222222221.",
    "..13333333111221",
    "...1111111...111",
    "...............1",
    "................",
    "................",
};
static const Sprite S_LUNES = {16, 12, S_LUNES_ROWS};

static const char *const S_CARPA_ROWS[] = {
    "................",
    "................",
    "......11.......1",
    "....112211...111",
    "..11333322111221",
    ".11222222222221.",
    ".12222222222221.",
    "1111333333111221",
    "1...111111...111",
    "...............1",
    "................",
    "................",
};
static const Sprite S_CARPA = {16, 12, S_CARPA_ROWS};

static const char *const S_TRUCHA_ROWS[] = {
    "................",
    "................",
    ".......1.......1",
    ".....1121....111",
    "..11113121111221",
    ".11222222122221.",
    ".13333131332221.",
    "..11133311111221",
    ".....111.....111",
    "...............1",
    "................",
    "................",
};
static const Sprite S_TRUCHA = {16, 12, S_TRUCHA_ROWS};

static const char *const S_BAGRE_ROWS[] = {
    "................",
    "................",
    "................",
    "....111111....11",
    "..11333322111121",
    ".121222222222221",
    ".12222222222221.",
    "1113333333112221",
    "1..1111111..1111",
    "11.............1",
    "................",
    "................",
};
static const Sprite S_BAGRE = {16, 12, S_BAGRE_ROWS};

static const char *const S_PARAGUAS_ROWS[] = {
    ".....111........",
    "...1122211......",
    "..111121111.....",
    "......1.........",
    "....11211....111",
    "..11333321111221",
    ".11222222222221.",
    ".12222222222221.",
    "..11333331111221",
    "....11111....111",
    "................",
    "................",
};
static const Sprite S_PARAGUAS = {16, 12, S_PARAGUAS_ROWS};

static const char *const S_KOI_ROWS[] = {
    ".......1........",
    "......121.......",
    ".....12221....11",
    "...1122221..1121",
    "..13333322112211",
    ".1122233223221..",
    ".1222222232221..",
    "..13333333112211",
    "...1111111..1121",
    "..............11",
    "................",
    "................",
};
static const Sprite S_KOI = {16, 12, S_KOI_ROWS};

static const char *const S_BOTA_ROWS[] = {
    "................",
    "................",
    "....1111111.....",
    "....1222221.....",
    "....1232221.....",
    "....1222221.....",
    "....1222221.....",
    "....1222221111..",
    "....12222222221.",
    "...122222222221.",
    "...133333333311.",
    "...11111111111..",
};
static const Sprite S_BOTA = {16, 12, S_BOTA_ROWS};

static const char *const S_LATA_ROWS[] = {
    "................",
    "................",
    ".....111111.....",
    "....13333321....",
    "....11111111....",
    "....12333221....",
    "....12333221....",
    "....12333221....",
    "....11111111....",
    "....12222221....",
    ".....111111.....",
    "................",
};
static const Sprite S_LATA = {16, 12, S_LATA_ROWS};

static const char *const S_ALGA_ROWS[] = {
    "................",
    "..1....1....1...",
    ".121..121..121..",
    ".121..121..121..",
    "..121.121.121...",
    "..121.121.121...",
    "...121121.121...",
    "...12121.1221...",
    "....1221.121....",
    "....1221.121....",
    "...12221.1221...",
    "...1111..1111...",
};
static const Sprite S_ALGA = {16, 12, S_ALGA_ROWS};

static const char *const S_CALCETIN_ROWS[] = {
    "................",
    "....1111111.....",
    "....1333331.....",
    "....1111111.....",
    "....1222221.....",
    "....1222221.....",
    "....1222221.....",
    "....122222111...",
    "....1222222211..",
    "...12222222221..",
    "...13333333221..",
    "...1111111111...",
};
static const Sprite S_CALCETIN = {16, 12, S_CALCETIN_ROWS};

static const Sprite *const SPRITES[FISH_TOTAL] = {
    &S_LUNES, &S_CARPA, &S_TRUCHA, &S_BAGRE, &S_PARAGUAS, &S_KOI,
    &S_BOTA, &S_LATA, &S_ALGA, &S_CALCETIN,
};

/* Colores: contorno, cuerpo, vientre. */
static const Color PAL[FISH_TOTAL][3] = {
    {C_DARK, C_SKY_D0, C_SKY_D3},        /* Pez Lunes */
    {C_ROOF, C_ORANGE, C_CREAM},         /* Carpa Resignada */
    {C_SKY_N2, C_METAL, C_PINK},         /* Trucha de Escarcha */
    {C_DARK, C_GREY, C_GREY_L},          /* Bagre Insomne */
    {C_DARK, C_CYAN, C_MOON},            /* Pez Paraguas */
    {C_ROOF, C_SUN_CORE, C_PINK},        /* Koi Lunar */
    {C_ROOF, C_GROUND, C_WOOD_L},        /* Bota Vieja */
    {C_GREY_D, C_STONE, C_GREY_L},       /* Lata Oxidada */
    {C_GREEN_D, C_GREEN, C_GREEN_L},     /* Alga Enredada */
    {C_ROOF, C_CREAM, C_MAGENTA},        /* Calcetín Solitario */
};

void art_fish(Framebuffer *fb, int x, int y, int idx, bool known, bool flip)
{
    if (idx < 0 || idx >= FISH_TOTAL)
        return;
    Color sil[3] = {C_GREY_D, C_GREY_D, C_GREY_D};
    const Color *pal = known ? PAL[idx] : sil;
    if (flip)
        gfx_sprite_flip(fb, x, y, SPRITES[idx], pal);
    else
        gfx_sprite(fb, x, y, SPRITES[idx], pal, 1);
}

void art_fish_mini(Framebuffer *fb, int x, int y, int idx, bool known)
{
    if (idx < 0 || idx >= FISH_TOTAL)
        return;
    Color sil[3] = {C_GREY_D, C_GREY_D, C_GREY_D};
    gfx_sprite_stretch(fb, x, y, SPRITES[idx], known ? PAL[idx] : sil, 12, 9);
}

void art_fish_mini_tint(Framebuffer *fb, int x, int y, int idx, Color c)
{
    if (idx < 0 || idx >= FISH_TOTAL)
        return;
    Color pal[3] = {c, c, c};
    gfx_sprite_stretch(fb, x, y, SPRITES[idx], pal, 12, 9);
}

void art_fish_big(Framebuffer *fb, int x, int y, int idx, bool known)
{
    if (idx < 0 || idx >= FISH_TOTAL)
        return;
    Color sil[3] = {C_GREY_D, C_GREY_D, C_GREY_D};
    gfx_sprite(fb, x, y, SPRITES[idx], known ? PAL[idx] : sil, 2);
}

void art_fish_tint(Framebuffer *fb, int x, int y, int idx, Color c, bool flip)
{
    if (idx < 0 || idx >= FISH_TOTAL)
        return;
    Color pal[3] = {c, c, c};
    if (flip)
        gfx_sprite_flip(fb, x, y, SPRITES[idx], pal);
    else
        gfx_sprite(fb, x, y, SPRITES[idx], pal, 1);
}

int art_fish_selfcheck(void)
{
    int bad = 0;
    for (int i = 0; i < FISH_TOTAL; i++) {
        const Sprite *s = SPRITES[i];
        if (s->w != 16 || s->h != 12)
            bad++;
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
