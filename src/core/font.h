#ifndef LLV_FONT_H
#define LLV_FONT_H

#include <stdint.h>

/* 8 filas de 5 bits (0x10 = columna izquierda). Nunca devuelve NULL. */
const uint8_t *font_glyph(uint32_t codepoint);
/* Decodifica un carácter UTF-8 y avanza el puntero. */
uint32_t font_utf8_next(const char **s);

#endif
