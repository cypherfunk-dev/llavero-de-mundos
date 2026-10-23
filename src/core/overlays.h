/* Superposiciones de las sorpresas (spec §6): cartel de "salí", invasión de un visitante,
 * cartucho sucio, arranque falso y INSERT COIN. Son solo dibujo: el estado de la sorpresa
 * vive en GameState.dir y el motor decide cuándo se pintan. */
#ifndef LLV_OVERLAYS_H
#define LLV_OVERLAYS_H

#include <stdint.h>

#include "gfx.h"
#include "rng.h"

/* Oscurece con tramado la escena (y deja el HUD) y pone un cartel de madera con el texto. */
void overlay_absent(Framebuffer *fb, const char *text);
/* Un visitante en un mundo que no es suyo, con su bocadillo abajo. */
void overlay_invasion(Framebuffer *fb, int spk, const char *text, uint32_t ms);
/* Imagen rota: filas desplazadas, bloques de color cambiado y el aviso en el centro. */
void overlay_glitch(Framebuffer *fb, const char *text, uint32_t ms, Rng *fx);
/* Arranque de consola noventera falsa; t_ms = tiempo desde que empezó. */
void overlay_boot(Framebuffer *fb, uint32_t t_ms);
/* Pantalla de arcade: INSERT COIN parpadeando. */
void overlay_coin(Framebuffer *fb, const char *text, uint32_t ms);

#endif
