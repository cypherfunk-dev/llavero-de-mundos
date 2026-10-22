/* Arte del Jardín y del Cobertizo (spec §12, §13): sprites de 4 colores (3 + transparente)
 * escritos como texto, con paleta por estación y animaciones de 2 frames. */
#ifndef LLV_ART_H
#define LLV_ART_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "garden.h"
#include "timeutil.h"

/* Dibuja una entidad en su casilla (x, y = esquina superior izquierda de 16x16).
 * 'var' da pequeñas diferencias fijas entre casillas; ms mueve las animaciones. */
void art_entity(Framebuffer *fb, const Entity *e, int x, int y, Season season, uint32_t ms, uint32_t var);

/* Icono de un objeto de la bolsa (12x12). */
void art_item(Framebuffer *fb, int x, int y, ItemKind item, uint32_t ms);
/* Icono de la colección (12x10). known = false: silueta "???". */
void art_coll_icon(Framebuffer *fb, int x, int y, CollSlot slot, bool known);
/* Estrella de 9x9 (deseo, estrella fugaz). */
void art_star(Framebuffer *fb, int x, int y, Color body, Color edge);

/* Mascota (spec §4.7): centrada en cx, apoyada en base_y. inflate 0..1000 la infla;
 * joy 0..1000 la pone contenta (sonrisa y corazones). */
void art_pet(Framebuffer *fb, int cx, int base_y, const Pet *p, uint32_t ms, int inflate, int joy, int sun_dx);

/* Verifica que cada sprite tenga las filas del tamaño declarado (para los tests). */
int art_selfcheck(void);

#endif
