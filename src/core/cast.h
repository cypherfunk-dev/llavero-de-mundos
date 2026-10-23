/* Los personajes de la Sala (spec §4.3, §12): sprites de 4 colores (3 + transparente) en capas,
 * como los objetos de una GBC: cabeza, torso y piernas/falda, con animación de 2 frames
 * y expresiones mínimas. Las caras se dibujan encima, en píxeles, según la expresión. */
#ifndef LLV_CAST_H
#define LLV_CAST_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

#define CAST_W 16
#define CAST_H 24

typedef enum {
    EXPR_NEUTRAL, EXPR_TALK, EXPR_HAPPY, EXPR_SURPRISE, EXPR_SLEEPY, EXPR_SMUG, EXPR_COUNT
} Expr;

/* Dibuja al personaje 'spk' (SPK_*) con la esquina superior izquierda en (x, y). 'pajama':
 * en madrugada salen en pijama y con gorro de dormir. ms mueve el balanceo y el parpadeo. */
void cast_draw(Framebuffer *fb, int spk, int x, int y, Expr expr, uint32_t ms, bool pajama);
/* Solo la cabeza (12 px de alto), para las burbujas de la caja de texto. */
void cast_draw_head(Framebuffer *fb, int spk, int x, int y, Expr expr, uint32_t ms);
/* Comprueba que cada sprite tenga las medidas declaradas (para los tests). Devuelve filas malas. */
int cast_selfcheck(void);

#endif
