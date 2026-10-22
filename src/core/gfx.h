/* Gráficos estilo GBC (spec §12). El framebuffer guarda índices de la paleta maestra;
 * la plataforma los convierte a color real al presentar. */
#ifndef LLV_GFX_H
#define LLV_GFX_H

#include <stdint.h>

#include "config.h"

/* Paleta maestra: colores "lavados" de GBC. */
typedef enum {
    C_DARK, C_WHITE, C_GREY, C_GREY_D, C_GREY_L,
    C_SKY_N0, C_SKY_N1, C_SKY_N2, C_SKY_N3, C_SKY_N4,
    C_SKY_D0, C_SKY_D1, C_SKY_D2, C_SKY_D3, C_SKY_D4,
    C_SKY_S0, C_SKY_S1, C_SKY_S2, C_SKY_S3, C_SKY_S4,
    C_SUN, C_SUN_CORE, C_MOON, C_STAR_BLUE,
    C_HILL_FAR, C_HILL_FAR_N, C_HILL_N1, C_HILL_N2,
    C_GREEN_L, C_GREEN, C_GREEN_D, C_ORANGE, C_LEAF, C_GROUND, C_GROUND_D,
    C_SNOW, C_SNOW_D, C_PINK, C_HOUSE, C_ROOF, C_WINDOW,
    C_BLUE_TXT, C_RED, C_CYAN, C_MAGENTA, C_VIOLET, C_VIOLET_D,
    C_ITEM_NUBE, C_ITEM_SEMILLA, C_ITEM_LOMBRIZ, C_ITEM_ALPISTE,
    C_LED_OFF,
    /* Fase 2 (siempre al final: los índices anteriores no cambian). */
    C_GRASS_A, C_GRASS_A_D, C_APPLE_ROT, C_WOOD_L, C_BIRD, C_BIRD_L, C_STONE, C_STONE_L,
    C_COUNT
} Color;

extern const uint32_t gfx_palette[C_COUNT]; /* 0xRRGGBB */

typedef struct {
    uint8_t px[LLV_W * LLV_H];
} Framebuffer;

void gfx_clear(Framebuffer *fb, Color c);
void gfx_pset(Framebuffer *fb, int x, int y, Color c);
Color gfx_get(const Framebuffer *fb, int x, int y);
void gfx_rect(Framebuffer *fb, int x, int y, int w, int h, Color c);
void gfx_frame(Framebuffer *fb, int x, int y, int w, int h, Color c);
void gfx_dither(Framebuffer *fb, int x, int y, int w, int h, Color c1, Color c2);
void gfx_ellipse(Framebuffer *fb, int cx, int cy, int rx, int ry, Color c);
/* Bandas de cielo con dithering entre ellas. */
void gfx_bands(Framebuffer *fb, int y0, int y1, const Color *bands, int n);

/* Sprite de 4 colores escrito como texto: '.' transparente, '1' '2' '3' -> pal[0..2]. */
typedef struct {
    int w, h;
    const char *const *rows;
} Sprite;
void gfx_sprite(Framebuffer *fb, int x, int y, const Sprite *s, const Color pal[3], int scale);
/* Igual, pero espejado en horizontal. */
void gfx_sprite_flip(Framebuffer *fb, int x, int y, const Sprite *s, const Color pal[3]);
/* Vecino más cercano a un tamaño dw x dh (para "inflar" y "desinflar"). */
void gfx_sprite_stretch(Framebuffer *fb, int x, int y, const Sprite *s, const Color pal[3], int dw, int dh);

/* Caja de texto estilo Pokémon (doble borde). */
void gfx_textbox(Framebuffer *fb, int x, int y, int w, int h);

/* Fuente 5x7 (+ descendentes), avance de 6 px, UTF-8 con tildes, ñ, ¿, ¡, « », ·. */
#define GFX_CHAR_W 6
#define GFX_LINE_H 9
int gfx_text(Framebuffer *fb, int x, int y, const char *utf8, Color c);
int gfx_text_width(const char *utf8);
/* Ajusta a 'w' px por palabras. Devuelve el número de líneas dibujadas (o que se dibujarían si fb es NULL). */
int gfx_text_wrap(Framebuffer *fb, int x, int y, int w, const char *utf8, Color c);
void gfx_text_center(Framebuffer *fb, int y, const char *utf8, Color c);

/* Dígitos grandes 3x5 escalables ('0'-'9' y ':'). */
int gfx_digits(Framebuffer *fb, int x, int y, const char *s, Color c, int scale);
int gfx_digits_width(const char *s, int scale);

#endif
