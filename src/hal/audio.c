/* Bucle común de audio, escrito solo con la HAL: del motor a la salida de muestras. */
#include "hal.h"

#define PUMP_CHUNK 512

void hal_audio_pump(Engine *e)
{
    int16_t buf[PUMP_CHUNK];
    size_t want = hal_audio_wanted();
    while (want > 0) {
        size_t chunk = want > PUMP_CHUNK ? PUMP_CHUNK : want;
        size_t n = engine_audio_render(e, buf, chunk);
        if (n == 0)
            break; /* nada suena: no se manda silencio, la salida se queda en reposo */
        hal_audio_push(buf, n);
        want -= n;
    }
}
