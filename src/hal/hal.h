/* HAL: las cinco piezas que cada plataforma debe aportar (spec §9.1).
 * El motor (src/core) NO incluye este archivo: solo el bucle principal de cada
 * plataforma (simulador SDL, llavero RP2350, web) usa la HAL y le pasa al motor
 * entradas, tiempo y bytes ya resueltos. */
#ifndef LLV_HAL_H
#define LLV_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/engine.h"
#include "../core/gfx.h"

/* ---------- ciclo de vida ---------- */
/* 'arg' es opaco para el motor; cada plataforma define su configuración. */
int hal_init(void *arg);   /* 0 = ok */
void hal_shutdown(void);

/* ---------- display ---------- */
/* Presenta el framebuffer indexado (convertir con gfx_palette). screen_on = false apaga
 * la pantalla (negro / retroiluminación fuera). */
void hal_display_present(const Framebuffer *fb, bool screen_on);

/* ---------- input ---------- */
/* Procesa eventos del sistema y devuelve el estado crudo de los botones (BTN_A | BTN_B)
 * y si hubo una sacudida desde la última llamada. false = hay que salir. */
bool hal_input_poll(RawInput *in);

/* ---------- clock ---------- */
/* Hora local en "segundos locales" (ver timeutil.h): segundos desde 1970-01-01 00:00
 * contados en hora local. Debe seguir contando mientras el chip duerme. */
int64_t hal_clock_local_seconds(void);
/* Milisegundos monótonos; pueden dar la vuelta (~49 días). */
uint32_t hal_clock_ms(void);
void hal_clock_delay_ms(uint32_t ms);

/* ---------- storage ---------- */
/* Lee el último guardado. Devuelve los bytes leídos (0 si no hay). */
size_t hal_storage_load(uint8_t *buf, size_t cap);
/* Escribe el guardado completo de forma atómica. true = ok. */
bool hal_storage_save(const uint8_t *buf, size_t len);

/* ---------- sound ---------- */
/* Encola tonos (onda cuadrada, frecuencia en Hz y duración en ms) en orden. */
void hal_sound_play(const Tone *tones, int count);

/* ---------- arranque común ---------- */
/* Carga el guardado con la HAL y arranca el motor: engine_resume si es válido,
 * engine_init con 'seed' si no. Devuelve true si reanudó un guardado. */
bool hal_boot_engine(Engine *e, uint64_t seed);
/* Duerme el motor y guarda su estado. true = guardado correcto. */
bool hal_save_engine(Engine *e);

#endif
