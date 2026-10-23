/* Salida de audio de mentira para la herramienta headless: no suena nada. Lleva la cuenta de lo
 * que "pediría el reloj" (la herramienta lo avanza con hal_audio_stub_advance) y entrega las muestras
 * a un observador opcional (grabar un WAV, medir). Así el motor de música corre por el mismo camino
 * que en el simulador y en el llavero (hal_audio_pump) y el visualizador sigue siendo lógico. */
#include "hal_audio_stub.h"

static size_t pending;
static HalAudioSink sink;
static void *sink_user;

void hal_audio_stub_set_sink(HalAudioSink fn, void *user)
{
    sink = fn;
    sink_user = user;
}

void hal_audio_stub_advance(size_t samples)
{
    pending += samples;
    if (pending > HAL_AUDIO_STUB_MAX) /* como un buffer de verdad: lo que no se pidió a tiempo se pierde */
        pending = HAL_AUDIO_STUB_MAX;
}

size_t hal_audio_wanted(void)
{
    return pending;
}

void hal_audio_push(const int16_t *pcm, size_t n)
{
    pending = n >= pending ? 0 : pending - n;
    if (sink)
        sink(sink_user, pcm, n);
}
