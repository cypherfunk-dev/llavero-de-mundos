#include "overlays.h"

#include <string.h>

#include "cast.h"
#include "font.h"
#include "phrases_vocab.h"

/* Texto ampliado (cada punto de la fuente se dibuja como scale x scale). */
static int text_big(Framebuffer *fb, int x, int y, const char *s, Color c, int scale)
{
    while (*s) {
        uint32_t cp = font_utf8_next(&s);
        const uint8_t *g = font_glyph(cp);
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 5; i++)
                if (g[j] & (0x10 >> i))
                    gfx_rect(fb, x + i * scale, y + j * scale, scale, scale, c);
        x += GFX_CHAR_W * scale;
    }
    return x;
}

static int big_width(const char *s, int scale)
{
    return gfx_text_width(s) * scale + (scale - 1);
}

/* Tabla para oscurecer la escena: cada color pasa a una rampa de grises azulados según su luz. */
static uint8_t dim_lut[C_COUNT];
static bool dim_ready;

static void build_dim(void)
{
    static const Color RAMP[5] = {C_DARK, C_HOUSE, C_ROOF, C_GREY_D, C_GREY};
    for (int i = 0; i < C_COUNT; i++) {
        uint32_t c = gfx_palette[i];
        uint32_t lum = (((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255)) / 10;
        dim_lut[i] = RAMP[lum * 5 / 330 > 4 ? 4 : lum * 5 / 330];
    }
    dim_ready = true;
}

void overlay_absent(Framebuffer *fb, const char *text)
{
    if (!dim_ready)
        build_dim();
    for (int y = LLV_HUD_H; y < LLV_H; y++)
        for (int x = 0; x < LLV_W; x++)
            fb->px[y * LLV_W + x] = dim_lut[fb->px[y * LLV_W + x]];
    int lines = gfx_text_wrap(NULL, 0, 0, 80, text, C_DARK);
    int h = lines * GFX_LINE_H + 10, w = 92, x = (LLV_W - w) / 2, y = 34;
    gfx_rect(fb, 56, y + h, 8, 40, C_GROUND_D); /* el poste */
    gfx_rect(fb, 56, y + h, 2, 40, C_WOOD_L);
    gfx_rect(fb, x, y, w, h, C_WOOD_L);
    gfx_frame(fb, x, y, w, h, C_GROUND_D);
    gfx_rect(fb, x + 1, y + h - 2, w - 2, 1, C_GROUND);
    gfx_pset(fb, x + 3, y + 3, C_GREY_D);
    gfx_pset(fb, x + w - 4, y + 3, C_GREY_D);
    gfx_pset(fb, x + 3, y + h - 4, C_GREY_D);
    gfx_pset(fb, x + w - 4, y + h - 4, C_GREY_D);
    gfx_text_wrap(fb, x + 6, y + 5, 80, text, C_DARK);
}

void overlay_invasion(Framebuffer *fb, int spk, const char *text, uint32_t ms)
{
    int lines = gfx_text_wrap(NULL, 0, 0, 100, text, C_DARK);
    int h = lines * GFX_LINE_H + 9, y = LLV_H - 3 - h;
    /* el visitante asoma por la derecha, con un saltito de curiosidad */
    int hop = ((ms / 350) & 1) ? 0 : 1;
    cast_draw(fb, spk, 98, y - 27 - hop, EXPR_TALK, ms, false);
    gfx_textbox(fb, 4, y, 112, h);
    gfx_text_wrap(fb, 10, y + 5, 100, text, C_DARK);
    /* la colita del bocadillo */
    gfx_rect(fb, 102, y - 1, 5, 2, C_WHITE);
    gfx_pset(fb, 103, y - 2, C_WHITE);
}

void overlay_glitch(Framebuffer *fb, const char *text, uint32_t ms, Rng *fx)
{
    static uint8_t row[LLV_W];
    for (int y = LLV_HUD_H; y < LLV_H; y++) {
        if (rng_below(fx, 100) < 18) {
            int dx = (int)rng_below(fx, 15) - 7;
            for (int x = 0; x < LLV_W; x++) {
                int sx = x + dx;
                row[x] = (sx >= 0 && sx < LLV_W) ? fb->px[y * LLV_W + sx] : fb->px[y * LLV_W + x];
            }
            memcpy(&fb->px[y * LLV_W], row, LLV_W);
        }
    }
    for (int k = 0; k < 14; k++) { /* bloques con el color cambiado */
        int bx = (int)rng_below(fx, LLV_W - 12), by = LLV_HUD_H + (int)rng_below(fx, LLV_H - LLV_HUD_H - 4);
        int bw = 4 + (int)rng_below(fx, 22), bh = 1 + (int)rng_below(fx, 3);
        for (int j = by; j < by + bh && j < LLV_H; j++)
            for (int i = bx; i < bx + bw && i < LLV_W; i++)
                fb->px[j * LLV_W + i] = (uint8_t)((fb->px[j * LLV_W + i] + 7 + (j & 3)) % C_COUNT);
    }
    for (int k = 0; k < 40; k++) /* nieve */
        gfx_pset(fb, (int)rng_below(fx, LLV_W), LLV_HUD_H + (int)rng_below(fx, LLV_H - LLV_HUD_H),
                 rng_below(fx, 2) ? C_WHITE : C_DARK);
    if ((ms / 400) % 5 != 4) {
        int lines = gfx_text_wrap(NULL, 0, 0, 90, text, C_WHITE);
        int h = lines * GFX_LINE_H + 8, y = 48;
        gfx_rect(fb, 8, y, 104, h, C_DARK);
        gfx_frame(fb, 8, y, 104, h, C_RED);
        gfx_text_wrap(fb, 14, y + 4, 90, text, C_WHITE);
    }
}

void overlay_boot(Framebuffer *fb, uint32_t t)
{
    gfx_clear(fb, C_DARK);
    /* el logo cae desde arriba y rebota una vez */
    int target = 38, y;
    if (t < 600)
        y = -12 + (int)((uint32_t)(target + 12) * t * t / (600u * 600u));
    else if (t < 800)
        y = target - (int)(8 - (int)(t - 600) * 8 / 200);
    else
        y = target;
    static const Color RAINBOW[7] = {C_RED, C_ORANGE, C_SUN, C_GREEN_L, C_CYAN, C_VIOLET, C_MAGENTA};
    const char *logo = "LLAVERO";
    int x = (LLV_W - big_width(logo, 2)) / 2;
    for (int i = 0; logo[i]; i++) {
        char one[2] = {logo[i], 0};
        text_big(fb, x + i * GFX_CHAR_W * 2 + 1, y + 1, one, C_GREY_D, 2);
        text_big(fb, x + i * GFX_CHAR_W * 2, y, one, t > 900 ? RAINBOW[i % 7] : C_WHITE, 2);
    }
    if (t > 800) {
        gfx_text_center(fb, 62, "C O L O R", C_VIOLET);
        gfx_text_center(fb, 100, "(c)1999 LLV Soft", C_GREY_D);
    }
    if (t > 1300 && t < 1450) /* destello final */
        gfx_dither(fb, 0, LLV_HUD_H, LLV_W, LLV_H - LLV_HUD_H, C_WHITE, C_DARK);
}

void overlay_coin(Framebuffer *fb, const char *text, uint32_t ms)
{
    gfx_clear(fb, C_DARK);
    gfx_dither(fb, 0, LLV_HUD_H, LLV_W, 16, C_VIOLET_D, C_DARK);
    gfx_text_center(fb, 14, "* ARCADE *", C_MAGENTA);
    /* la moneda gira */
    int w = 2 + (int)((ms / 120) % 6);
    if (w > 5)
        w = 10 - w;
    gfx_ellipse(fb, 60, 50, w, 9, C_SUN);
    gfx_ellipse(fb, 60, 50, w > 2 ? w - 2 : 1, 7, C_LEAF);
    gfx_rect(fb, 59, 46, 2, 8, C_SUN);
    if ((ms / 500) & 1)
        gfx_text_center(fb, 76, text, C_WHITE);
    gfx_text_center(fb, 92, "PULSA A", C_CYAN);
    gfx_text_center(fb, 106, "CREDITOS 00", C_GREY_D);
}
