/* Buenos hábitos sin sermones (spec §3.3). Reglas: como mucho LLV_HABIT_MAX_PER_DAY preguntas
 * al día, nunca la misma dos veces, cada hábito se activa y desactiva (máscara en el estado),
 * cumplirlo ayuda al universo y no cumplirlo no castiga nada. Todo vive en GameState.hab. */
#ifndef LLV_HABITS_H
#define LLV_HABITS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

enum { HA_EFFECT = 1, HA_LIE = 2 };

const char *habit_title(int h);   /* para el menú: "Beber agua" */
const char *habit_label(int h);   /* para el diario: "agua" (cabe tras "Cuidado: ") */
bool habit_enabled(const GameState *gs, int h);
void habit_set_enabled(GameState *gs, int h, bool on);

/* Cambio de día (05:00): se olvidan las preguntas y los logros de ayer. */
void habits_new_day(GameState *gs);
/* Una mirada: de madrugada se apunta (la pregunta del sueño y la mentira dependen de ello). */
void habits_on_look(GameState *gs, int64_t now);

/* ¿Se puede preguntar ahora esta pregunta de hábito (0..HAB_WITH_QUESTION-1)? Hábito activo, sin
 * preguntar ni cumplir hoy, dentro de su franja del día, por debajo del tope y sin repetir. */
bool habit_question_ok(const GameState *gs, int q, int64_t now);
/* Anota que se ha preguntado (cuenta para el tope y para no insistir). */
void habit_note_asked(GameState *gs, int q, int64_t now);
/* La respuesta a una pregunta de hábito. Un sí aplica el efecto una vez al día. */
int habit_answer(GameState *gs, int h, bool yes);
/* Cumplir un hábito sin pregunta (respirar con la mascota): una vez al día. */
bool habit_do(GameState *gs, int h);
/* Terminó un ejercicio de respiración con 'cycles' respiraciones completas. */
bool habit_breath(GameState *gs, int cycles);
/* Lectura del podómetro (contador que solo crece). Un paseo largo suma un objeto, máx. 1 al día. */
void habit_steps(GameState *gs, uint32_t total_steps);

#define HAB_WITH_QUESTION 6   /* los hábitos 0..5 preguntan; moverse lo mide el podómetro */

#endif
