#include "gfx.h"

#include <string.h>

#include "font.h"

const uint32_t gfx_palette[C_COUNT] = {
    [C_DARK] = 0x202028, [C_WHITE] = 0xF8F8F0, [C_GREY] = 0x7880A0,
    [C_GREY_D] = 0x505060, [C_GREY_L] = 0xC8C0B8,
    [C_SKY_N0] = 0x0C1430, [C_SKY_N1] = 0x141E40, [C_SKY_N2] = 0x1C2A58,
    [C_SKY_N3] = 0x283868, [C_SKY_N4] = 0x384A80,
    [C_SKY_D0] = 0x5090D8, [C_SKY_D1] = 0x78B0E8, [C_SKY_D2] = 0xA0D0F0,
    [C_SKY_D3] = 0xC8E4F4, [C_SKY_D4] = 0xF4E2C0,
    [C_SKY_S0] = 0x403870, [C_SKY_S1] = 0x7A4C8C, [C_SKY_S2] = 0xC86A88,
    [C_SKY_S3] = 0xF09878, [C_SKY_S4] = 0xF8C878,
    [C_SUN] = 0xF8E048, [C_SUN_CORE] = 0xF8F4C0, [C_MOON] = 0xF8F0B0,
    [C_STAR_BLUE] = 0xA8B8F8,
    [C_HILL_FAR] = 0x78A898, [C_HILL_FAR_N] = 0x283050, [C_HILL_N1] = 0x182040,
    [C_HILL_N2] = 0x0C1428,
    [C_GREEN_L] = 0x88D058, [C_GREEN] = 0x48A038, [C_GREEN_D] = 0x2E7830,
    [C_ORANGE] = 0xD88838, [C_LEAF] = 0xF0B048, [C_GROUND] = 0xA86830,
    [C_GROUND_D] = 0x885028,
    [C_SNOW] = 0xE8F0F8, [C_SNOW_D] = 0xB8C8E0, [C_PINK] = 0xF878B8,
    [C_HOUSE] = 0x2A2438, [C_ROOF] = 0x4A3448, [C_WINDOW] = 0xF8D058,
    [C_BLUE_TXT] = 0x3060A0, [C_RED] = 0xE83828, [C_CYAN] = 0x48F8E8,
    [C_MAGENTA] = 0xF848A8, [C_VIOLET] = 0x8858F8, [C_VIOLET_D] = 0x241038,
    [C_ITEM_NUBE] = 0xA8D8F8, [C_ITEM_SEMILLA] = 0x78D050,
    [C_ITEM_LOMBRIZ] = 0xF088A8, [C_ITEM_ALPISTE] = 0xD8B878,
    [C_LED_OFF] = 0x3A3446,
    [C_GRASS_A] = 0x98A840, [C_GRASS_A_D] = 0x788830, [C_APPLE_ROT] = 0x6C4830,
    [C_WOOD_L] = 0xD8A060, [C_BIRD] = 0x4868D0, [C_BIRD_L] = 0x98B8F0,
    [C_STONE] = 0x8890A0, [C_STONE_L] = 0xB8C0CC,
};

void gfx_clear(Framebuffer *fb, Color c)
{
    memset(fb->px, (int)c, sizeof(fb->px));
}

void gfx_pset(Framebuffer *fb, int x, int y, Color c)
{
    if ((unsigned)x < LLV_W && (unsigned)y < LLV_H)
        fb->px[y * LLV_W + x] = (uint8_t)c;
}

Color gfx_get(const Framebuffer *fb, int x, int y)
{
    if ((unsigned)x < LLV_W && (unsigned)y < LLV_H)
        return (Color)fb->px[y * LLV_W + x];
    return C_DARK;
}

void gfx_rect(Framebuffer *fb, int x, int y, int w, int h, Color c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > LLV_W ? LLV_W : x + w, y1 = y + h > LLV_H ? LLV_H : y + h;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++)
            fb->px[j * LLV_W + i] = (uint8_t)c;
}

void gfx_frame(Framebuffer *fb, int x, int y, int w, int h, Color c)
{
    gfx_rect(fb, x, y, w, 1, c);
    gfx_rect(fb, x, y + h - 1, w, 1, c);
    gfx_rect(fb, x, y, 1, h, c);
    gfx_rect(fb, x + w - 1, y, 1, h, c);
}

void gfx_dither(Framebuffer *fb, int x, int y, int w, int h, Color c1, Color c2)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            gfx_pset(fb, i, j, ((i + j) & 1) ? c2 : c1);
}

void gfx_ellipse(Framebuffer *fb, int cx, int cy, int rx, int ry, Color c)
{
    /* Prueba entera de pertenencia: (dx/rx)^2 + (dy/ry)^2 <= 1, con medio píxel de holgura. */
    long a2 = (long)(2 * rx + 1) * (2 * rx + 1), b2 = (long)(2 * ry + 1) * (2 * ry + 1);
    for (int dy = -ry; dy <= ry; dy++)
        for (int dx = -rx; dx <= rx; dx++)
            if (4L * dx * dx * b2 + 4L * dy * dy * a2 <= a2 * b2)
                gfx_pset(fb, cx + dx, cy + dy, c);
}

void gfx_bands(Framebuffer *fb, int y0, int y1, const Color *bands, int n)
{
    int h = y1 - y0;
    for (int k = 0; k < n; k++) {
        int a = y0 + h * k / n, b = y0 + h * (k + 1) / n;
        gfx_rect(fb, 0, a, LLV_W, b - a, bands[k]);
        if (k + 1 < n)
            gfx_dither(fb, 0, b - 1, LLV_W, 2, bands[k], bands[k + 1]);
    }
}

void gfx_sprite(Framebuffer *fb, int x, int y, const Sprite *s, const Color pal[3], int scale)
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            char ch = s->rows[j][i];
            if (ch >= '1' && ch <= '3')
                gfx_rect(fb, x + i * scale, y + j * scale, scale, scale, pal[ch - '1']);
        }
}

void gfx_sprite_flip(Framebuffer *fb, int x, int y, const Sprite *s, const Color pal[3])
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++) {
            char ch = s->rows[j][s->w - 1 - i];
            if (ch >= '1' && ch <= '3')
                gfx_pset(fb, x + i, y + j, pal[ch - '1']);
        }
}

void gfx_sprite_stretch(Framebuffer *fb, int x, int y, const Sprite *s, const Color pal[3], int dw, int dh)
{
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            char ch = s->rows[j * s->h / dh][i * s->w / dw];
            if (ch >= '1' && ch <= '3')
                gfx_pset(fb, x + i, y + j, pal[ch - '1']);
        }
}

void gfx_textbox(Framebuffer *fb, int x, int y, int w, int h)
{
    gfx_rect(fb, x, y, w, h, C_DARK);
    gfx_rect(fb, x + 1, y + 1, w - 2, h - 2, C_WHITE);
    gfx_frame(fb, x + 2, y + 2, w - 4, h - 4, C_GREY);
}

/* ---------- texto ---------- */

static void glyph(Framebuffer *fb, int x, int y, const uint8_t *rows, Color c)
{
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 5; i++)
            if (rows[j] & (0x10 >> i))
                gfx_pset(fb, x + i, y + j, c);
}

int gfx_text(Framebuffer *fb, int x, int y, const char *s, Color c)
{
    while (*s) {
        uint32_t cp = font_utf8_next(&s);
        if (fb)
            glyph(fb, x, y, font_glyph(cp), c);
        x += GFX_CHAR_W;
    }
    return x;
}

int gfx_text_width(const char *s)
{
    int n = 0;
    while (*s) {
        font_utf8_next(&s);
        n++;
    }
    return n ? n * GFX_CHAR_W - 1 : 0;
}

void gfx_text_center(Framebuffer *fb, int y, const char *s, Color c)
{
    gfx_text(fb, (LLV_W - gfx_text_width(s)) / 2, y, s, c);
}

int gfx_text_wrap(Framebuffer *fb, int x, int y, int w, const char *s, Color c)
{
    int maxc = (w + 1) / GFX_CHAR_W;
    if (maxc < 1)
        maxc = 1;
    char line[160];
    size_t llen = 0;
    int lchars = 0, lines = 0;

#define FLUSH()                                                        \
    do {                                                               \
        line[llen] = '\0';                                             \
        if (fb)                                                        \
            gfx_text(fb, x, y + lines * GFX_LINE_H, line, c);          \
        lines++;                                                       \
        llen = 0;                                                      \
        lchars = 0;                                                    \
    } while (0)

    const char *p = s;
    while (*p) {
        if (*p == '\n') {
            FLUSH();
            p++;
            continue;
        }
        if (*p == ' ') {
            p++;
            continue;
        }
        const char *word = p;
        int wchars = 0;
        while (*p && *p != ' ' && *p != '\n') {
            font_utf8_next(&p);
            wchars++;
        }
        size_t wbytes = (size_t)(p - word);
        if (lchars && lchars + 1 + wchars > maxc)
            FLUSH();
        if (llen + wbytes + 2 >= sizeof(line))
            break; /* línea absurdamente larga: se corta */
        if (lchars) {
            line[llen++] = ' ';
            lchars++;
        }
        memcpy(line + llen, word, wbytes);
        llen += wbytes;
        lchars += wchars;
    }
    if (lchars)
        FLUSH();
#undef FLUSH
    return lines;
}

/* ---------- dígitos grandes 3x5 ---------- */

static const uint8_t DIGITS[11][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    {0, 2, 0, 2, 0}, /* ':' (1 columna, la del medio) */
};

int gfx_digits_width(const char *s, int scale)
{
    int w = 0;
    for (; *s; s++)
        w += (*s == ':' ? 2 : 4) * scale;
    return w ? w - scale : 0;
}

int gfx_digits(Framebuffer *fb, int x, int y, const char *s, Color c, int scale)
{
    for (; *s; s++) {
        if (*s == ':') {
            for (int j = 0; j < 5; j++)
                if (DIGITS[10][j])
                    gfx_rect(fb, x, y + j * scale, scale, scale, c);
            x += 2 * scale;
            continue;
        }
        if (*s < '0' || *s > '9') {
            x += 4 * scale;
            continue;
        }
        const uint8_t *g = DIGITS[*s - '0'];
        for (int j = 0; j < 5; j++)
            for (int i = 0; i < 3; i++)
                if (g[j] & (4 >> i))
                    gfx_rect(fb, x + i * scale, y + j * scale, scale, scale, c);
        x += 4 * scale;
    }
    return x;
}
