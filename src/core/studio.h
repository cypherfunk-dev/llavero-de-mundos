/* El Estudio de la música (v1.1, spec §4.4): la tabla de parámetros que el usuario puede tocar dentro
 * del mundo Música. Es solo el modelo (nombres, valores, ciclo y texto de cada fila): el motor decide
 * cuándo se abre y se cierra (engine.c), el mundo lo dibuja (worlds/musica.c) y el generador lo aplica
 * (music.c, MusicStudio). Los valores viven en GameState.mus.studio (empaquetados).
 *
 * Cada fila de generación tiene el valor 0 = AUTO (lo decide la semilla, la hora y el clima) y el resto
 * FIJO (lo elegido; "mutar" y "semilla nueva" no lo tocan). Las voces son interruptores (suena/muda) y el
 * dado es una acción (mantener A). A (toque) pasa al valor siguiente, de forma cíclica. */
#ifndef LLV_STUDIO_H
#define LLV_STUDIO_H

#include <stdbool.h>
#include <stddef.h>

#include "music.h"

typedef enum {
    SR_SCALE, SR_KEY, SR_TEMPO, SR_ARP, SR_PROG, SR_DUTY, SR_RHYTHM,
    SR_LEAD, SR_BASS, SR_NOISE,
    SR_DICE,
    SR_COUNT
} StudioRow;

#define STUDIO_VISIBLE 6   /* filas que se ven a la vez en el panel */

const char *studio_row_name(int row);                 /* "ESCALA" */
bool studio_row_is_switch(int row);                   /* las tres voces: suena / muda */
bool studio_row_is_action(int row);                   /* el dado: se ejecuta manteniendo A */
int studio_row_values(int row);                       /* longitud del ciclo (AUTO incluido); 1 en el dado */
int studio_row_value(const MusicStudio *s, int row);  /* índice actual dentro del ciclo */
bool studio_row_locked(const MusicStudio *s, int row);/* FIJO: solo en las filas de generación */
/* Texto del valor actual: "AUTO", "DÓRICA", "100 BPM", "I-V-vi-IV", "MUDA"... */
void studio_value_text(char *buf, size_t n, const MusicStudio *s, int row);
/* Texto de un valor concreto del ciclo (0 = AUTO en las filas de generación). */
void studio_text_of(char *buf, size_t n, int row, int value);
/* Pasa al valor siguiente del ciclo (el dado no cambia). */
void studio_cycle(MusicStudio *s, int row);
/* Pone un valor del ciclo (para pruebas y herramientas). */
void studio_set(MusicStudio *s, int row, int value);
/* Primera fila visible para que la seleccionada se vea, con 'visible' filas de 'count'. */
int studio_first_row(int sel, int count, int visible);
const char *studio_note_name(int pitch_class);        /* "Do#" */

#endif
