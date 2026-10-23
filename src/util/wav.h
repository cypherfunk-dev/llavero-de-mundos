/* Escritura de WAV (RIFF, PCM mono de 16 bits). Para las herramientas; no entra en el firmware. */
#ifndef LLV_WAV_H
#define LLV_WAV_H

#include <stddef.h>
#include <stdint.h>

/* Devuelve 0 si todo fue bien. */
int wav_write(const char *path, const int16_t *pcm, size_t n, uint32_t rate);

#endif
