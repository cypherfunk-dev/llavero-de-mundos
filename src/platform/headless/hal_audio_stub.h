/* Audio de mentira de la herramienta headless (ver hal_audio_stub.c). */
#ifndef LLV_HAL_AUDIO_STUB_H
#define LLV_HAL_AUDIO_STUB_H

#include <stddef.h>
#include <stdint.h>

#include "../../hal/hal.h"

#define HAL_AUDIO_STUB_MAX 8192

typedef void (*HalAudioSink)(void *user, const int16_t *pcm, size_t n);

void hal_audio_stub_set_sink(HalAudioSink fn, void *user);
/* El reloj avanza: la salida "pide" estas muestras más. */
void hal_audio_stub_advance(size_t samples);

#endif
