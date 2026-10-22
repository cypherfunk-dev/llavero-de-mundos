/* Salida de imágenes sin dependencias: escalado del framebuffer, PNG (bloques stored) y PPM.
 * Lo usan el simulador (capturas, textura) y la herramienta headless. Sin malloc. */
#ifndef LLV_IMGOUT_H
#define LLV_IMGOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/gfx.h"

#define IMG_MAX_SCALE 8

/* Renderiza el framebuffer en píxeles 0xRRGGBB de (LLV_W*scale) x (LLV_H*scale).
 * lcd: rejilla sutil entre píxeles (spec §12.2); solo se nota con scale >= 3. */
void img_render(const Framebuffer *fb, int scale, bool lcd, uint32_t *out);

/* Devuelven 0 si todo fue bien. 'px' son píxeles 0xRRGGBB. */
int img_write_png(const char *path, const uint32_t *px, int w, int h);
int img_write_ppm(const char *path, const uint32_t *px, int w, int h);

#endif
